#include <odroid_system.h>
#include "rg_utils.h"
#include "gw_malloc.h"

#include <stdlib.h>
#include <string.h>

char *rg_strtolower(char *str)
{
    if (!str)
        return NULL;

    for (char *c = str; *c; c++)
        if (*c >= 'A' && *c <= 'Z')
            *c += 32;

    return str;
}

char *rg_strtoupper(char *str)
{
    if (!str)
        return NULL;

    for (char *c = str; *c; c++)
        if (*c >= 'a' && *c <= 'z')
            *c -= 32;

    return str;
}

const char *rg_dirname(const char *path)
{
    static char buffer[RG_PATH_MAX];
    const char *basename = strrchr(path, '/');
    ptrdiff_t length;

    if (!path || !basename)
        return ".";

    if (path[0] == '/' && path[1] == 0)
        return "/";

    length = basename - path;
    if (length >= (ptrdiff_t)sizeof(buffer))
        length = (ptrdiff_t)sizeof(buffer) - 1;

    memcpy(buffer, path, (size_t)length);
    buffer[length] = 0;

    return buffer;
}

const char *rg_basename(const char *path)
{
    if (!path)
        return ".";

    const char *name = strrchr(path, '/');
    return name ? name + 1 : path;
}

const char *rg_extension(const char *path)
{
    if (!path)
        return NULL;

    const char *ptr = rg_basename(path);
    const char *ext = strrchr(ptr, '.');
    if (!ext)
        return ptr + strlen(ptr);
    return ext + 1;
}

const char *rg_relpath(const char *path)
{
    if (!path)
        return NULL;

    if (strncmp(path, RG_STORAGE_ROOT, strlen(RG_STORAGE_ROOT)) == 0)
    {
        const char *relpath = path + strlen(RG_STORAGE_ROOT);
        if (relpath[0] == '/' || relpath[0] == 0)
            path = relpath;
    }
    return path;
}

uint32_t rg_crc32(uint32_t crc, const uint8_t *buf, size_t len)
{
#ifdef ESP_PLATFORM
    // This is part of the ROM but finding the correct header is annoying as it differs per SOC...
    extern uint32_t crc32_le(uint32_t crc, const uint8_t *buf, uint32_t len);
    return crc32_le(crc, buf, len);
#else
    // Derived from: http://www.hackersdelight.org/hdcodetxt/crc.c.txt
    crc = ~crc;
    for (size_t i = 0; i < len; ++i)
    {
        crc = crc ^ buf[i];
        for (int j = 7; j >= 0; j--) // Do eight times.
        {
            uint32_t mask = -(crc & 1);
            crc = (crc >> 1) ^ (0xEDB88320 & mask);
        }
    }
    return ~crc;
#endif
}

/**
 * This function is the SuperFastHash from:
 *  http://www.azillionmonkeys.com/qed/hash.html
*/
IRAM_ATTR uint32_t rg_hash(const char *data, size_t len)
{
    #define get16bits(d) (*((const uint16_t *)(d)))

    if (len <= 0 || data == NULL)
        return 0;

    uint32_t hash = len, tmp;
    int rem = len & 3;
    len >>= 2;

    /* Main loop */
    for (; len > 0; len--)
    {
        hash += get16bits(data);
        tmp = (get16bits(data + 2) << 11) ^ hash;
        hash = (hash << 16) ^ tmp;
        data += 2 * sizeof(uint16_t);
        hash += hash >> 11;
    }

    /* Handle end cases */
    switch (rem)
    {
    case 3:
        hash += get16bits(data);
        hash ^= hash << 16;
        hash ^= ((signed char)data[sizeof(uint16_t)]) << 18;
        hash += hash >> 11;
        break;
    case 2:
        hash += get16bits(data);
        hash ^= hash << 11;
        hash += hash >> 17;
        break;
    case 1:
        hash += (signed char)*data;
        hash ^= hash << 10;
        hash += hash >> 1;
    }

    /* Force "avalanching" of final 127 bits */
    hash ^= hash << 3;
    hash += hash >> 5;
    hash ^= hash << 4;
    hash += hash >> 17;
    hash ^= hash << 25;
    hash += hash >> 6;

    #undef get16bits
    return hash;
}

const char *const_string(const char *str)
{
    char *data = ram_malloc(strlen(str)+1);
    strcpy(data,str);
    return (const char *)data;
}
