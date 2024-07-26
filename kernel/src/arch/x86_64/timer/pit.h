#ifndef TIMER_H
#define TIMER_H

#include "lib/stdint.h"

void InitTimer(uint32_t freq);

void SleepFor(uint32_t milliseconds);
void SleepForSeconds(double seconds);

#endif