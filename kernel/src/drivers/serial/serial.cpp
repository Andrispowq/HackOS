#include "serial.h"
#include "hackos/serial.h"

static bool available;

void InitialiseSerial()
{
    available = HackosSerialInitialise();
}

void SerialCharacter(char ch)
{
    if (available)
    {
        available = HackosSerialPut(ch);
    }
}

void SerialText(const char* text)
{
    while (*text)
    {
        SerialCharacter(*text++);
    }
}
