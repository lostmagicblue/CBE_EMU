#include "cbeParser.h"

cbeInfo g_cbeInfo;

int sumCheck(int *memBuffer, int len)
{
    int result = 0;
    len /= 4;
    while (len-- > 0)
    {
        result += *(memBuffer++);
    }
    return result;
}

int parseCbeHeader(u8 *cbeFileBuffer, u32 size)
{

    int v = 0;
    int n = 0;
    u8 *filePtr;
    for (int i = 0; i < 0x98; i++) // 头部信息固定0x98字节
    {
        if (cbeFileBuffer[i] == 0xfe) // 直接忽略间隔符
            continue;
        // 按大端序读取4字节
        v = (cbeFileBuffer[i] << 24) | (cbeFileBuffer[i + 1] << 16) | (cbeFileBuffer[i + 2] << 8) | cbeFileBuffer[i + 3];
        i += 3;
        if (n == 0)
        {
            g_cbeInfo.entryOffset = v;
        }
        else if (n == 1)
            printf("未知：%08x\n", v);
        else if (n == 2)
            printf("未知：%08x\n", v);
        else if (n == 3)
            printf("未知：%08x\n", v);
        else if (n == 4)
            printf("未知：%08x\n", v);
        else if (n == 5)
            printf("未知：%08x\n", v);
        else if (n == 6)
            g_cbeInfo.codeLen = v;
        else if (n == 7)
            g_cbeInfo.codeSign = v;
        else if (n == 8)
            g_cbeInfo.BssDataLen = v;
        else if (n == 9)
            g_cbeInfo.BssDataSign = v;
        else if (n == 10)
            g_cbeInfo.RwDataLen = v;
        else if (n == 11)
            g_cbeInfo.RwDataSign = v;
        else
            printf("未知部分：%08x\n", v);
        n++;
    }

    filePtr = cbeFileBuffer + 0x98;
    int preCodeIntervalCount = 0;
    while (*filePtr == 0xfe)
    {
        preCodeIntervalCount++;
        filePtr++;
    }
    g_cbeInfo.preCodeIntervalCount = preCodeIntervalCount;
    g_cbeInfo.codeOffset = filePtr - cbeFileBuffer; // 代码段的实际起始位置
    filePtr += preCodeIntervalCount;                // 跳过间隔符
    filePtr += g_cbeInfo.codeLen;                   // 跳过代码段

    preCodeIntervalCount = 0;
    while (*filePtr == 0xfe)
    {
        preCodeIntervalCount++;
        filePtr++;
    }
    g_cbeInfo.BssDataOffset = filePtr - cbeFileBuffer; // BSS段的实际起始位置
    g_cbeInfo.preBssDataIntervalCount = preCodeIntervalCount;
    filePtr += preCodeIntervalCount; // 跳过间隔符
    filePtr += g_cbeInfo.BssDataLen; // 跳过BSS数据段

    preCodeIntervalCount = 0;
    while (*filePtr == 0xfe)
    {
        preCodeIntervalCount++;
        filePtr++;
    }
    g_cbeInfo.RwDataOffset = filePtr - cbeFileBuffer; // RW段的实际起始位置
    g_cbeInfo.preRwDataIntervalCount = preCodeIntervalCount;

    filePtr += preCodeIntervalCount; // 跳过间隔符
    filePtr += g_cbeInfo.RwDataLen;  // 跳过RW数据段

    preCodeIntervalCount = 0;
    while (*filePtr == 0xfe)
    {
        preCodeIntervalCount++;
        filePtr++;
    }
    g_cbeInfo.preRwDataIntervalCount = preCodeIntervalCount;
    g_cbeInfo.DF_DataPacakge_Size = (*filePtr++ << 24) | (*filePtr++ << 16) | (*filePtr++ << 8) | *filePtr++;

    preCodeIntervalCount = 0;
    while (*filePtr == 0xfe)
    {
        preCodeIntervalCount++;
        filePtr++;
    }
    g_cbeInfo.DF_Data_Pacakage_Offset = filePtr - cbeFileBuffer;

    printf("[cbeParser]entry %x\n", g_cbeInfo.entryOffset);

    printf("[cbeParser]codeOffset %x\n", g_cbeInfo.codeOffset);
    printf("[cbeParser]codeLen %x\n", g_cbeInfo.codeLen);
    printf("[cbeParser]codeSign %x sumCheck %x\n", g_cbeInfo.codeSign, sumCheck(cbeFileBuffer + g_cbeInfo.codeOffset, g_cbeInfo.codeLen));

    printf("[cbeParser]BssDataOffset %x\n", g_cbeInfo.BssDataOffset);
    printf("[cbeParser]bssDataLen %x\n", g_cbeInfo.BssDataLen);
    printf("[cbeParser]bssDataSign %x sumCheck %x\n", g_cbeInfo.BssDataSign, sumCheck(cbeFileBuffer + g_cbeInfo.BssDataOffset, g_cbeInfo.BssDataLen));

    printf("[cbeParser]RwDataOffset %x\n", g_cbeInfo.RwDataOffset);
    printf("[cbeParser]rwDataLen %x\n", g_cbeInfo.RwDataLen);
    printf("[cbeParser]rwDataSign %x sumCheck %x\n", g_cbeInfo.RwDataSign, sumCheck(cbeFileBuffer + g_cbeInfo.RwDataOffset, g_cbeInfo.RwDataLen));

    printf("[cbeParser]DF_Data_Pacakage_Offset %x\n", g_cbeInfo.DF_Data_Pacakage_Offset);
    printf("[cbeParser]DF_DataPacakge_Size %x\n", g_cbeInfo.DF_DataPacakge_Size);
    return 1;
}
