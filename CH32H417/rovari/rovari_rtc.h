/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Rovari - rvembedded.com
 *
 * rovari_rtc.h - Real-Time Clock (CH32H417)
 *
 * 32-bit seconds counter clocked from LSE (32.768 kHz crystal) or LSI
 * (~40 kHz internal RC). The counter stores seconds since the Unix epoch
 * (1970-01-01 00:00:00). Calendar conversion is done in software.
 *
 * This driver presents the same public API as the CH32V203 / CH32V307 RTC
 * drivers so a sketch written for one Rovari target compiles unchanged on
 * another. The H417 adds two interrupt callbacks (rtc_on_second,
 * rtc_on_alarm) on top of that shared contract.
 *
 * Usage:
 *   Rtc rtc;
 *   rtc.begin();
 *   rtc.setDateTime(2026, 4, 29, 22, 30, 0);
 *
 *   RtcDateTime dt;
 *   rtc.getDateTime(&dt);
 */

#ifndef ROVARI_RTC_H
#define ROVARI_RTC_H

#include "rovari_defs.h"
#include <stdint.h>

/* -- Types ---------------------------------------------------------------- */

typedef enum {
    RTC_CLK_LSE    = 0,   /* 32.768 kHz external crystal (most accurate) */
    RTC_CLK_LSI    = 1,   /* ~40 kHz internal RC oscillator (default, no xtal) */
    RTC_CLK_HSE128 = 2    /* HSE / 128 (lost on power-down) */
} RtcClockSource;

typedef struct {
    uint16_t year;    /* 1970-2099 */
    uint8_t  month;   /* 1-12 */
    uint8_t  day;     /* 1-31 */
    uint8_t  hour;    /* 0-23 */
    uint8_t  min;     /* 0-59 */
    uint8_t  sec;     /* 0-59 */
    uint8_t  weekday; /* 0=Sunday, 1=Monday, ... 6=Saturday */
} RtcDateTime;

/*
 * RtcTime is the legacy H417 name for the calendar struct. It is kept as an
 * alias of RtcDateTime so existing H417 code (e.g. the FatFs get_fattime
 * hook in rovari_diskio.c) compiles unchanged. New code should use
 * RtcDateTime for portability across targets.
 */
typedef RtcDateTime RtcTime;

typedef void (*RtcCallback)(void);

/* =========================================================================
 *  C API
 * ========================================================================= */
#ifdef __cplusplus
extern "C" {
#endif

/* -- Init ----------------------------------------------------------------- */

/**
 * Initialize the RTC.
 * If the RTC was already configured and running (preserved across reset by
 * VBAT), the existing counter is kept.
 * @param src  Clock source (LSI default on the H417 EVT board; use LSE if a
 *             32.768 kHz crystal is fitted).
 * @return 0 on success, 1 if the selected oscillator failed to start.
 */
uint8_t rtc_init(RtcClockSource src);

/** Was the RTC already running before this boot? */
uint8_t rtc_was_running(void);

/* -- Date and time -------------------------------------------------------- */

/** Set date and time. */
void rtc_set_datetime(uint16_t year, uint8_t month, uint8_t day,
                      uint8_t hour, uint8_t min, uint8_t sec);

/** Get current date and time into a struct. */
void rtc_get_datetime(RtcDateTime *dt);

/**
 * Get current date and time by value.
 * Legacy H417 convenience form; identical data to rtc_get_datetime().
 */
RtcTime rtc_get(void);

/**
 * Set date and time (legacy H417 name; equivalent to rtc_set_datetime()).
 */
void rtc_set(uint16_t year, uint8_t month, uint8_t day,
             uint8_t hour, uint8_t min, uint8_t sec);

/** Set time only (hours, minutes, seconds); preserves the current date. */
void rtc_set_time(uint8_t h, uint8_t m, uint8_t s);

/** Get time only (hours, minutes, seconds). */
void rtc_get_time(uint8_t *h, uint8_t *m, uint8_t *s);

/* -- Raw epoch ------------------------------------------------------------ */

/** Get raw counter (seconds since 1970-01-01). */
uint32_t rtc_get_epoch(void);

/** Set raw counter. */
void rtc_set_epoch(uint32_t seconds);

/* -- Alarm ---------------------------------------------------------------- */

/** Set alarm at a specific date/time. */
void rtc_set_alarm_datetime(uint16_t year, uint8_t month, uint8_t day,
                            uint8_t hour, uint8_t min, uint8_t sec);

/** Set alarm at a specific time today. */
void rtc_set_alarm(uint8_t h, uint8_t m, uint8_t s);

/** Set alarm by raw epoch value. */
void rtc_set_alarm_epoch(uint32_t seconds);

/** Check and clear the alarm flag. Returns 1 if the alarm fired. */
uint8_t rtc_alarm_fired(void);

/* -- Interrupt callbacks (H417 extension) --------------------------------- */

/** Register a callback for the once-per-second interrupt. */
void rtc_on_second(RtcCallback callback);

/** Register a callback for the alarm interrupt. */
void rtc_on_alarm(RtcCallback callback);

/* -- Calibration ---------------------------------------------------------- */

/**
 * Set the RTC calibration value (0-127): number of clock pulses masked out
 * of every 2^20. Each unit slows the clock by ~0.954 ppm. Use to compensate
 * a fast crystal; a slow crystal cannot be corrected this way.
 */
void rtc_set_calibration(uint8_t value);

/** Get current calibration value (0-127). */
uint8_t rtc_get_calibration(void);

/* -- Conversion helpers --------------------------------------------------- */

/** Convert date/time to Unix epoch seconds. */
uint32_t rtc_datetime_to_epoch(uint16_t year, uint8_t month, uint8_t day,
                               uint8_t hour, uint8_t min, uint8_t sec);

/** Convert Unix epoch to date/time struct. */
void rtc_epoch_to_datetime(uint32_t epoch, RtcDateTime *dt);

/** Get day of week (0=Sunday). */
uint8_t rtc_day_of_week(uint16_t year, uint8_t month, uint8_t day);

/** Check if a year is a leap year. */
uint8_t rtc_is_leap_year(uint16_t year);

#ifdef __cplusplus
}
#endif

/* =========================================================================
 *  C++ API
 * ========================================================================= */
#ifdef __cplusplus

class Rtc {
public:
    explicit Rtc(RtcClockSource src = RTC_CLK_LSI) : _src(src) {}

    uint8_t begin()                    { return rtc_init(_src); }
    uint8_t wasRunning()               { return rtc_was_running(); }

    void setDateTime(uint16_t year, uint8_t month, uint8_t day,
                     uint8_t hour, uint8_t min, uint8_t sec) {
        rtc_set_datetime(year, month, day, hour, min, sec);
    }
    void getDateTime(RtcDateTime *dt)  { rtc_get_datetime(dt); }

    void setTime(uint8_t h, uint8_t m, uint8_t s) { rtc_set_time(h, m, s); }
    void getTime(uint8_t *h, uint8_t *m, uint8_t *s) { rtc_get_time(h, m, s); }

    uint32_t epoch()                   { return rtc_get_epoch(); }
    void setEpoch(uint32_t s)          { rtc_set_epoch(s); }

    void setAlarm(uint8_t h, uint8_t m, uint8_t s) { rtc_set_alarm(h, m, s); }
    void setAlarmDateTime(uint16_t year, uint8_t month, uint8_t day,
                          uint8_t hour, uint8_t min, uint8_t sec) {
        rtc_set_alarm_datetime(year, month, day, hour, min, sec);
    }
    void setAlarmEpoch(uint32_t s)     { rtc_set_alarm_epoch(s); }
    uint8_t alarmFired()               { return rtc_alarm_fired(); }

    void onSecond(RtcCallback cb)      { rtc_on_second(cb); }
    void onAlarm(RtcCallback cb)       { rtc_on_alarm(cb); }

    void setCalibration(uint8_t val)   { rtc_set_calibration(val); }
    uint8_t getCalibration()           { return rtc_get_calibration(); }

private:
    RtcClockSource _src;
};

#endif /* __cplusplus */
#endif /* ROVARI_RTC_H */
