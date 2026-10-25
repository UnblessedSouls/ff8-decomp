#include "common.h"
#include "psxsdk/libpad.h"
#include "psxsdk/libgpu.h"
#include "psxsdk/libapi.h"
#include "psxsdk/libetc.h"
#include "psxsdk/r3000.h"
#include "battle.h"
#include "field_init.h"
#include "input/gamepad.h"

enum {
    CONTROLLER_PORT_1 = 0x00,
    CONTROLLER_PORT_2 = 0x10,
    CONTROLLER_PORT_1_MULTITAP_A = 0x00,
    CONTROLLER_PORT_1_MULTITAP_B = 0x01,
    CONTROLLER_PORT_1_MULTITAP_C = 0x02,
    CONTROLLER_PORT_1_MULTITAP_D = 0x03,
    CONTROLLER_PORT_2_MULTITAP_A = 0x10,
    CONTROLLER_PORT_2_MULTITAP_B = 0x11,
    CONTROLLER_PORT_2_MULTITAP_C = 0x12,
    CONTROLLER_PORT_2_MULTITAP_D = 0x13
};

#define GAMEPAD_TYPE_MOUSE 1
#define GAMEPAD_TYPE_ANALOG_JOYSTICK 5
#define GAMEPAD_TYPE_ANALOG_CONTROLLER 7

typedef struct {
    u8 inputReadFailed;
    u8 gamePadInputIdentifier;
    u8 buttons[2];
    union {
        u8 analog[4];
        s8 mouseMovement[2];
    } data;
} GamePadReceiveData;

static s32 UpdateAutoRepeatForGroup(EngineState *engine, GamePad *gamePad, u16 previousButtons, u16 currentButtons, s32 buttonGroup);
static void SetupActuators(EngineState *engine, GamePad *gamePad, s32 port);
static void UpdateGamePad(EngineState *engine, GamePad *gamePad, s32 port);
static void UpdateGamePads(EngineState *engine);
static u16 getPadReadReleased(s32 idx, s32 offset);
static inline GamePadInputFrame *GetHistoricalFrame(s32 idx, s32 offset);
static s32 getPadReadByte(s32 idx, s32 param, s32 frameOffset);
static s32 getPadReadType(s32 idx, s32 frameOffset);
static void resetPadInput(s32 idx, s32 buttons);

extern u8 g_animInitialized;
extern u8 g_animFlag;
extern u16 g_animState;

static s32 UpdateAutoRepeatForGroup(EngineState *engine, GamePad *gamePad, u16 previousButtons, u16 currentButtons, s32 buttonGroup) {
    s32 counter;
    s32 interval;
    u16 restartDelay;
    u16 repeatInterval;
    u16 mask;

    restartDelay = engine->repeatDelays.hword;
    mask = gamePad->autoRepeatButtonMasks[buttonGroup];
    counter = gamePad->autoRepeatCountdowns[buttonGroup];

    interval = restartDelay >> 8;
    restartDelay &= 0xFF;
    repeatInterval = interval;
    currentButtons &= mask;
    previousButtons &= mask;
    if (currentButtons & previousButtons) {
        if (currentButtons != previousButtons) {
            counter = restartDelay;
        }
        counter--;
        if (counter < 0) {
            counter = repeatInterval;
        } else {
            currentButtons = 0;
        }
    } else {
        counter = restartDelay;
    }
    gamePad->autoRepeatCountdowns[buttonGroup] = counter;
    return currentButtons;
}

/**
 * @brief Set up actuators (vibration motors) for a given gamepad on the given port
 *
 * @param engine Engine reference.
 * @param gamePad Gamepad to update.
 * @param port PS1 gamepad port number (see CONTROLLER_PORT_*)
 */
static void SetupActuators(EngineState *engine, GamePad *gamePad, s32 port) {
    s32 actuator;
    s32 j;
    s32 info;
    s32 leftActuator = -1;
    s32 rightActuator = -1;
    s32 rightIntensityBits;
    s32 leftIntensityBits;
    u8 *align = gamePad->actuatorAlign;
    u8 *p;

    actuator = PadInfoAct(port, -1, 0);
    gamePad->actuatorCount = actuator;
    for (actuator = actuator - 1; actuator >= 0; actuator--) {
        info = PadInfoAct(port, actuator, InfoActFunc);
        if (info == 1) {
            info = PadInfoAct(port, actuator, InfoActSub);
            if (info == 1) {
                leftActuator = actuator;
                info = PadInfoAct(port, leftActuator, InfoActSize);
                if (info == 0) {
                    leftIntensityBits = 1;
                } else {
                    leftIntensityBits = info << 3;
                }
            } else if (info == 2) {
                rightActuator = actuator;
                info = PadInfoAct(port, rightActuator, InfoActSize);
                if (info == 0) {
                    rightIntensityBits = 1;
                } else {
                    rightIntensityBits = info << 3;
                }
            }
        }
    }
    p = align;
    for (j = 0; j < 6; j++) {
        *p++ = 0xFF;
    }
    if (rightActuator != -1) {
        rightIntensityBits = 8 - rightIntensityBits;
        align[1] = rightActuator;
        gamePad->vibrationIntensityShift[0] = rightIntensityBits;
    }
    if (leftActuator != -1) {
        leftIntensityBits = 8 - leftIntensityBits;
        align[2] = leftActuator;
        gamePad->vibrationIntensityShift[1] = leftIntensityBits;
    }
}


/**
 * @brief Update the state of a given GamePad using a given port
 *
 * @param engine Engine reference.
 * @param gamePad Gamepad to update.
 * @param port PS1 gamepad port number (See CONTROLLER_PORT_*)
 */
static void UpdateGamePad(EngineState *engine, GamePad *gamePad, s32 port) {
    s32 state;
    GetSr();
    state = PadGetState(port);
    gamePad->gamePadState = state;
    switch (state) {
    case PadStateFindPad:
        gamePad->actuatorCount = 0;
        gamePad->actuatorDataChanged = 1;
        gamePad->actuatorSetupPending = 1;
        break;
    case PadStateFindCTP1:
        gamePad->actuatorCount = 0;
        gamePad->actuatorSetupPending = 1;
        gamePad->actuatorDataChanged = 1;
        gamePad->vibrationMask = 0;
        break;
    case PadStateReqInfo:
    case PadStateExecCmd:
        break;
    case PadStateStable: {
        u8 mask = gamePad->vibrationMask;
        if (gamePad->actuatorSetupPending == 1) {
            SetupActuators(engine, gamePad, port);
            gamePad->actuatorDataChanged = 1;
            gamePad->actuatorSetupPending = 0;
            gamePad->gamePadTransmitBuf[1] = 0;
            gamePad->gamePadTransmitBuf[2] = 0;
            PadSetActAlign(port, gamePad->actuatorAlign);
            PadSetAct(port, gamePad->gamePadTransmitBuf, 6);
        } else {
            u8 rightVibrationIntensity = gamePad->vibrationIntensity[0];
            u8 leftVibrationIntensity = gamePad->vibrationIntensity[1];
            u8 leftShift = gamePad->vibrationIntensityShift[1];
            u8 rightShift;
            s32 rightMasked;
            s32 leftMasked;
            do {
                gamePad->actuatorDataChanged = 1;
                rightMasked = rightVibrationIntensity & mask;
            } while (0); // matching hack
            leftMasked = leftVibrationIntensity & mask;
            rightShift = gamePad->vibrationIntensityShift[0];
            gamePad->gamePadTransmitBuf[2] = leftMasked >> leftShift;
            gamePad->gamePadTransmitBuf[1] = rightMasked >> rightShift;
        }
        break;
    }
    case PadStateDiscon:
    case PadStateFindCTP2:
    default:
        gamePad->actuatorCount = 0;
        gamePad->actuatorDataChanged = 1;
        gamePad->vibrationMask = 0;
        gamePad->actuatorSetupPending = 1;
        break;
    }
}

s32 TrySetAnalogMode(s32 port) {
    s32 status;
    s32 ret;
    status = PadGetState(port);
    ret = 0;
    switch (status) {
    case PadStateFindCTP1:
        ret = 1;
        break;
    case PadStateStable:
        ret = PadSetMainMode(port, 1, 1);
        break;
    case PadStateDiscon:
    case PadStateFindPad:
    case PadStateFindCTP2:
    case PadStateReqInfo:
    case PadStateExecCmd:
        break;
    }
    return ret;
}

void RequestActuatorSetup(s32 idx) {
    GamePad *port = &g_engine.gamePads[idx];
    port->actuatorSetupPending = 1;
    port->actuatorDataChanged = 1;
}

static void UpdateGamePads(EngineState *engine) {
    UpdateGamePad(engine, &engine->gamePads[0], CONTROLLER_PORT_1);
    UpdateGamePad(engine, &engine->gamePads[1], CONTROLLER_PORT_2);
}

void WaitForGamePadDetection(void) {
    EngineState *engine = &g_engine;
    GamePad *gamePad;
    s32 stableReads0;
    s32 stableReads1;

    gamePad = &engine->gamePads[0];
    stableReads0 = 0;
    gamePad->vibrationIntensity[0] = 0;
    gamePad->vibrationIntensity[1] = 0;
    while (1) {
        while (PadChkVsync() == 0) {} // await communication with controller
        UpdateGamePad(engine, gamePad, CONTROLLER_PORT_1);
        if (gamePad->gamePadState == PadStateDiscon || gamePad->gamePadState == PadStateFindCTP1) {
            break;
        }
        if (gamePad->gamePadState == PadStateStable) {
            stableReads0++;
            if (stableReads0 >= 2) {
                break;
            }
        }
    }

    gamePad = &engine->gamePads[1];
    stableReads1 = 0;
    gamePad->vibrationIntensity[0] = 0;
    gamePad->vibrationIntensity[1] = 0;
    while (1) {
        while (PadChkVsync() == 0) {} // await communication with controller
        UpdateGamePad(engine, gamePad, CONTROLLER_PORT_2);
        if (gamePad->gamePadState == PadStateDiscon || gamePad->gamePadState == PadStateFindCTP1) {
            break;
        }
        if (gamePad->gamePadState == PadStateStable) {
            stableReads1++;
            if (stableReads1 >= 2) {
                break;
            }
        }
    }
}

/**
 * @brief Set the area the mouse cursor is kept inside.
 *
 * @param rect The area, in screen pixels.
 */
void SetMouseBounds(RECT *rect) {
    g_engine.mouseBoundsLeft = rect->x;
    g_engine.mouseBoundsTop = rect->y;
    g_engine.mouseBoundsRight = rect->x + rect->w - 1;
    g_engine.mouseBoundsBottom = rect->y + rect->h - 1;
}

s32 GetActuatorCountForGamePad(s32 idx) {
    GamePad *gamePad = &g_engine.gamePads[idx];
    return gamePad->actuatorCount;
}

/**
 * @brief Updates gamepad states and records their input in the input ring buffer
 */
void RecordGamePadInput(void) {
    EngineState *engine = &g_engine;
    GamePad *gamePad;
    GamePadReceiveData *receiveBuf;
    GamePadInputFrame *frame;
    u32 sr;
    s32 i;
    s32 inputSourceIndex;
    s32 frameIndex;
    u8 gamePadType;
    s32 previousButtons;
    s32 currentButtons;
    s32 mouseX;
    s32 mouseY;
    s32 stickX;
    s32 stickY;

    UpdateGamePads(engine);
    gamePad = engine->gamePads;
    sr = GetSr();
    if (!(sr & SR_IEP)) {
        EnterCriticalSection();
    }
    for (i = 0; i < 2; i++, gamePad++) {
        receiveBuf = (GamePadReceiveData *)engine->gamePadReceiveBuf[i];
        inputSourceIndex = i;
        gamePadType = receiveBuf->gamePadInputIdentifier >> 4;
        frameIndex = gamePad->frameCounter;
        previousButtons = gamePad->frames[frameIndex].currentlyPressedButtons;
        currentButtons = 0;
        if (gamePad->gamePadState != PadStateDiscon) {
            if (receiveBuf->inputReadFailed != 0) {
                currentButtons = previousButtons;
            } else {
                currentButtons = receiveBuf->buttons[0] << 8;
                currentButtons += receiveBuf->buttons[1];
                currentButtons ^= 0xFFFF;
                if ((currentButtons & (PADLleft | PADLright)) == (PADLleft | PADLright)) {
                    currentButtons &= ~(PADLleft | PADLright);
                }
                if ((currentButtons & (PADLup | PADLdown)) == (PADLup | PADLdown)) {
                    currentButtons &= ~(PADLup | PADLdown);
                }
                if (engine->animFlag != 0 && i == 0 && currentButtons == 0) {
                    if (gamePadType == GAMEPAD_TYPE_ANALOG_CONTROLLER || gamePadType == GAMEPAD_TYPE_ANALOG_JOYSTICK) {
                        frame = (GamePadInputFrame *)receiveBuf;
                        stickX = frame->analog[0] - 0x80;
                        stickY = frame->analog[1] - 0x80;
                        if (stickX * stickX + stickY * stickY < 40 * 40) {
                            stickX = frame->analog[2] - 0x80;
                            stickY = frame->analog[3] - 0x80;
                            if (stickX * stickX + stickY * stickY < 40 * 40) {
                                inputSourceIndex = 1;
                            }
                        }
                    } else {
                        inputSourceIndex = 1;
                    }
                }
                if (gamePadType == GAMEPAD_TYPE_MOUSE) {
                    mouseX = receiveBuf->data.mouseMovement[0];
                    mouseY = receiveBuf->data.mouseMovement[1];
                    mouseX += engine->mousePositions[i][0];
                    mouseY += engine->mousePositions[i][1];
                    mouseX = (mouseX < engine->mouseBoundsLeft) ? engine->mouseBoundsLeft : (mouseX > engine->mouseBoundsRight) ? engine->mouseBoundsRight : mouseX;
                    mouseY = (mouseY < engine->mouseBoundsTop) ? engine->mouseBoundsTop : (mouseY > engine->mouseBoundsBottom) ? engine->mouseBoundsBottom : mouseY;
                    currentButtons &= MOUSEleft | MOUSEright;
                    engine->mousePositions[i][0] = mouseX;
                    engine->mousePositions[i][1] = mouseY;
                }
            }
            if (engine->pad1E2 == 0) {
                currentButtons = 0;
                previousButtons = 0;
            }
        }
        frameIndex++;
        frameIndex &= 7;
        frame = &gamePad->frames[frameIndex];
        ((u32 *)frame)[0] = ((u32 *)receiveBuf)[0];
        ((u32 *)frame)[1] = ((u32 *)receiveBuf)[1];
        frame->currentlyPressedButtons = currentButtons;
        frame->newlyPressedButtons = (previousButtons ^ currentButtons) & currentButtons;
        frame->newlyReleasedButtons = (previousButtons ^ currentButtons) & ~currentButtons;
        frame->autoRepeatedButtons[0] = UpdateAutoRepeatForGroup(engine, gamePad, previousButtons, currentButtons, 0);
        frame->autoRepeatedButtons[1] = UpdateAutoRepeatForGroup(engine, gamePad, previousButtons, currentButtons, 1);
        frame->autoRepeatedButtons[2] = UpdateAutoRepeatForGroup(engine, gamePad, previousButtons, currentButtons, 2);
        frame->autoRepeatedButtons[3] = UpdateAutoRepeatForGroup(engine, gamePad, previousButtons, currentButtons, 3);
        gamePad->frameCounter = frameIndex;
        gamePad->inputSourceIndex = inputSourceIndex;
    }
    if (!(sr & SR_IEP)) {
        ExitCriticalSection();
    }
}

static inline GamePadInputFrame *GetHistoricalFrame(s32 idx, s32 offset) {
    GamePad *gamePad;
    idx &= 1; // & 1 is same as % 2 (gamePads size is 2)
    gamePad = &g_engine.gamePads[g_engine.gamePads[idx].inputSourceIndex];
    return &gamePad->frames[(gamePad->frameCounter - offset) & 7]; // & 7 is basically modulo 8. (The input ring buffer is size 8)
}

u16 getPadReadButtons(s32 idx, s32 offset) {
    return GetHistoricalFrame(idx, offset)->currentlyPressedButtons;
}

u16 getPadReadRepeat(s32 idx, s32 offset) {
    GamePadInputFrame *frame = GetHistoricalFrame(idx, offset);
    return frame->autoRepeatedButtons[0] | frame->autoRepeatedButtons[1] | frame->autoRepeatedButtons[2] | frame->autoRepeatedButtons[3];
}

u16 getPadReadPressed(s32 idx, s32 offset) {
    return GetHistoricalFrame(idx, offset)->newlyPressedButtons;
}

static u16 getPadReadReleased(s32 idx, s32 offset) {
    return GetHistoricalFrame(idx, offset)->newlyReleasedButtons;
}


/**
 * @brief Compute angle from (x, y)
 *
 * @param x X component (horizontal).
 * @param y Y component (vertical).
 * @return Angle in fixed-point (0x1000 = 360 degrees).
 */
inline s32 CalculateVectorAngle(s32 x, s32 y) { // Original devs possibly forgot to make it static
    s32 angle;

    if (x == 0) {
        angle = 0xC00;
        if (y > 0) {
            angle = 0x400;
        }
    } else if (y == 0) {
        angle = (x < 1) << 11;
    } else {
        angle = ratan2(x, y);
    }
    return angle;
}


/**
 * @brief Map an angle to d-pad direction
 *
 * @param angle Angle in fixed-point (4096 = 360 degrees).
 * @return The d-pad button bit for the angle's direction (PADLup, PADLright, PADLdown or PADLleft).
 */
inline s32 AngleToDPadButton(s32 angle) { // Original devs possibly forgot to make it static
    s32 v = angle + 1536;
    v = ~v & 0xFFF;
    v = v / 1024;
    return 1 << (v + 12);
}

/**
 * @brief Get the analog stick dead zone of the port linked to port @p a0.
 * @param a0 Port index (only bit 0 used).
 * @return (analogStickConfig & 0x7F) << 8. func_80027CF8 reports no direction while the
 * stick's squared distance from the centre is below it.
 */
s32 getPadDeadZone(s32 a0) {
    s32 slot;
    a0 &= 1;
    slot = g_engine.gamePads[a0].inputSourceIndex;
    return (g_engine.gamePads[slot].analogStickConfig & 0x7F) << 8;
}


/**
 * @brief Set the analog stick dead zone of the port linked to port @p a0.
 *
 * Clamps @p a1 to [0, 0x7F], squares it and divides by 256 into bits 0-6 of
 * analogStickConfig, keeping bit 7 (see setPadAnalogFlag).
 *
 * @param a0 Port index (only bit 0 used).
 * @param a1 Dead-zone level, clamped to [0, 0x7F].
 */
void setPadDeadZone(s32 a0, s32 a1) {
    s32 slot;
    GamePad *linked;
    s32 sq;
    s32 clamped;
    sq = a1;
    a0 &= 1;
    slot = g_engine.gamePads[a0].inputSourceIndex;
    linked = &g_engine.gamePads[slot];
    clamped = (sq < 0) ? 0 : (sq > 0x7F) ? 0x7F : a1;
    sq = clamped * clamped;
    sq = sq / 0x100;
    linked->analogStickConfig = (linked->analogStickConfig & PAD_PORT_SWAP_ANALOG_STICKS) | sq;
}


/**
 * @brief Set or clear bit 7 of analogStickConfig, the CONFIG_ANALOG option, on the port linked to port @p a0.
 * @param a0 Port index (only bit 0 used).
 * @param a1 Nonzero to set the bit, zero to clear it.
 */
void setPadAnalogFlag(s32 a0, s32 a1) {
    s32 slot;
    GamePad *linked;
    a0 &= 1;
    slot = g_engine.gamePads[a0].inputSourceIndex;
    linked = &g_engine.gamePads[slot];
    if (a1) {
        linked->analogStickConfig |= PAD_PORT_SWAP_ANALOG_STICKS;
    } else {
        linked->analogStickConfig &= ~PAD_PORT_SWAP_ANALOG_STICKS;
    }
}


s32 func_80027CF8(s32 a, s32 b, s32 c) {
    s32 zone;
    u16 bits;

    zone = getPadDeadZone(a);
    if (b * b + c * c < zone) {
        return 0;
    }
    bits = AngleToDPadButton(CalculateVectorAngle(b, c));
    return bits;
}


s32 func_80027DB4(s32 pad, s32 axis, s32 c) {
    GamePad *port;
    GamePad *linked;
    GamePadInputFrame *frame;
    s32 frameSlot;
    s32 result;
    s32 type;

    pad &= 1;
    port = &g_engine.gamePads[g_engine.gamePads[pad & 1].inputSourceIndex];
    frameSlot = (port->frameCounter - c) & 7;
    result = -1;
    linked = &g_engine.gamePads[g_engine.gamePads[pad].inputSourceIndex];
    frame = &port->frames[frameSlot];
    if (frame->inputReadFailed == 0) {
        if (linked->analogStickConfig & PAD_PORT_SWAP_ANALOG_STICKS) {
            axis ^= 2;
        }
        axis = CLAMP(axis, 0, 3);
        type = frame->gamePadInputIdentifier >> 4;
        if (type == GAMEPAD_TYPE_ANALOG_CONTROLLER || type == GAMEPAD_TYPE_ANALOG_JOYSTICK) {
            result = frame->analog[axis];
        }
    }
    return result;
}


/**
 * @brief Get a pad port's vibration mask.
 * @param idx Port index (masked to 0 or 1).
 * @return 0xFF with vibration on, 0 with it off.
 */
s32 getPadVibration(s32 idx) {
    GamePad *port = &g_engine.gamePads[idx & 1];
    return port->vibrationMask;
}


/**
 * @brief Turn a pad port's vibration on or off.
 * @param idx Port index (masked to 0 or 1).
 * @param val Nonzero for on.
 */
void setPadVibration(s32 idx, s32 val) {
    GamePad *port = &g_engine.gamePads[idx & 1];
    if (val != 0) {
        port->vibrationMask = 0xFF;
    } else {
        port->vibrationMask = 0;
    }
}


/**
 * @brief Set a pad port's motor levels.
 *
 * Updates vibrationIntensity[1] and vibrationIntensity[0] (skipped if < 0), then sets actuatorDataChanged to 1.
 *
 * @param idx Port index (masked to 0 or 1).
 * @param motor1 Level for vibrationIntensity[1] (-1 to skip).
 * @param motor0 Level for vibrationIntensity[0] (-1 to skip).
 */
void setPadMotors(s32 idx, s32 motor1, s32 motor0) {
    GamePad *port = &g_engine.gamePads[idx & 1];
    if (motor1 >= 0) {
        port->vibrationIntensity[1] = motor1;
    }
    if (motor0 >= 0) {
        port->vibrationIntensity[0] = motor0;
    }
    port->actuatorDataChanged = 1;
}


/**
 * @brief Get one axis of a gamepad's mouse cursor position.
 * @param idx Gamepad index (masked to 0 or 1).
 * @param axis 0 = X, 1 = Y (clamped to [0,1]).
 * @return The coordinate value as s16.
 */
s16 GetMousePosition(s32 idx, s32 axis) {
    s32 slot = idx & 1;

    do { slot++; slot--; } while (0); /* Regalloc: boost slot priority for s-reg order */

    if (axis >= 0) {
        idx = 1;
        if (axis < 2) {
            idx = axis;
        }
    } else {
        idx = 0;
    }
    return g_engine.mousePositions[slot][idx];
}


/**
 * @brief Set a gamepad's mouse cursor position.
 * @param idx Gamepad index (masked to 0 or 1).
 * @param x X coordinate.
 * @param y Y coordinate.
 */
void SetMousePosition(s32 idx, s16 x, s16 y) {
    idx &= 1;
    g_engine.mousePositions[idx][0] = x;
    g_engine.mousePositions[idx][1] = y;
}


/**
 * @brief Read a data byte after the buttons in a past read on the port linked to port @p idx.
 * @param idx Port index (masked to 0 or 1).
 * @param param Byte index (clamped to [0,3]).
 * @param frameOffset How many reads back to look.
 * @return The byte, or -1 if that read failed or its gamepad type is not 1.
 */
static s32 getPadReadByte(s32 idx, s32 param, s32 frameOffset) {
    GamePad *port;
    GamePadInputFrame *frame;
    s32 frameSlot;
    s32 result;

    idx &= 1;
    port = &g_engine.gamePads[g_engine.gamePads[idx].inputSourceIndex];
    frameSlot = (port->frameCounter - frameOffset) & 7;
    result = -1;
    frame = &port->frames[frameSlot];

    if (frame->inputReadFailed == 0) {
        param = CLAMP(param, 0, 3);
        if ((frame->gamePadInputIdentifier >> 4) == GAMEPAD_TYPE_MOUSE) {
            result = frame->analog[param];
        }
    }

    return result;
}


/**
 * @brief Check whether port 0's latest read succeeded.
 * @return 1 if it did, 0 otherwise.
 */
s32 isPadConnected(void) {
    return getPadReadType(0, 0) >= 0;
}


/**
 * @brief Get the gamepad type of a past read on the port linked to port @p idx.
 * @param idx Port index (bit 0 selects the port).
 * @param frameOffset How many reads back to look.
 * @return The ID byte's high nibble, or -1 if that read failed.
 */
static s32 getPadReadType(s32 idx, s32 frameOffset) {
    s32 syncFlag;
    s32 slot;
    GamePad *port;
    GamePadInputFrame *frame;
    s32 frameSlot;

    syncFlag = GetSr() & SR_IEP;
    if (syncFlag == 0) {
        EnterCriticalSection();
    }

    slot = idx & 1;
    port = &g_engine.gamePads[g_engine.gamePads[slot].inputSourceIndex];
    frameSlot = (port->frameCounter - frameOffset) & 7;
    frame = &port->frames[frameSlot];

    if (syncFlag == 0) {
        ExitCriticalSection();
    }

    if (frame->inputReadFailed != 0) {
        return -1;
    }
    return frame->gamePadInputIdentifier >> 4;
}


/**
 * @brief Set the button mask of one auto-repeat channel on both pad ports.
 * @param unused Unused parameter.
 * @param channel Auto-repeat channel (0-3).
 * @param mask Buttons the channel repeats.
 */
void setPadRepeatMask(s32 unused, s32 channel, s32 mask) {
    int new_var;
    new_var = 1;
    g_engine.gamePads[0].autoRepeatButtonMasks[channel] = mask;
    g_engine.gamePads[new_var].autoRepeatButtonMasks[channel] = mask;
}


/**
 * @brief Reset the input state of the port linked to port @p idx.
 *
 * Restarts its read ring and auto-repeat countdowns, and clears all eight reads.
 *
 * @param idx Port index (selects via inputSourceIndex).
 * @param buttons Held buttons to store in each read.
 */
static void resetPadInput(s32 idx, s32 buttons) {
    GamePadInputFrame *fp;
    s32 fid;
    GamePad *port;
    s32 i;

    port = &g_engine.gamePads[g_engine.gamePads[idx].inputSourceIndex];
    port->frameCounter = 0;
    port->actuatorDataChanged = 0;
    port->autoRepeatCountdowns[0] = g_engine.repeatDelays.b.lo;
    port->autoRepeatCountdowns[1] = g_engine.repeatDelays.b.lo;
    port->autoRepeatCountdowns[2] = g_engine.repeatDelays.b.lo;
    port->autoRepeatCountdowns[3] = g_engine.repeatDelays.b.lo;

    fid = buttons;
    for (i = 0; 8 > i; i++) {
        GamePadInputFrame *frame = &port->frames[i];
        frame->inputReadFailed = 0;
        frame->gamePadInputIdentifier = 0;
        fp = frame;
        fp->currentlyPressedButtons = fid;
        fp->analog[0] = 0;
        frame->analog[1] = 0;
        frame->analog[2] = 0;
        frame->analog[3] = 0;
        fp->autoRepeatedButtons[0] = 0;
        frame->autoRepeatedButtons[1] = 0;
        frame->autoRepeatedButtons[2] = 0;
        frame->autoRepeatedButtons[3] = 0;
        fp->newlyPressedButtons = 0;
        fp->newlyReleasedButtons = 0;
    }
}


/**
 * @brief Reset a pad port's auto-repeat countdowns and reads.
 *
 * Sets the four field0C entries, the per-channel pad auto-repeat countdowns, to the
 * restart delay g_engine.repeatDelays.b.lo, then
 * calls resetPadInput to clear its reads.
 *
 * @param idx Port index (0 or 1).
 */
void initPadInput(s32 idx) {
    GamePad *port = &g_engine.gamePads[idx];
    port->autoRepeatCountdowns[0] = g_engine.repeatDelays.b.lo;
    port->autoRepeatCountdowns[1] = g_engine.repeatDelays.b.lo;
    port->autoRepeatCountdowns[2] = g_engine.repeatDelays.b.lo;
    port->autoRepeatCountdowns[3] = g_engine.repeatDelays.b.lo;
    resetPadInput(idx, 0);
}


void initAnimStateAndWait(void) {
    s32 i;

    g_animInitialized = 1;
    for (i = 0; i < 2; i++) {
        resetPadInput(i, 0);
    }
    PadEnableCom(3);
    for (i = 0; i < 24; i++) {
        VSync(0);
        if (TrySetAnalogMode(0) != 0) {
            break;
        }
    }
    VSync(2);
}


void resetAnimState(void) {
    s32 i;
    g_animInitialized = 0;
    PadEnableCom(0);
    for (i = 0; i < 2; i++) {
        resetPadInput(i, 0);
    }
}


/** @brief Get the current value of g_animState (global u16 state variable). */
s32 getAnimGlobalState(void) {
    return g_animState;
}


/**
 * @brief Set g_animState to a new value.
 * @param value Value to store.
 * @return The value that was set.
 */
s32 setAnimGlobalState(s32 value) {
    g_animState = value;
    return value;
}


/**
 * @brief Set the global flag g_animFlag.
 * @param value Value to store in g_animFlag.
 */
void setAnimFlag(s32 value) {
    g_animFlag = value;
}


/**
 * @brief Initialize or reset the CD audio/streaming subsystem state.
 * @note Calls several initialization functions and resets a counter at g_engine + 0x9C4 to 0.
 *       Passes two g_engine buffer pointers (offsets 0x188 and 0x1AC) to PadInitDirect.
 */
void initCdAnimSubsystem(void) {
    EngineState *bas = &g_engine;
    func_800982B8();
    PadInitDirect(bas->gamePadReceiveBuf[0], bas->gamePadReceiveBuf[1]);
    func_8003AB84();
    initAnimStateAndWait();
    bas->cdStreamCounter = 0;
}
