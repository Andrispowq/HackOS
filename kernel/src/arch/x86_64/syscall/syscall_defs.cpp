#include "syscall_defs.h"

#include "lib/memory.h"
#include "lib/string.h"

#include "drivers/screen/screen.h"
#include "drivers/screen/window_manager.h"

extern WindowManager* KernelWindowManager;

char user_input[256];

void exit(int code)
{
    _Kill();
}

char* kread()
{
    strcpy(user_input, system_input);    
    memset(system_input, 0, 256);

    return user_input;
}

uint64_t create_window(uint32_t width, uint32_t height, uint32_t flags)
{
    uint32_t spawnX = 0;
    uint32_t spawnY = 0;
    if(flags & WINDOW_FLAG_SPAWN_AT_CENTER)
    {
        spawnX = (Display::SharedDisplay()->framebuffer.width - width) / 2;
        spawnY = (Display::SharedDisplay()->framebuffer.height - height) / 2;
    }

    Window* new_window = KernelWindowManager->CreateWindow(spawnX, spawnY, width, height);
    return new_window->GetWindowID();
}

void destroy_window(uint64_t ID)
{
    KernelWindowManager->DestroyWindowByID(ID);
}