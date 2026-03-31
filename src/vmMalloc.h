#include <stdio.h>
#include <assert.h>
#include "config.h"
#include "../Lib/sdl2-2.0.10/include/SDL2/SDL.h"

typedef struct VMBlock
{
    u32 addr; // VM地址
    u32 size;
    int used;
    struct VMBlock *next;
} VMBlock;

void InitVmMalloc();
void vm_free(u32 addr);
u32 vm_malloc(u32 size);
