#include "shell.h"
#include "arch/timer/rtc.h"

int check_command(char* cmd, const char* text);
int check_short_command(char* cmd, const char* text, int length);

void command_mode(char* input);
void calculator_mode(char* input);

uint64_t get_stack_pointer()
{
    asm volatile("mov %rsp, %rax");
}

uint64_t get_base_pointer()
{
    asm volatile("mov %rbp, %rax");
}

extern uint32_t tick;

void shell_command(char* input)
{
    switch(state)
    {
    case COMMAND_MODE:
        command_mode(input);
        break;
    case CALCULATOR_MODE:
        calculator_mode(input);
        break;
    }    

    kprintf("\n> ");
}

void command_mode(char* input)
{
    if(check_command(input, "help"))
    {
        kprintf("This is the HackOS help panel!\nCurrently implemented commands:\n");
        kprintf("\t- clear: Clears the console\n");
        kprintf("\t- page: Allocates 4096 bytes of page-aligned memory (kmalloc test)\n");
        kprintf("\t- calc: Enters calculator mode, which is currently not functioning\n");
        kprintf("\t- print <arg>: Prints <arg> to the console\n");
        kprintf("\t- print <var>: Prints <val> to the console, if it's a kernel variable\n");
        kprintf("\t- shutdown: Halts the CPU, causing the computer to shut down\n");
        kprintf("\t- restart: Restarts the computer (only works on Virtual Machines)\n");
        kprintf("\t- help: displays this menu");
    }
    else if(check_command(input, "page"))
    {
        uint64_t phys_addr;
        uint64_t page = kmalloc_int(0x1000, 1, &phys_addr);

        kprintf("Page: 0x%x, physical addr: 0x%x", page, phys_addr);

        state = COMMAND_MODE;
    }
    else if(check_command(input, "clear"))
    {
        clrscr();
        kprintf("Type 'shutdown' to halt the CPU");

        state = COMMAND_MODE;
    }
    else if(check_command(input, "calc"))
    {
        kprintf("Entering calculator mode!\nType 'quit' to return to command mode!");

        state = CALCULATOR_MODE;
    }
    else
    {
        if(check_short_command(input, "print", 5))
        {
            char* msg = &input[6];

            //Check if the user tries to get the value of an existing variable:
            if(check_short_command(msg, "time", 4))
            {
                struct tm time;
                GetTimeRTC(&time);
                kprintf("Time: %x:%x:%x", time.hour, time.minute, time.second);
            }
            else if(check_short_command(msg, "date", 4))
            {
                struct tm time;
                GetTimeRTC(&time);
                kprintf("Date: 20%x.%x.%x", time.year, time.month, time.day);
            }
            else if(check_short_command(msg, "tick", 4))
            {
                kprintf("Time elapsed since startup: %d", tick);
            }
            else if(check_short_command(msg, "stack", 5))
            {
                kprintf("Top of the stack: %x", get_stack_pointer());
            }
            else
            {
                kprintf(msg);
            }
            
        } else
        {
            kprintf("You said: %s", input);            
        }        

        state = COMMAND_MODE;
    }
}

void calculator_mode(char* input)
{
    //First, check for functions like sin, cos, tan
    if(check_command(input, "quit"))
    {
        kprintf("Exiting calculator mode.");
        state = COMMAND_MODE;
        return;
    }
    else if(check_short_command(input, "sin", 3))
    {
        kprintf("Sorry, the 'sin' function is not yet supported!");
    }
    else if(check_short_command(input, "cos", 3))
    {
        kprintf("Sorry, the 'cos' function is not yet supported!");
    }
    else if(check_short_command(input, "tan", 3))
    {
        kprintf("Sorry, the 'tan' function is not yet supported!");
    }
    else if(check_short_command(input, "cotan", 5))
    {
        kprintf("Sorry, the 'cotan' function is not yet supported!");
    } 
    else
    {
        int cnt;
        char** com = split(input, ' ', &cnt);

        kprintf("You said: \n");
        
        for(int i = 0; i < cnt; i++)
        {
            kprintf("%s ", com[i]);
        }

        //kprintf("Sorry, the calculator is not yet implemented!");    
    }

    state = CALCULATOR_MODE;
}

int check_command(char* cmd, const char* text)
{
    return strcmp(cmd, (char*)text) == 0;
}

int check_short_command(char* cmd, const char* text, int length)
{
    int i;
    for (i = 0; cmd[i] == text[i] && i < length; i++);

    if(i == length)
        return 1;
    else 
        return 0;
}