#include "lcd.h"

#define PIXEL565R(v) ((((u32)v >> 11) << 3) & 0xff) // 5位红色
#define PIXEL565G(v) ((((u32)v >> 5) << 2) & 0xff)  // 6位绿色
#define PIXEL565B(v) (((u32)v << 3) & 0xff)         // 5位蓝色

#define PIXEL888R(v) ((((u32)v >> 16)) & 0xff) // 5位红色
#define PIXEL888G(v) ((((u32)v >> 8)) & 0xff)  // 6位绿色
#define PIXEL888B(v) (v & 0xff)                // 5位蓝色

u8 *Lcd_Cache_Buffer;

void InitLcd()
{
    Lcd_Cache_Buffer = SDL_malloc(LCD_WIDTH * LCD_HEIGHT * PIXEL_PER_BYTE);
}

void UpdateLcd()
{
    SDL_Surface *sfc = SDL_GetWindowSurface(window);
    for (int i = 0; i < LCD_HEIGHT; i++)
    {
        for (int j = 0; j < LCD_WIDTH; j++)
        {
            u32 offset = j + i * LCD_WIDTH;
            u16 color = ((u16 *)Lcd_Cache_Buffer)[offset];
            *((u32 *)sfc->pixels + offset) = SDL_MapRGB(sfc->format, PIXEL565R(color), PIXEL565G(color), PIXEL565B(color));
        }
    }
    SDL_UpdateWindowSurface(window);
}