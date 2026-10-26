#ifndef KEYBOARD_H
#define KEYBOARD_H

#include "lib/stdint.h"

void InitKeyboard();
enum class KeyboardRead { Empty, Character, Overflow };
KeyboardRead ReadKeyboard(char& ch);

#endif