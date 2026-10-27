#ifndef MOUSE_H
#define MOUSE_H

#include "lib/stdint.h"

#define PS2LeftButton 0b00000001
#define PS2MiddleButton 0b00000100
#define PS2RightButton 0b00000010
#define PS2XSign 0b00010000
#define PS2YSign 0b00100000
#define PS2XOverflow 0b01000000
#define PS2YOverflow 0b10000000

void InitialiseMouse(uint32_t width, uint32_t height);
bool MouseEnabled();
uint8_t MouseDeviceId();

double GetMouseX();
double GetMouseY();
uint8_t GetMouseButton(uint8_t index);

struct MouseState
{
    int32_t x, y;
    uint32_t buttons;
    uint32_t key; // zero for mouse packets
    int32_t wheel;
};

// 0 empty, 1 packet, 2 overflow with the latest state.
unsigned ReadMouse(MouseState& state);
void DrawMouse();
void QueueKey(uint32_t key);
void DiscardQueuedKeys();

#endif
