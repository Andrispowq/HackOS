#ifndef MOUSE_H
#define MOUSE_H

#include "lib/stdint.h"

void InitialiseMouse(double mouseDivisor, uint32_t width, uint32_t height);

double GetMouseX();
double GetMouseY();
uint8_t GetMouseButton(uint8_t index);

#endif