/*
 * rovari_sd.c - thin wrapper over FatFs (ff.c) hiding the SD plumbing.
 * One static file object keeps the user API tiny (one open file at a time),
 * which fits the CH32V003's 2K RAM. Full FatFs underneath via diskio.c.
 */
#include "rovari_sd.h"
#include "ff.h"

static FATFS  s_fs;            /* file system object (mounted once)   */
static FIL    s_fp;            /* the single open file                */
static uint8_t s_mounted = 0;  /* 1 after a good sd_begin()           */
static uint8_t s_open = 0;     /* 1 while a file is open              */

uint8_t sd_begin(void)
{
    if (s_mounted) return 1;
    if (f_mount(&s_fs, "0:", 1) != FR_OK) {
        s_mounted = 0;
        return 0;
    }
    s_mounted = 1;
    return 1;
}

static uint8_t open_mode(const char *name, BYTE mode, int seek_end)
{
    if (!s_mounted) return 0;
    if (s_open) { f_close(&s_fp); s_open = 0; }
    if (f_open(&s_fp, name, mode) != FR_OK) return 0;
    s_open = 1;
    if (seek_end) {
        if (f_lseek(&s_fp, f_size(&s_fp)) != FR_OK) { f_close(&s_fp); s_open = 0; return 0; }
    }
    return 1;
}

uint8_t sd_open(const char *name)
{
    /* create or overwrite */
    return open_mode(name, FA_CREATE_ALWAYS | FA_WRITE, 0);
}

uint8_t sd_append(const char *name)
{
    /* open (creating if needed), then seek to end so writes append */
    return open_mode(name, FA_OPEN_ALWAYS | FA_WRITE, 1);
}

uint32_t sd_write(const void *data, uint32_t len)
{
    UINT bw = 0;
    if (!s_open) return 0;
    if (f_write(&s_fp, data, (UINT)len, &bw) != FR_OK) return 0;
    return (uint32_t)bw;
}

uint32_t sd_print(const char *s)
{
    uint32_t n = 0;
    const char *p = s;
    while (*p) { p++; n++; }
    return sd_write(s, n);
}

uint32_t sd_print_int(int32_t value)
{
    char tmp[12];
    int k = 0;
    uint32_t uv;
    char out[12];
    int j = 0;

    if (value < 0) { out[j++] = '-'; uv = (uint32_t)(-(value + 1)) + 1u; }
    else uv = (uint32_t)value;

    if (uv == 0) tmp[k++] = '0';
    while (uv) { tmp[k++] = (char)('0' + (uv % 10)); uv /= 10; }
    while (k--) out[j++] = tmp[k];

    return sd_write(out, (uint32_t)j);
}

void sd_close(void)
{
    if (s_open) { f_close(&s_fp); s_open = 0; }
}