/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Rovari - rvembedded.com
 *
 * rovari_rtc.h - Real-Time Clock driver for CH32V307
 *
 * Full calendar support with Unix epoch-based counter.
 * The counter stores seconds since 1970-01-01 00:00:00 UTC.
 * Software converts between epoch and year/month/day/hour/min/sec.
 *
 * Clock sources:
 *   RTC_CLK_LSE    32.768 kHz external crystal (accurate, default)
 *   RTC_CLK_LSI    ~40 kHz internal oscillator (drifts ~5%+)
 *   RTC_CLK_HSE128 HSE / 128 (lost on power-down)
 *
 * Usage:
 *   Rtc rtc;
 *   rtc.begin();
 *   rtc.setDateTime(2025, 6, 15, 14, 30, 0);
 *
 *   RtcDateTime dt;
 *   rtc.getDateTime(&dt);
 *   serial.printf("%04d-%02d-%02d %02d:%02d:%02d\n",
 *       dt.year, dt.month, dt.day, dt.hour, dt.min, dt.sec);
 */

#ifndef ROVARI_RTC_H
#define ROVARI_RTC_H

#include "rovari_defs.h"
#include <stdint.h>

/* -----------------------------------------------------------------------
 *  Types
 * ----------------------------------------------------------------------- */

typedef enum {
    RTC_CLK_LSE    = 0,   /* 32.768 kHz external crystal (default) */
    RTC_CLK_LSI    = 1,   /* ~40 kHz internal RC oscillator */
    RTC_CLK_HSE128 = 2    /* HSE / 128 */
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

/* -----------------------------------------------------------------------
 *  C API
 * ----------------------------------------------------------------------- */

#ifdef __cplusplus
extern "C" {
#endif

/* Init */

/**
 * Initialize the RTC.
 * If the RTC was already configured (magic in BKP_DR1), skips setup
 * and preserves the running counter.
 * @param src  Clock source
 * @return 0 on success, 1 if LSE failed to start
 */
uint8_t rtc_init(RtcClockSource src);

/**
 * Was the RTC already running before this boot?
 */
uint8_t rtc_was_running(void);

/* Date and time */

/**
 * Set date and time.
 * Converts to Unix epoch and writes the counter.
 */
void rtc_set_datetime(uint16_t year, uint8_t month, uint8_t day,
                      uint8_t hour, uint8_t min, uint8_t sec);

/**
 * Get current date and time.
 * Reads the counter and converts from Unix epoch.
 */
void rtc_get_datetime(RtcDateTime *dt);

/**
 * Set time only (hours, minutes, seconds).
 * Preserves the current date.
 */
void rtc_set_time(uint8_t h, uint8_t m, uint8_t s);

/**
 * Get time only (hours, minutes, seconds).
 */
void rtc_get_time(uint8_t *h, uint8_t *m, uint8_t *s);

/* Raw epoch */

/** Get raw counter (seconds since 1970-01-01). */
uint32_t rtc_get_epoch(void);

/** Set raw counter. */
void rtc_set_epoch(uint32_t seconds);

/* Alarm */

/** Set alarm at a specific date/time. */
void rtc_set_alarm_datetime(uint16_t year, uint8_t month, uint8_t day,
                            uint8_t hour, uint8_t min, uint8_t sec);

/** Set alarm at a specific time today. */
void rtc_set_alarm(uint8_t h, uint8_t m, uint8_t s);

/** Set alarm by raw epoch value. */
void rtc_set_alarm_epoch(uint32_t seconds);

/** Check and clear the alarm flag. Returns 1 if alarm fired. */
uint8_t rtc_alarm_fired(void);

/* Calibration */

/**
 * Set the RTC calibration value.
 * The RTC calibration works by masking a number of clock pulses
 * out of every 2^20 (1,048,576) pulses of the RTC clock.
 *
 * @param value  Number of pulses to mask (0-127).
 *               Each unit slows the clock by ~0.954 ppm.
 *               Use this to compensate for a fast crystal.
 *               Maximum correction: ~121 ppm (about 10.4 sec/day).
 *
 * To calibrate:
 *   1. Let the RTC run for 24 hours against a known reference.
 *   2. Measure drift in seconds per day.
 *   3. If the clock is FAST by N seconds/day:
 *      value = N / (86400 / 1048576) ≈ N * 12.136
 *   4. If the clock is SLOW, calibration cannot help; the crystal
 *      or load capacitors need adjustment.
 */
void rtc_set_calibration(uint8_t value);

/** Get current calibration value (0-127). */
uint8_t rtc_get_calibration(void);

/* Conversion helpers (public for user convenience) */

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

/* -----------------------------------------------------------------------
 *  C++ API
 * ----------------------------------------------------------------------- */

#ifdef __cplusplus

class Rtc {
public:
    explicit Rtc(RtcClockSource src = RTC_CLK_LSE) : _src(src) {}

    /** Initialize RTC. Returns 0 on success. */
    uint8_t begin()                    { return rtc_init(_src); }

    /** Was the RTC already running before this boot? */
    uint8_t wasRunning()               { return rtc_was_running(); }

    /* Date and time */

    void setDateTime(uint16_t year, uint8_t month, uint8_t day,
                     uint8_t hour, uint8_t min, uint8_t sec) {
        rtc_set_datetime(year, month, day, hour, min, sec);
    }

    void getDateTime(RtcDateTime *dt)  { rtc_get_datetime(dt); }

    void setTime(uint8_t h, uint8_t m, uint8_t s) { rtc_set_time(h, m, s); }
    void getTime(uint8_t *h, uint8_t *m, uint8_t *s) { rtc_get_time(h, m, s); }

    /* Raw epoch */

    uint32_t epoch()                   { return rtc_get_epoch(); }
    void setEpoch(uint32_t s)          { rtc_set_epoch(s); }

    /* Alarm */

    void setAlarm(uint8_t h, uint8_t m, uint8_t s) { rtc_set_alarm(h, m, s); }

    void setAlarmDateTime(uint16_t year, uint8_t month, uint8_t day,
                          uint8_t hour, uint8_t min, uint8_t sec) {
        rtc_set_alarm_datetime(year, month, day, hour, min, sec);
    }

    void setAlarmEpoch(uint32_t s)     { rtc_set_alarm_epoch(s); }
    uint8_t alarmFired()               { return rtc_alarm_fired(); }

    /* Calibration */

    void setCalibration(uint8_t val)   { rtc_set_calibration(val); }
    uint8_t getCalibration()           { return rtc_get_calibration(); }

private:
    RtcClockSource _src;
};

#endif /* __cplusplus */
#endif /* ROVARI_RTC_H */