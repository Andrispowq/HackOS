#include "keyboard.h"

#include "arch/x86_64/ports.h"
#include "arch/x86_64/interrupts/idt.h"

#include "lib/string.h"
#include "lib/memory.h"
#include "lib/function.h"
#include "lib/stdio.h"

#include "kernel.h"

#define HUNGARIAN_LAYOUT 0

#define BACKSPACE 0x0E
#define ENTER 0x1C

#define SCANCODE_MAX 57

#define RELEASED 0x80
#define KEY_BUFFER_SIZE 255

static char key_buffer[KEY_BUFFER_SIZE + 1];

//Bit 0 -> shift, 1 -> control, 2 -> super, 3 -> alt
int control_keys = 0;

#if HUNGARIAN_LAYOUT != 0
//Hungarian layout
const char *sc_name[] = { "ERROR", "Esc", "1", "2", "3", "4", "5", "6", 
    "7", "8", "9", "Ö", "Ü", "Ó", "Backspace", "Tab", "Q", "W", "E", 
        "R", "T", "Z", "U", "I", "O", "P", "Ő", "Ú", "Enter", "Lctrl", 
        "A", "S", "D", "F", "G", "H", "J", "K", "L", "É", "Á", "0", 
        "LShift", "Ű", "Y", "X", "C", "V", "B", "N", "M", ",", ".", 
        "/", "RShift", "Keypad *", "LAlt", "Spacebar", "Caps lock",
        "F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10", "Numlock"};

const char sc_ascii[] = { '?', '?', '1', '2', '3', '4', '5', '6',     
    '7', '8', '9', 'Ö', 'Ü', 'Ó', '?', '?', 'Q', 'W', 'E', 'R', 'T', 'Z', 
        'U', 'I', 'O', 'P', 'Ő', 'Ú', '?', '?', 'A', 'S', 'D', 'F', 'G', 
        'H', 'J', 'K', 'L', 'É', 'Á', '0', '?', 'Ű', 'Y', 'X', 'C', 'V', 
        'B', 'N', 'M', ',', '.', '/', '?', '*', '?', ' ', '?',
        '?', '?', '?', '?', '?', '?', '?', '?', '?', '?', '?'};
#else
//United States layout
const char *sc_name[] = { "ERROR", "Esc", 
        "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", 
        "-", "=", "Backspace", "Tab", 
        "Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P", "[", "]", "Enter", "Lctrl", 
        "A", "S", "D", "F", "G", "H", "J", "K", "L", ";", "'", "`", "LShift", "\\",
        "Z", "X", "C", "V", "B", "N", "M", ",", ".", "/", 
        "RShift", "Keypad *", "LAlt", "Spacebar", 
        "F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10", 
        "Numlock", "Scroll_lock", "Home", "Up", "Page_Up", "-",
        "Left", "Empty", "Right", "+", "End", "Down", "Page_Down", "Insert", "Delete"};

const char sc_ascii[] = { '?', '?', 
        '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', 
        '-', '=', '?', '?', 
        'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '[', ']', '?', '?', 
        'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ';', '\'', '`', '?', '\\', 
        'Z', 'X', 'C', 'V', 'B', 'N', 'M', ',', '.', '/', 
        '?', '?', '?', ' ', 
        '?', '?', '?', '?', '?', '?', '?', '?', '?', '?', 
        '?', '?', '?', '?', '?', '-',
        '?', '?', '?', '+', '?', '?', '?', '?', '?'};

#endif

int get_index_of_control_key(int scancode);

static void keyboard_callback(Registers* regs) 
{
    //the PIC leaves us the scancode in port 0x60 
    uint8_t scancode = __inb(0x60);

    //if (scancode > SCANCODE_MAX) 
    //    return;

    if (scancode == BACKSPACE) 
    {
        if(strlen(key_buffer) == 0)
            return;

        backspace(key_buffer);
        kprintf_backspace();
    } 
    else if (scancode == ENTER) 
    {
        kprintf("\n");
        user_input(key_buffer);
        memset(key_buffer, 0, KEY_BUFFER_SIZE + 1);
    } 
    else 
    {
        //If full, flush the buffer
        if(strlen(key_buffer) == 255)
        {
            kprintf("\n");
            user_input(key_buffer);
            key_buffer[0] = '\0';
        }

        //If we hit a control key, we should act on it
        char letter = sc_ascii[(int)scancode];
        int idx = (int)(scancode & RELEASED ? scancode - RELEASED : scancode);

        if(sc_ascii[idx] == '?')
        {
            int bitshift = get_index_of_control_key(idx);
            if(bitshift < 0)
                return;

            if(scancode & RELEASED)
            {
                control_keys &= ~(0x1 << bitshift);
            } else
            {
                control_keys |= (0x1 << bitshift);
            }
        }
        else if(scancode <= SCANCODE_MAX)
        {
            if(!(control_keys & 0x1) && (letter >= 'A' && letter <= 'Z'))
                letter += ('a' - 'A'); //If shift is not down, we display a lowercase letter

            if(control_keys & (0x1 << 3))
            {
                letter = '~';
            }

            /* Remember that printf only accepts char[] */
            char str[2] = { letter, '\0' };
            append(key_buffer, letter);
            kprintf(str);
        }      

    }
    
    UNUSED(regs);
}

int get_index_of_control_key(int scancode)
{
    char* scanname = (char*)sc_name[(int)scancode];
    if(strcmp(scanname, "Lctrl") == 0)
    {
        return 1;
    } 
    else if(strcmp(scanname, "LShift") == 0)
    {
        return 0;
    }
    else if(strcmp(scanname, "RShift") == 0)
    {
        return 0;
    }
    else if(strcmp(scanname, "LAlt") == 0)
    {
        return 3;
    } 
    else
    {
        return -1;
    }
}

void InitKeyboard() 
{
    RegisterInterruptHandler(IRQ1, keyboard_callback);
}