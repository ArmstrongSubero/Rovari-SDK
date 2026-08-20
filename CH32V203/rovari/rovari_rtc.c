/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Rovari — rvembedded.com
 *
 * rovari_rtc.c — Real-Time Clock driver for CH32V307
 *
 * Full calendar with Unix epoch counter. Based on WCH RTC_Calendar patterns.
 */

#include "rovari_rtc.h"
#include "ch32v30x.h"
#include "ch32v30x_rcc.h"
#include "ch32v30x_pwr.h"
#include "ch32v30x_bkp.h"
#include "ch32v30x_rtc.h"
#include "debug.h"

/* ── Constants ───────────────────────────────────────────────────────── */

#define RTC_BKP_MAGIC    0xA1A1
#define PRESCALER_LSE    32767
#define PRESCALER_LSI    39999
#define PRESCALER_HSE128 (SystemCoreClock / 128 - 1)
#define SECS_PER_DAY     86400
#define SECS_PER_HOUR    3600
#define SECS_PER_MIN     60

/* ── Internal state ──────────────────────────────────────────────────── */

static uint8_t rtc_already_running = 0;

/* ── Month day table ─────────────────────────────────────────────────── */

static const uint8_t days_in_month[12] = {
    31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
};

/* Week day table for Zeller-like calculation */
static const uint8_t week_table[12] = {
    0, 3, 3, 6, 1, 4, 6, 2, 5, 0, 3, 5
};


/* ═══════════════════════════════════════════════════════════════════════
 *  Calendar conversion helpers
 * ═══════════════════════════════════════════════════════════════════════ */

uint8_t rtc_is_leap_year(uint16_t year)
{
    if (year % 4 != 0) return 0;
    if (year % 100 != 0) return 1;
    if (year % 400 == 0) return 1;
    return 0;
}

uint8_t rtc_day_of_week(uint16_t year, uint8_t month, uint8_t day)
{
    uint16_t temp;
    uint8_t  yH = year / 100;
    uint8_t  yL = year % 100;

    if (yH > 19) yL += 100;
    temp = yL + yL / 4;
    temp = temp % 7;
    temp = temp + day + week_table[month - 1];
    if (yL % 4 == 0 && month < 3) temp--;
    return (temp % 7);
}

uint32_t rtc_datetime_to_epoch(uint16_t year, uint8_t month, uint8_t day,
                               uint8_t hour, uint8_t min, uint8_t sec)
{
    uint32_t secs = 0;
    uint16_t y;

    if (year < 1970) return 0;

    /* Accumulate years */
    for (y = 1970; y < year; y++) {
        secs += rtc_is_leap_year(y) ? 31622400 : 31536000;
    }

    /* Accumulate months */
    for (uint8_t m = 0; m < month - 1; m++) {
        secs += (uint32_t)days_in_month[m] * SECS_PER_DAY;
        if (rtc_is_leap_year(year) && m == 1) {
            secs += SECS_PER_DAY;  /* Feb 29 */
        }
    }

    /* Days, hours, minutes, seconds */
    secs += (uint32_t)(day - 1) * SECS_PER_DAY;
    secs += (uint32_t)hour * SECS_PER_HOUR;
    secs += (uint32_t)min * SECS_PER_MIN;
    secs += sec;

    return secs;
}

void rtc_epoch_to_datetime(uint32_t epoch, RtcDateTime *dt)
{
    uint32_t days = epoch / SECS_PER_DAY;
    uint32_t rem  = epoch % SECS_PER_DAY;

    /* Time of day */
    dt->hour = (uint8_t)(rem / SECS_PER_HOUR);
    rem %= SECS_PER_HOUR;
    dt->min  = (uint8_t)(rem / SECS_PER_MIN);
    dt->sec  = (uint8_t)(rem % SECS_PER_MIN);

    /* Year */
    uint16_t year = 1970;
    while (days >= 365) {
        if (rtc_is_leap_year(year)) {
            if (days >= 366) days -= 366; else break;
        } else {
            days -= 365;
        }
        year++;
    }
    dt->year = year;

    /* Month */
    uint8_t month = 0;
    while (days >= 28) {
        if (rtc_is_leap_year(year) && month == 1) {
            if (days >= 29) days -= 29; else break;
        } else {
            if (days >= days_in_month[month]) {
                days -= days_in_month[month];
            } else {
                break;
            }
        }
        month++;
    }
    dt->month = month + 1;
    dt->day   = (uint8_t)(days + 1);

    /* Weekday */
    dt->weekday = rtc_day_of_week(dt->year, dt->month, dt->day);
}


/* ═══════════════════════════════════════════════════════════════════════
 *  Init
 * ═══════════════════════════════════════════════════════════════════════ */

uint8_t rtc_init(RtcClockSource src)
{
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_PWR | RCC_APB1Periph_BKP, ENABLE);
    PWR_BackupAccessCmd(ENABLE);
    RTC_ClearITPendingBit(RTC_IT_ALR);
    RTC_ClearITPendingBit(RTC_IT_SEC);

    /* Check if RTC was already configured */
    if (BKP_ReadBackupRegister(BKP_DR1) == RTC_BKP_MAGIC) {
        rtc_already_running = 1;
        RTC_WaitForLastTask();
        RTC_WaitForSynchro();
        RTC_WaitForLastTask();
        return 0;
    }

    /* Fresh configuration */
    rtc_already_running = 0;
    BKP_DeInit();

    switch (src) {
    case RTC_CLK_LSE: {
        RCC_LSEConfig(RCC_LSE_ON);
        uint8_t retry = 0;
        while (RCC_GetFlagStatus(RCC_FLAG_LSERDY) == RESET && retry < 250) {
            retry++;
            Delay_Ms(20);
        }
        if (retry >= 250) return 1;

        RCC_RTCCLKConfig(RCC_RTCCLKSource_LSE);
        RCC_RTCCLKCmd(ENABLE);
        RTC_WaitForLastTask();
        RTC_WaitForSynchro();
        RTC_WaitForLastTask();

        RTC_EnterConfigMode();
        RTC_SetPrescaler(PRESCALER_LSE);
        RTC_WaitForLastTask();
        RTC_ExitConfigMode();
        break;
    }

    case RTC_CLK_LSI: {
        RCC_LSICmd(ENABLE);
        while (RCC_GetFlagStatus(RCC_FLAG_LSIRDY) == RESET) {}

        RCC_RTCCLKConfig(RCC_RTCCLKSource_LSI);
        RCC_RTCCLKCmd(ENABLE);
        RTC_WaitForLastTask();
        RTC_WaitForSynchro();
        RTC_WaitForLastTask();

        RTC_EnterConfigMode();
        RTC_SetPrescaler(PRESCALER_LSI);
        RTC_WaitForLastTask();
        RTC_ExitConfigMode();
        break;
    }

    case RTC_CLK_HSE128: {
        RCC_RTCCLKConfig(RCC_RTCCLKSource_HSE_Div128);
        RCC_RTCCLKCmd(ENABLE);
        RTC_WaitForLastTask();
        RTC_WaitForSynchro();
        RTC_WaitForLastTask();

        RTC_EnterConfigMode();
        RTC_SetPrescaler(PRESCALER_HSE128);
        RTC_WaitForLastTask();
        RTC_ExitConfigMode();
        break;
    }
    }

    /* Default to 2025-01-01 00:00:00 */
    RTC_SetCounter(rtc_datetime_to_epoch(2025, 1, 1, 0, 0, 0));
    RTC_WaitForLastTask();

    BKP_WriteBackupRegister(BKP_DR1, RTC_BKP_MAGIC);
    return 0;
}


/* ═══════════════════════════════════════════════════════════════════════
 *  Date and time
 * ═══════════════════════════════════════════════════════════════════════ */

void rtc_set_datetime(uint16_t year, uint8_t month, uint8_t day,
                      uint8_t hour, uint8_t min, uint8_t sec)
{
    uint32_t epoch = rtc_datetime_to_epoch(year, month, day, hour, min, sec);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_PWR | RCC_APB1Periph_BKP, ENABLE);
    PWR_BackupAccessCmd(ENABLE);
    RTC_SetCounter(epoch);
    RTC_WaitForLastTask();
}

void rtc_get_datetime(RtcDateTime *dt)
{
    uint32_t epoch = RTC_GetCounter();
    rtc_epoch_to_datetime(epoch, dt);
}

void rtc_set_time(uint8_t h, uint8_t m, uint8_t s)
{
    /* Preserve the date, replace only the time */
    RtcDateTime dt;
    rtc_get_datetime(&dt);
    rtc_set_datetime(dt.year, dt.month, dt.day, h, m, s);
}

void rtc_get_time(uint8_t *h, uint8_t *m, uint8_t *s)
{
    RtcDateTime dt;
    rtc_get_datetime(&dt);
    *h = dt.hour;
    *m = dt.min;
    *s = dt.sec;
}


/* ═══════════════════════════════════════════════════════════════════════
 *  Raw epoch
 * ═══════════════════════════════════════════════════════════════════════ */

uint32_t rtc_get_epoch(void)
{
    return RTC_GetCounter();
}

void rtc_set_epoch(uint32_t seconds)
{
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_PWR | RCC_APB1Periph_BKP, ENABLE);
    PWR_BackupAccessCmd(ENABLE);
    RTC_SetCounter(seconds);
    RTC_WaitForLastTask();
}


/* ═══════════════════════════════════════════════════════════════════════
 *  Alarm
 * ═══════════════════════════════════════════════════════════════════════ */

void rtc_set_alarm_datetime(uint16_t year, uint8_t month, uint8_t day,
                            uint8_t hour, uint8_t min, uint8_t sec)
{
    uint32_t epoch = rtc_datetime_to_epoch(year, month, day, hour, min, sec);
    rtc_set_alarm_epoch(epoch);
}

void rtc_set_alarm(uint8_t h, uint8_t m, uint8_t s)
{
    /* Alarm today at h:m:s — use current date */
    RtcDateTime dt;
    rtc_get_datetime(&dt);
    uint32_t epoch = rtc_datetime_to_epoch(dt.year, dt.month, dt.day, h, m, s);
    rtc_set_alarm_epoch(epoch);
}

void rtc_set_alarm_epoch(uint32_t seconds)
{
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_PWR | RCC_APB1Periph_BKP, ENABLE);
    PWR_BackupAccessCmd(ENABLE);
    RTC_SetAlarm(seconds);
    RTC_WaitForLastTask();
    RTC_ClearFlag(RTC_FLAG_ALR);
}

uint8_t rtc_alarm_fired(void)
{
    if (RTC_GetFlagStatus(RTC_FLAG_ALR) != RESET) {
        RTC_ClearFlag(RTC_FLAG_ALR);
        return 1;
    }
    return 0;
}


/* ═══════════════════════════════════════════════════════════════════════
 *  Calibration
 * ═══════════════════════════════════════════════════════════════════════ */

void rtc_set_calibration(uint8_t value)
{
    if (value > 127) value = 127;

    RCC_APB1PeriphClockCmd(RCC_APB1Periph_PWR | RCC_APB1Periph_BKP, ENABLE);
    PWR_BackupAccessCmd(ENABLE);

    /* BKP_RTCCR register: bits [6:0] = calibration value */
    uint16_t reg = BKP->OCTLR;
    reg &= ~((uint16_t)0x007F);     /* clear bits [6:0] */
    reg |= (uint16_t)(value & 0x7F);
    BKP->OCTLR = reg;
}

uint8_t rtc_get_calibration(void)
{
    return (uint8_t)(BKP->OCTLR & 0x7F);
}


/* ═══════════════════════════════════════════════════════════════════════
 *  Info
 * ═══════════════════════════════════════════════════════════════════════ */

uint8_t rtc_was_running(void)
{
    return rtc_already_running;
}