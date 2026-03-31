#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>

typedef struct vm_img_result
{
    int pixelsPtr; // RGB565 数据指针
    short width;
    short height;
    char need_free;
} vm_img_result;

typedef struct
{
    char *pixels;
    uint16_t width;
    uint16_t height;
    uint8_t allocated; /* 1=函数内部分配 */
    int mallocSize;
} GifOut;

/* 全局状态（模拟反汇编中的全局变量） */
static uint8_t *g_LKV2_GIFArray = NULL;   /* 流当前位置指针（用于子块读取/位流读取） */
static uint16_t *g_LKV2_Palette = NULL;   /* 16-bit palette，已字节交换 */
static int g_LKV2_TransparentIndex = 256; /* 透明索引 */

/* 字典与临时缓冲 */
static int32_t *g_LKV2_Prefix = NULL;
static uint8_t *g_LKV2_Suffix = NULL;
static uint8_t *g_LKV2_OutCode = NULL;

/* LZW 解码状态（cur code size / max code 等） */
static int g_LKV2_CurCodeSize = 0;
static int g_LKV2_CurMaxCode = 0;

/* bit-buffer 状态（用于 robust GAME_ReadCode） */
static uint32_t bitBuffer = 0;
static int bitsInBuffer = 0;
static int blkRemain = 0; /* 当前子块剩余字节 */

/* 初始化 palette 内存（与反汇编 gif_lkv2_Init 一致） */
static int gif_lkv2_Init(void)
{
    if (!g_LKV2_Palette)
    {
        g_LKV2_Palette = (uint16_t *)SDL_malloc(512); /* 256 * 2 bytes */
        if (!g_LKV2_Palette)
            return 0;
        memset(g_LKV2_Palette, 0, 512);
    }
    return 1;
}

/* 更稳健的 GAME_ReadCode：LSB-first bit buffer，处理 sub-blocks
   返回 -1 表示流结束或错误（与反汇编的 -1 行为兼容）。
*/
static int GAME_ReadCode_robust(void)
{
    if (!g_LKV2_GIFArray)
        return -1;

    /* 确保 bitBuffer 中有至少 cur code size 个比特 */
    while (bitsInBuffer < g_LKV2_CurCodeSize)
    {
        if (blkRemain == 0)
        {
            /* 读取下一个子块长度字节 */
            uint8_t sz = *g_LKV2_GIFArray++;
            blkRemain = (int)sz;
            if (blkRemain == 0)
            {
                return -1; /* 数据块结束 */
            }
        }
        /* 读取一个字节填充到 bitBuffer（LSB-first） */
        uint8_t b = *g_LKV2_GIFArray++;
        blkRemain--;
        bitBuffer |= ((uint32_t)b) << bitsInBuffer;
        bitsInBuffer += 8;
    }

    /* 提取最低的 g_LKV2_CurCodeSize 个比特作为 code */
    uint32_t mask = (g_LKV2_CurCodeSize >= 32) ? 0xFFFFFFFFu : ((1u << g_LKV2_CurCodeSize) - 1u);
    int code = (int)(bitBuffer & mask);
    bitBuffer >>= g_LKV2_CurCodeSize;
    bitsInBuffer -= g_LKV2_CurCodeSize;
    return code;
}

/* Helper: safe free and nullify */
static void safe_free(void **p)
{
    if (p && *p)
    {
        SDL_free(*p);
        *p = NULL;
    }
}

/* 主函数：gifDecodeExt，接口与之前说明一致 */
int gifDecodeExt(const uint8_t *a1, GifOut *out, int a3)
{
    if (!a1 || !out)
        return 0;
    if (!gif_lkv2_Init())
        return 0;

    /* 清理先前状态（保留 g_LKV2_Palette） */
    safe_free((void **)&g_LKV2_Prefix);
    safe_free((void **)&g_LKV2_Suffix);
    safe_free((void **)&g_LKV2_OutCode);
    g_LKV2_GIFArray = NULL;
    bitBuffer = 0;
    bitsInBuffer = 0;
    blkRemain = 0;
    g_LKV2_TransparentIndex = 256;

    /* 反汇编计算 v34 = a1[3] + (*a1 << 24) + (a1[1] << 16) + (a1[2] << 8) */
    uint32_t v34 = ((uint32_t)a1[0] << 24) | ((uint32_t)a1[1] << 16) | ((uint32_t)a1[2] << 8) | (uint32_t)a1[3];
    uint8_t v4 = a1[4];
    const uint8_t *v5 = a1 + 5;
    uint8_t v6 = 0;

    /* 如果最高位没有被设置，返回 0（与反汇编开头一致） */
    if ((v4 & 0x80) == 0)
        return 0;

    /* v7 = 1 << ((v4 & 7) + 1) */
    int v7 = 1 << (((int)v4 & 7) + 1);
    int v38 = v7 - 1; /* palette 索引掩码 */
    /* 反汇编中 LKV2_GIFArray 起始为 a1 + 5 或 a1 + 7，模拟相同移动 */
    g_LKV2_GIFArray = (uint8_t *)(v5 + 2);
    const uint8_t *src = v5 + 2;

    /* 反汇编含有一个循环：当 v5[1] == 0 时会重复处理 palette 等。
       这里我们按照相同语义循环，直到遇到 image separator (0x2C) */
    while (1)
    {
        /* disasm 判断 v5[1] == 0 才进入 loop；如果不为 0 则跳出并返回 0 */
        if ((v5[1]) != 0)
            break;

        /* 从 src 复制 palette：2 * v7 字节，随后对每个 16-bit 做字节交换（与反汇编一致） */
        if (!g_LKV2_Palette)
            return 0;
        memcpy((uint8_t *)g_LKV2_Palette, src, 2 * v7);

        /* advance pointer like disasm: point after palette */
        g_LKV2_GIFArray = (uint8_t *)((char *)g_LKV2_GIFArray + 2 * v7);
        /* 字节交换每个 16-bit 词（反汇编中为 *(_WORD *) = HIBYTE(w) | (w << 8)） */
        for (int i = 0; i < v7; ++i)
        {
            uint8_t lo = ((uint8_t *)g_LKV2_Palette)[2 * i];
            uint8_t hi = ((uint8_t *)g_LKV2_Palette)[2 * i + 1];
            uint16_t w = (uint16_t)((hi) | (lo << 8));
            g_LKV2_Palette[i] = w;
        }

        /* advance 3 bytes as in disasm */
        g_LKV2_GIFArray = (uint8_t *)((char *)g_LKV2_GIFArray + 3);

        /* 反汇编根据某一位条件选择透明索引或 256；我们按位检查第 3 字节的 0x80 */
        uint8_t *p3 = g_LKV2_GIFArray - 3;
        if (((uint8_t)p3[3] & 0x80) != 0)
        {
            g_LKV2_GIFArray = (uint8_t *)((char *)p3 + 6);
            g_LKV2_TransparentIndex = (int)((uint8_t *)p3)[6];
        }
        else
        {
            g_LKV2_GIFArray = (uint8_t *)((char *)p3 + 6);
            g_LKV2_TransparentIndex = 256;
        }

        /* 读取下一个字节（srcByte），然后 advance 3 */
        uint8_t srcByte = g_LKV2_GIFArray[2];
        uint8_t *v14 = g_LKV2_GIFArray + 3;
        g_LKV2_GIFArray = g_LKV2_GIFArray + 3;

        /* 如果遇到 image separator 0x2C（逗号），则读取 image descriptor 与 LZW 数据 */
        if (srcByte == 0x2C)
        {
            uint8_t *v15 = v14 + 4;
            uint8_t x0 = *v15++;
            uint8_t x1 = *v15++;
            int width = (x1 << 8) | x0;
            uint8_t y0 = *v15++;
            uint8_t y1 = *v15++;
            int height = (y1 << 8) | y0;

            /* move pointer similar to disasm */
            g_LKV2_GIFArray = v15 + 1;

            uint8_t flagByte = v15[1];
            if ((flagByte >> 6) != 0)
            {
                /* 如果高两位非零，按反汇编直接失败 */
                return 0;
            }

            /* initial LZW code size byte */
            uint8_t initialCodeSize = *g_LKV2_GIFArray++;
            g_LKV2_CurCodeSize = (int)initialCodeSize + 1;
            g_LKV2_CurMaxCode = 1 << (initialCodeSize + 1);
            int clearCode = 1 << initialCodeSize;
            int nextCode = clearCode + 2;

            /* 计算每行填充使其按 4 字节对齐（反汇编使用复杂的公式，这里等价实现） */
            int pad = (4 - (width % 4)) % 4;
            int rowStride = 2 * (width + pad);
            size_t requiredSize = (size_t)rowStride * (size_t)height;
            if (width <= 0 || height <= 0)
                return 0;

            /* 决定字典缓冲大小：以 v34 为基础，但至少 4096，并对极大值做上限保护 */
            uint32_t dictSize = v34;
            if (dictSize < 4096)
                dictSize = 4096;
            if (dictSize > 65536)
                dictSize = 65536; /* 上限保护，避免恶意或错误的巨大分配 */

            /* 分配输出像素缓冲（每像素 2 字节） */
            uint8_t *pixelBuffer = NULL;
            if (a3 == 1)
            {
                pixelBuffer = (uint8_t *)SDL_malloc(requiredSize);
                if (!pixelBuffer)
                    return 0;
                memset(pixelBuffer, 0, requiredSize);
                out->pixels = (char *)pixelBuffer;
                out->allocated = 1;
                out->mallocSize = (int)requiredSize;
            }
            else
            {
                if (!out->pixels)
                    return 0;
                if (out->mallocSize < (int)requiredSize)
                    return 0;
                pixelBuffer = (uint8_t *)out->pixels;
                out->allocated = 0;
            }

            /* 为字典与临时数组分配内存（大小为 dictSize） */
            g_LKV2_Prefix = (int32_t *)SDL_malloc(sizeof(int32_t) * dictSize);
            g_LKV2_Suffix = (uint8_t *)SDL_malloc(sizeof(uint8_t) * dictSize);
            g_LKV2_OutCode = (uint8_t *)SDL_malloc(sizeof(uint8_t) * dictSize);
            if (!g_LKV2_Prefix || !g_LKV2_Suffix || !g_LKV2_OutCode)
            {
                safe_free((void **)&g_LKV2_Prefix);
                safe_free((void **)&g_LKV2_Suffix);
                safe_free((void **)&g_LKV2_OutCode);
                if (a3 == 1)
                {
                    SDL_free(pixelBuffer);
                    out->pixels = NULL;
                    out->allocated = 0;
                }
                return 0;
            }

            /* 初始化 bit-buffer 状态以开始解码 LZW 数据子块 */
            bitBuffer = 0;
            bitsInBuffer = 0;
            blkRemain = 0;

            /* 初始化基本字典项（0..clearCode-1 为字面索引，后续用 Prefix/Suffix 填充） */
            /* 反汇编并未显式初始化 Prefix/Suffix 的所有项，写入时按 nextCode 控制。 */

            /* LZW 解码循环 */
            int remainingPixels = width * height;
            int x = 0, y = 0;
            int outPos = 0;
            int prevCode = -1;
            int lastCode = 0;
            int outLen = 0;

            /* We will use GAME_ReadCode_robust to get codes */
            while (remainingPixels > 0)
            {
                int code = GAME_ReadCode_robust();
                if (code < 0)
                    break;

                if (code == clearCode)
                {
                    /* clear code：重置字典状态为初值 */
                    g_LKV2_CurCodeSize = (int)initialCodeSize + 1;
                    g_LKV2_CurMaxCode = 1 << g_LKV2_CurCodeSize;
                    nextCode = clearCode + 2;
                    prevCode = -1;

                    /* read first code after clear and output that literal */
                    int first = GAME_ReadCode_robust();
                    if (first < 0)
                        break;
                    lastCode = first & 0xFF;
                    /* 将 palette[lastCode] 写入像素 */
                    uint16_t pal = g_LKV2_Palette[(uint8_t)lastCode];
                    size_t pos = (size_t)y * (size_t)rowStride + (size_t)x * 2;
                    if (pos + 1 < requiredSize)
                    {
                        pixelBuffer[pos] = (uint8_t)(pal & 0xFF);
                        pixelBuffer[pos + 1] = (uint8_t)((pal >> 8) & 0xFF);
                    }
                    x++;
                    remainingPixels--;
                    if (x >= width)
                    {
                        x = 0;
                        y++;
                    }
                    prevCode = first;
                    continue;
                }
                else
                {
                    /* 常规 code 处理 */
                    int cur = code;
                    outLen = 0;

                    if (cur >= nextCode)
                    {
                        /* 特殊情况：cur 指向尚未加入字典的条目，按规范应使用 prevCode 的输出 + first char */
                        /* 这里需校验 prevCode 有效 */
                        if (prevCode == -1)
                        {
                            /* 无效流 */
                            break;
                        }
                        /* 先把 prevCode 的最后一个字符放入 out */
                        /* 通过展开 prevCode 的后缀链得到最后字符 */
                        int tmp = prevCode;
                        if ((uint32_t)tmp >= dictSize)
                        { /* 越界保护 */
                            goto DECODE_CLEANUP_FAIL;
                        }
                        uint8_t ch = 0;
                        if (tmp <= v38)
                        { /* 基本字面值 */
                            ch = (uint8_t)tmp;
                        }
                        else
                        {
                            /* 通过 suffix 链获得最后一个字节 */
                            while (tmp > v38)
                            {
                                if ((uint32_t)tmp >= dictSize)
                                    goto DECODE_CLEANUP_FAIL;
                                ch = g_LKV2_Suffix[tmp];
                                tmp = (int)g_LKV2_Prefix[tmp];
                            }
                            ch = (uint8_t)tmp;
                        }
                        /* 将该字符放入 out buffer */
                        if (outLen < (int)dictSize)
                            g_LKV2_OutCode[outLen++] = ch;
                        /* 并将 cur 设为 prevCode 以继续展开 */
                        cur = prevCode;
                    }

                    /* 通过 prefix/suffix 链从 cur 展开到最前面（直到 <= v38）并把字节逐个放入 OutCode */
                    while (cur > v38)
                    {
                        if ((uint32_t)cur >= dictSize)
                            goto DECODE_CLEANUP_FAIL;
                        if (outLen >= (int)dictSize)
                            goto DECODE_CLEANUP_FAIL;
                        g_LKV2_OutCode[outLen++] = g_LKV2_Suffix[cur];
                        cur = (int)g_LKV2_Prefix[cur];
                    }
                    /* 最后加入字面值 cur */
                    if (outLen >= (int)dictSize)
                        goto DECODE_CLEANUP_FAIL;
                    g_LKV2_OutCode[outLen++] = (uint8_t)cur;

                    /* 现在按逆序输出 OutCode 中的字节（从 outLen-1 到 0）到像素缓冲 */
                    for (int idx = outLen - 1; idx >= 0; --idx)
                    {
                        uint8_t colorIndex = g_LKV2_OutCode[idx];
                        if ((uint32_t)colorIndex > (uint32_t)v38 && colorIndex >= dictSize)
                        {
                            goto DECODE_CLEANUP_FAIL;
                        }
                        uint16_t pal = g_LKV2_Palette[colorIndex];
                        size_t pos = (size_t)y * (size_t)rowStride + (size_t)x * 2;
                        if (pos + 1 < requiredSize)
                        {
                            pixelBuffer[pos] = (uint8_t)(pal & 0xFF);
                            pixelBuffer[pos + 1] = (uint8_t)((pal >> 8) & 0xFF);
                        }
                        x++;
                        remainingPixels--;
                        if (x >= width)
                        {
                            x = 0;
                            y++;
                        }
                        if (remainingPixels <= 0)
                            break;
                    }

                    /* 在完成输出后，用 prevCode (lastCode) 和当前第一字符填充新的字典项（如果还有空间） */
                    if (prevCode != -1 && nextCode < (int)dictSize)
                    {
                        /* first char of current expansion = last element g_LKV2_OutCode[outLen-1] */
                        uint8_t firstChar = g_LKV2_OutCode[outLen - 1];
                        g_LKV2_Prefix[nextCode] = prevCode;
                        g_LKV2_Suffix[nextCode] = firstChar;
                        nextCode++;
                    }

                    /* 更新 prevCode */
                    prevCode = code;

                    /* 当达到当前 cur max 时扩大 code size（最多到 12） */
                    if (nextCode >= g_LKV2_CurMaxCode && g_LKV2_CurCodeSize < 12)
                    {
                        g_LKV2_CurMaxCode <<= 1;
                        g_LKV2_CurCodeSize++;
                    }
                } /* end else (non-clear code) */
            } /* end while decoding */

            /* 解码结束，释放字典缓存（反汇编在函数末尾释放） */
            safe_free((void **)&g_LKV2_OutCode);
            safe_free((void **)&g_LKV2_Suffix);
            safe_free((void **)&g_LKV2_Prefix);

            /* 填充返回结构 */
            out->width = (uint16_t)width;
            out->height = (uint16_t)height;
            if (a3 == 1)
            {
                out->mallocSize = (int)requiredSize;
            }
            return 1;

        DECODE_CLEANUP_FAIL:
            /* 在任意错误处清理并返回失败 */
            safe_free((void **)&g_LKV2_OutCode);
            safe_free((void **)&g_LKV2_Suffix);
            safe_free((void **)&g_LKV2_Prefix);
            if (a3 == 1)
            {
                if (out->pixels)
                {
                    SDL_free(out->pixels);
                    out->pixels = NULL;
                    out->allocated = 0;
                    out->mallocSize = 0;
                }
            }
            return 0;
        } /* end if srcByte == 0x2C */

        /* 如果不是 image separator，继续循环（保持与反汇编一致的遍历） */
    } /* end while palette loop */

    return 0;
}