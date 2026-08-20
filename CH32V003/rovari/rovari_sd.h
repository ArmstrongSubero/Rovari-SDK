/*
 * rovari_sd.h - simple SD-card logging for Rovari (CH32V003).
 *
 * Hides FatFs behind four calls. Full FatFs underneath, so files are created,
 * grown and appended normally - no pre-formatting of file slots needed.
 *
 * Wiring: CS=PC4  SCK=PC5  MOSI=PC6  MISO=PC7.  Board MUST run at 3.3V.
 *
 *   if (sd_begin()) {
 *       sd_open("LOG.TXT");          // create/overwrite, or sd_append("LOG.TXT")
 *       sd_print("hello\r\n");
 *       sd_print_int(count);
 *       sd_close();
 *   }
 */
#ifndef ROVARI_SD_H
#define ROVARI_SD_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Mount the card. Returns 1 on success, 0 on failure. Call once at startup. */
uint8_t sd_begin(void);

/* Open a file for writing, truncating any existing contents. 1=ok 0=fail.
 * Name must be 8.3 and <= 8 chars before the dot (e.g. "LOG.TXT"). */
uint8_t sd_open(const char *name);

/* Open a file for writing, seeking to the end so writes append. 1=ok 0=fail. */
uint8_t sd_append(const char *name);

/* Write a null-terminated string to the open file. Returns bytes written. */
uint32_t sd_print(const char *s);

/* Write a signed integer as decimal text (no float). Returns bytes written. */
uint32_t sd_print_int(int32_t value);

/* Write raw bytes. Returns bytes written. */
uint32_t sd_write(const void *data, uint32_t len);

/* Flush and close the open file. Call before removing the card. */
void sd_close(void);


#ifdef __cplusplus
}
#endif

#endif /* ROVARI_SD_H */