#include "mystd.h"

void my_memcpy(void *dest, void *src, int len)
{
    memcpy(dest, src, len);
}

void my_memset(void *dest, u8 value, int len)
{
    memset(dest, value, len);
}

/**
 * 内存比较 1 相等 0 不相等
 */
u8 my_mem_compare(u8 *src, u8 *dest, u32 len)
{
    while (*src++ == *dest++)
    {
        len--;
        if (len == 0)
            return 1;
    }
    return 0;
}

void gbk_to_utf8(u8 *gbk, u8 *utf8, size_t outlen)
{
    iconv_t cd = iconv_open("UTF-8", "GBK");
    if (cd == (iconv_t)-1)
        return;

    size_t inlen = strlen_gbk(gbk);
    u8 *pin = (u8 *)gbk;
    u8 *pout = utf8;

    memset(utf8, 0, outlen);

    iconv(cd, &pin, &inlen, &pout, &outlen);
    iconv_close(cd);
}

void gbk_to_unicode(u8 *gbk, u8 *unicode, size_t outlen)
{
    iconv_t cd = iconv_open("UTF-16", "GBK");
    if (cd == (iconv_t)-1)
        return;

    size_t inlen = strlen_gbk(gbk);
    u8 *pin = (u8 *)gbk;
    u8 *pout = unicode;

    memset(unicode, 0, outlen);

    iconv(cd, &pin, &inlen, &pout, &outlen);
    iconv_close(cd);
}

int strlen_utf16(u8 *utf16)
{
    int len = 0;
    while (utf16[len * 2] != 0 || utf16[len * 2 + 1] != 0)
        len++;
    return len;
}

int strlen_gbk(u8 *gbk)
{
    int len = 0;
    while (*gbk)
    {
        if (*gbk < 0x80)
        {
            // ASCII（1字节）
            gbk += 1;
        }
        else
        {
            // GBK 双字节
            if (*(gbk + 1) != 0)
                gbk += 2;
            else
                break; // 防止越界
        }
        len++;
    }
    return len;
}

int utf16_len(u8 *utf16)
{
    int len = 0;
    while (*utf16++ != 0)
        len++;
    return len;
}

int ucs2_to_utf8(u8 *in, int ilen, u8 *out, int olen)
{
    int length = 0;
    if (!out)
        return length;
    u8 *start = NULL;
    u8 *pout = out;
    for (start = in; start != NULL && start < in + ilen - 1; start += 2)
    {
        unsigned short ucs2_code = *(unsigned short *)start;
        if (0x0080 > ucs2_code)
        {
            /* 1 byte UTF-8 Character.*/
            if (length + 1 > olen)
                return -1;

            *pout = (u8)*start;
            length++;
            pout++;
        }
        else if (0x0800 > ucs2_code)
        {
            /*2 bytes UTF-8 Character.*/
            if (length + 2 > olen)
                return -1;
            *pout = ((u8)(ucs2_code >> 6)) | 0xc0;
            *(pout + 1) = ((u8)(ucs2_code & 0x003F)) | 0x80;
            length += 2;
            pout += 2;
        }
        else
        {
            /* 3 bytes UTF-8 Character .*/
            if (length + 3 > olen)
                return -1;

            *pout = ((u8)(ucs2_code >> 12)) | 0xE0;
            *(pout + 1) = ((u8)((ucs2_code & 0x0FC0) >> 6)) | 0x80;
            *(pout + 2) = ((u8)(ucs2_code & 0x003F)) | 0x80;
            length += 3;
            pout += 3;
        }
    }

    return length;
}
