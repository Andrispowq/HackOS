#ifndef MOUSE_H
#define MOUSE_H

#include "../libc/stdint.h"

void init_mouse(int movement_divider);

int8_t get_mouse_x();
int8_t get_mouse_y();
uint8_t get_mouse_button(uint8_t i);

#endif