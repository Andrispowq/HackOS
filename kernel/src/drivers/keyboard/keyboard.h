#ifndef KEYBOARD_H
#define KEYBOARD_H

#include "lib/stdint.h"

void InitKeyboard();
void ClearKeyboard(bool queued = true);
void DeliverKeyboard(uint32_t key);
void KeyboardOverflow();
enum class KeyboardRead { Empty, Character, Overflow };
KeyboardRead ReadKeyboard(char& ch);

#endif