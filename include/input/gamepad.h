#ifndef INPUT_GAMEPAD_H
#define INPUT_GAMEPAD_H

#include "common.h"
#include "psxsdk/libgpu.h"

#define CONTROLLER_PORT_1 0x00
#define CONTROLLER_PORT_2 0x10
#define CONTROLLER_PORT_1_MULTITAP_A = 0x00
#define CONTROLLER_PORT_1_MULTITAP_B = 0x01
#define CONTROLLER_PORT_1_MULTITAP_C = 0x02
#define CONTROLLER_PORT_1_MULTITAP_D = 0x03
#define CONTROLLER_PORT_2_MULTITAP_A = 0x10
#define CONTROLLER_PORT_2_MULTITAP_B = 0x11
#define CONTROLLER_PORT_2_MULTITAP_C = 0x12
#define CONTROLLER_PORT_2_MULTITAP_D = 0x13

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

#define PAD_PORT_SWAP_ANALOG_STICKS 0x80

void func_800275D4(void);

typedef enum {
    PAD_AXIS_X2 = 0,
    PAD_AXIS_Y2 = 1,
    PAD_AXIS_X  = 2,
    PAD_AXIS_Y  = 3
} PadAxis;

s32 func_80027DB4(s32 pad, s32 axis, s32 c);
s32 func_80027CF8(s32 a, s32 b, s32 c);
void WaitForGamePadDetection(void);
s32 getPadVibration(s32 idx);
void setPadDeadZone(s32 a0, s32 a1);
void setPadAnalogFlag(s32 a0, s32 a1);
void RequestActuatorSetup(s32 idx);
s32 GetActuatorCountForGamePad(s32 idx);
s32 getAnimGlobalState(void);
s32 setAnimGlobalState(s32 value);
void setPadMotors(s32 idx, s32 motor1, s32 motor0);
void setPadVibration(s32 idx, s32 val);
void setPadRepeatMask(s32 unused, s32 channel, s32 mask);
void initCdAnimSubsystem(void);
void SetMouseBounds(RECT *rect);
s16 GetMousePosition(s32 idx, s32 axis);
void SetMousePosition(s32 idx, s16 x, s16 y);

#endif /* INPUT_GAMEPAD_H */
