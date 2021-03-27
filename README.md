# HackOS
This is a hobby operating system I'm currently developing. I have big hopes that it will eventually turn out to some kind of hacking toolset, but of course this is just the plan, and may turn out completely differently.

I have a lot of plans, these are:
 - [x] Create a simple first-stage bootloader
 - [x] Jump into the kernel, written in C
 - [x] Create a simple VGA driver for outputting text in 80 x 25 text mode
 - [x] Create the Interrupt Descriptor Table, enable interrupts
 - [x] Create a very simple timer and keyboard driver
 - [x] Write a simple kmalloc() function for basic, 4K aligned and unaligned memory allocations for the kernel
 - [x] Implement a basic paging system, for virtual memory
 - [x] Write a simple memory management system to use by the kernel
 - [x] Write a second stage bootloader, so that the kernel can be bigger then 64 KiB, and it can be placed anywhere in the physical address space
 - [x] Refactor the code for better readability and expandibility, and using the datatypes provided by stdint.h
 - [x] Use the VESA BIOS extensions to switch to 1080p graphics mode, and load in a font file
 - [x] Switch to 64-bit mode in the second-stage bootloader, set up a basic identity paging in Assembly, and enable SSE2
 - [x] Implement a good paging system for 64-bit, which can be used by kernel and userspace as well
 - [x] Write a simple font renderer for the graphics renderer, and later make it scaleable
 - [ ] Add a simple Virtual File System
 - [ ] Add support for the FAT32 file system, or something simple
 - [ ] Implement multitasking
 - [ ] Start working on proper hardware detection
 - [ ] Write a VGA driver for low resolution (and maybe high resolution) graphics (256-bit, native hardware acceleration)
 - [ ] Start working on switching to userspace
 - [ ] Write a memory manager for userspace
 - [ ] Add support for userspace applications
 - [ ] Write a good, easy-to-use shell
 - [ ] Write a few basic apps for the OS, probably even a C compiler, assembler and a linker
 - [ ] Add support for mulithreading, along with synchronisation primitives like semaphores and locks
 - [ ] Add very simple networking
 - [ ] Create a development enviroment (IDE) for the OS, so we can develop apps for the OS in the OS
 - [ ] During the development, maybe consider dropping legacy BIOS support, and use UEFI instead, which has lots of benefits
