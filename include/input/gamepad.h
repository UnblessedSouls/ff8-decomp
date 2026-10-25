#ifndef INPUT_GAMEPAD_H
#define INPUT_GAMEPAD_H

#include "common.h"
#include "psxsdk/libgpu.h"

typedef struct {
    u8 inputReadFailed;
    u8 gamePadInputIdentifier; // high nibble is gamepad type, low is number of byte pairs (16 bit) in input
    u16 currentlyPressedButtons; // buttons currently being pressed down in the frame (1 = pressed)
    u8 analog[4]; // rightX, rightY, leftX, leftY
    u16 autoRepeatedButtons[4]; // used for repeating held down inputs. e.g. useful for scrolling lists by holding up/down
    u16 newlyPressedButtons;
    u16 newlyReleasedButtons;
} GamePadInputFrame; /* 0x14 */

typedef struct {
    u8 gamePadTransmitBuf[6];
    u8 vibrationIntensity[2]; // one for each motor in the gamepad
    u8 vibrationIntensityShift[2]; // small motor only goes 0-1 and large 0-255, this scales the vibrationIntensity[] level (0-255) so it fits each motor accurately
    u8 actuatorDataChanged;
    u8 actuatorCount; // number of vibration motors (sdk calls it actuator)
    s8 autoRepeatCountdowns[4];
    u16 autoRepeatButtonMasks[4];
    u8 frameCounter;
    s8 actuatorSetupPending;
    s8 gamePadState;
    u8 vibrationMask; // 0xFF with vibration on, 0 with it off.
    GamePadInputFrame frames[8];
    u8 actuatorAlign[6]; // actuator parameter transmission bytes (used when calling SDK PadSetActAlign)
    u8 inputSourceIndex; // this gamepad's input is read from g_engine.gamePads[inputSourceIndex]
    u8 analogStickConfig; // bits 0-6 = value of analog deadzone, bit 7 swaps which analog stick should be used
} GamePad;

void RecordGamePadInput(void);

typedef enum {
    PAD_AXIS_X2 = 0,
    PAD_AXIS_Y2 = 1,
    PAD_AXIS_X  = 2,
    PAD_AXIS_Y  = 3
} PadAxis;

s32 GetAnalogStickAxis(s32 idx, s32 axis, s32 offset);
s32 StickToDPadButton(s32 idx, s32 x, s32 y);
void WaitForGamePadDetection(void);
s32 IsVibrationEnabled(s32 idx);
void SetStickDeadZone(s32 idx, s32 deadZoneLevel);
void SetSwapAnalogSticks(s32 idx, s32 enabled);
void RequestActuatorSetup(s32 idx);
s32 GetActuatorCountForGamePad(s32 idx);
s32 IsFirstGamePadConnected(void);
s32 getAnimGlobalState(void);
s32 setAnimGlobalState(s32 value);
void SetVibrationIntensity(s32 idx, s32 largeMotor, s32 smallMotor);
void SetVibrationEnabled(s32 idx, s32 enabled);
void setPadRepeatMask(s32 unused, s32 channel, s32 mask);
void initCdAnimSubsystem(void);
void SetMouseBounds(RECT *rect);
void SetMousePosition(s32 idx, s16 x, s16 y);

#endif /* INPUT_GAMEPAD_H */
