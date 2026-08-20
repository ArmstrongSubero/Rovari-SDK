/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Rovari - rvembedded.com
 *
 * rovari_rtc.c - Real-Time Clock implementation (CH32H417)
 *
 * The H417 RTC is a 32-bit free-running counter clocked from LSI (40 kHz)
 * or LSE (32.768 kHz external crystal). The counter value represents
 * seconds since the Unix epoch (1970-01-01 00:00:00).
 *
 * Calendar conversion (year/month/day/hour/min/sec) is done in software
 * by converting to/from the seconds counter.
 *
 * The RTC and backup registers are powered from VBAT when main power is
 * off (if a battery is connected to the VBAT pin).
 *
 * This file implements the shared Rovari RTC API (same as CH32V203/V307)
 * plus the H417-specific per-second / alarm interrupt callbacks. All the
 * calendar arithmetic funnels through datetime_to_seccount() and the
 * counter read in rtc_get() so there is one source of truth for the math.
 */

#include "rovari_rtc.h"
#include "debug.h"

/* -- Month length table --------------------------------------------------- */
static const uint8_t mon_table[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

static uint8_t is_leap_year(uint16_t year)
{
    if (year % 4 == 0) {
        if (year % 100 == 0) {
            return (year % 400 == 0) ? 1 : 0;
        }
        return 1;
    }
    return 0;
}

/* -- State ---------------------------------------------------------------- */
static RtcTime current_time = {0};
static volatile RtcCallback rtc_second_callback = 0;
static volatile RtcCallback rtc_alarm_callback = 0;
static volatile uint8_t rtc_alarm_flag = 0;

/* -- Shared calendar -> seconds-since-epoch conversion -------------------- */
static uint32_t datetime_to_seccount(uint16_t year, uint8_t month, uint8_t day,
                                     uint8_t hour, uint8_t min, uint8_t sec)
{
    uint32_t seccount = 0;

    if (year < 1970 || year > 2099) return 0;

    for (uint16_t t = 1970; t < year; t++) {
        seccount += is_leap_year(t) ? 31622400u : 31536000u;
    }

    for (uint8_t t = 0; t < month - 1; t++) {
        seccount += (uint32_t)mon_table[t] * 86400u;
        if (is_leap_year(year) && t == 1) {
            seccount += 86400u;
        }
    }

    seccount += (uint32_t)(day - 1) * 86400u;
    seccount += (uint32_t)hour * 3600u;
    seccount += (uint32_t)min * 60u;
    seccount += sec;

    return seccount;
}

/* -- Shared seconds-since-epoch -> calendar conversion -------------------- */
static void seccount_to_datetime(uint32_t timecount, RtcDateTime *dt)
{
    uint32_t temp = timecount / 86400u;
    uint32_t y;

    /* Year */
    y = 1970;
    while (temp >= 365) {
        if (is_leap_year(y)) {
            if (temp >= 366) temp -= 366;
            else break;
        } else {
            temp -= 365;
        }
        y++;
    }
    dt->year = (uint16_t)y;

    /* Month */
    {
        uint8_t m = 0;
        while (temp >= 28) {
            if (is_leap_year(dt->year) && m == 1) {
                if (temp >= 29) temp -= 29;
                else break;
            } else {
                if (temp >= mon_table[m]) temp -= mon_table[m];
                else break;
            }
            m++;
        }
        dt->month = m + 1;
        dt->day = (uint8_t)(temp + 1);
    }

    /* Time of day */
    temp = timecount % 86400u;
    dt->hour = (uint8_t)(temp / 3600u);
    dt->min  = (uint8_t)((temp % 3600u) / 60u);
    dt->sec  = (uint8_t)((temp % 3600u) % 60u);

    /* Day of week: 1970-01-01 was a Thursday (4). */
    dt->weekday = (uint8_t)(((timecount / 86400u) + 4u) % 7u);
}

/* =========================================================================
 *  Init
 * ========================================================================= */

uint8_t rtc_init(RtcClockSource src)
{
    uint8_t timeout = 0;

    /* Enable PWR and BKP clocks */
    RCC_HB1PeriphClockCmd(RCC_HB1Periph_PWR | RCC_HB1Periph_BKP, ENABLE);
    PWR_BackupAccessCmd(ENABLE);

    /* Clear any pending RTC interrupts */
    RTC_ClearITPendingBit(RTC_IT_ALR);
    RTC_ClearITPendingBit(RTC_IT_SEC);

    if (src == RTC_CLK_LSE) {
        /* Start the external 32.768 kHz crystal */
        RCC_LSEConfig(RCC_LSE_ON);
        while (RCC_GetFlagStatus(RCC_FLAG_LSERDY) == RESET) {
            timeout++;
            Delay_Ms(10);
            if (timeout >= 250) return 1;  /* LSE failed to start */
        }
        RCC_RTCCLKConfig(RCC_RTCCLKSource_LSE);
        RCC_RTCCLKCmd(ENABLE);

        RTC_WaitForLastTask();
        RTC_WaitForSynchro();
        RTC_ITConfig(RTC_IT_SEC, ENABLE);
        RTC_WaitForLastTask();

        /* 32768 Hz / (32767 + 1) = 1 Hz */
        RTC_EnterConfigMode();
        RTC_SetPrescaler(32767);
        RTC_WaitForLastTask();
        RTC_ExitConfigMode();
    } else {
        /* Default: LSI (no external crystal needed). HSE128 not wired on the
         * EVT board, so it falls through to the internal oscillator too. */
        RCC_LSICmd(ENABLE);
        while (RCC_GetFlagStatus(RCC_FLAG_LSIRDY) == RESET) {
            timeout++;
            Delay_Ms(10);
            if (timeout >= 250) return 1;  /* LSI failed to start */
        }
        RCC_RTCCLKConfig(RCC_RTCCLKSource_LSI);
        RCC_RTCCLKCmd(ENABLE);

        RTC_WaitForLastTask();
        RTC_WaitForSynchro();
        RTC_ITConfig(RTC_IT_SEC, ENABLE);
        RTC_WaitForLastTask();

        /* ~40 kHz / 40000 = 1 Hz */
        RTC_EnterConfigMode();
        RTC_SetPrescaler(40000);
        RTC_WaitForLastTask();
        RTC_ExitConfigMode();
    }

    /* Enable IRQ */
    NVIC_SetPriority(RTC_IRQn, 2);
    NVIC_EnableIRQ(RTC_IRQn);

    return 0;
}

uint8_t rtc_was_running(void)
{
    /* The H417 RTC counter persists across reset while VBAT is supplied.
     * A nonzero counter means it was already keeping time. */
    return (RTC_GetCounter() != 0) ? 1 : 0;
}

/* =========================================================================
 *  Date and time
 * ========================================================================= */

void rtc_set_datetime(uint16_t year, uint8_t month, uint8_t day,
                      uint8_t hour, uint8_t min, uint8_t sec)
{
    uint32_t seccount;

    if (year < 1970 || year > 2099) return;

    seccount = datetime_to_seccount(year, month, day, hour, min, sec);

    RCC_HB1PeriphClockCmd(RCC_HB1Periph_PWR | RCC_HB1Periph_BKP, ENABLE);
    PWR_BackupAccessCmd(ENABLE);
    RTC_SetCounter(seccount);
    RTC_WaitForLastTask();
}

/* Legacy H417 name; identical behavior. */
void rtc_set(uint16_t year, uint8_t month, uint8_t day,
             uint8_t hour, uint8_t min, uint8_t sec)
{
    rtc_set_datetime(year, month, day, hour, min, sec);
}

void rtc_get_datetime(RtcDateTime *dt)
{
    if (!dt) return;
    seccount_to_datetime(RTC_GetCounter(), dt);
    current_time = *dt;
}

RtcTime rtc_get(void)
{
    seccount_to_datetime(RTC_GetCounter(), &current_time);
    return current_time;
}

void rtc_set_time(uint8_t h, uint8_t m, uint8_t s)
{
    RtcDateTime dt;
    seccount_to_datetime(RTC_GetCounter(), &dt);
    rtc_set_datetime(dt.year, dt.month, dt.day, h, m, s);
}

void rtc_get_time(uint8_t *h, uint8_t *m, uint8_t *s)
{
    RtcDateTime dt;
    seccount_to_datetime(RTC_GetCounter(), &dt);
    if (h) *h = dt.hour;
    if (m) *m = dt.min;
    if (s) *s = dt.sec;
}

/* =========================================================================
 *  Raw epoch
 * ========================================================================= */

uint32_t rtc_get_epoch(void)
{
    return RTC_GetCounter();
}

void rtc_set_epoch(uint32_t seconds)
{
    RCC_HB1PeriphClockCmd(RCC_HB1Periph_PWR | RCC_HB1Periph_BKP, ENABLE);
    PWR_BackupAccessCmd(ENABLE);
    RTC_SetCounter(seconds);
    RTC_WaitForLastTask();
}

/* =========================================================================
 *  Alarm
 * ========================================================================= */

void rtc_set_alarm_datetime(uint16_t year, uint8_t month, uint8_t day,
                            uint8_t hour, uint8_t min, uint8_t sec)
{
    uint32_t seccount;

    if (year < 1970 || year > 2099) return;

    seccount = datetime_to_seccount(year, month, day, hour, min, sec);

    RCC_HB1PeriphClockCmd(RCC_HB1Periph_PWR | RCC_HB1Periph_BKP, ENABLE);
    PWR_BackupAccessCmd(ENABLE);
    RTC_SetAlarm(seccount);
    RTC_WaitForLastTask();

    RTC_ITConfig(RTC_IT_ALR, ENABLE);
    RTC_WaitForLastTask();
}

void rtc_set_alarm(uint8_t h, uint8_t m, uint8_t s)
{
    RtcDateTime dt;
    seccount_to_datetime(RTC_GetCounter(), &dt);
    rtc_set_alarm_datetime(dt.year, dt.month, dt.day, h, m, s);
}

void rtc_set_alarm_epoch(uint32_t seconds)
{
    RCC_HB1PeriphClockCmd(RCC_HB1Periph_PWR | RCC_HB1Periph_BKP, ENABLE);
    PWR_BackupAccessCmd(ENABLE);
    RTC_SetAlarm(seconds);
    RTC_WaitForLastTask();

    RTC_ITConfig(RTC_IT_ALR, ENABLE);
    RTC_WaitForLastTask();
}

uint8_t rtc_alarm_fired(void)
{
    uint8_t fired = rtc_alarm_flag;
    rtc_alarm_flag = 0;
    return fired;
}

/* =========================================================================
 *  Interrupt callbacks (H417 extension)
 * ========================================================================= */

void rtc_on_second(RtcCallback callback)
{
    rtc_second_callback = callback;
}

void rtc_on_alarm(RtcCallback callback)
{
    rtc_alarm_callback = callback;
}

/* =========================================================================
 *  Calibration
 * ========================================================================= */

void rtc_set_calibration(uint8_t value)
{
    if (value > 127) value = 127;
    RCC_HB1PeriphClockCmd(RCC_HB1Periph_PWR | RCC_HB1Periph_BKP, ENABLE);
    PWR_BackupAccessCmd(ENABLE);
    BKP_SetRTCCalibrationValue(value);
}

uint8_t rtc_get_calibration(void)
{
    return BKP_GetRTCCalibrationValue();
}

/* =========================================================================
 *  Conversion helpers (public)
 * ========================================================================= */

uint32_t rtc_datetime_to_epoch(uint16_t year, uint8_t month, uint8_t day,
                               uint8_t hour, uint8_t min, uint8_t sec)
{
    return datetime_to_seccount(year, month, day, hour, min, sec);
}

void rtc_epoch_to_datetime(uint32_t epoch, RtcDateTime *dt)
{
    if (!dt) return;
    seccount_to_datetime(epoch, dt);
}

uint8_t rtc_day_of_week(uint16_t year, uint8_t month, uint8_t day)
{
    uint32_t seccount = datetime_to_seccount(year, month, day, 0, 0, 0);
    return (uint8_t)(((seccount / 86400u) + 4u) % 7u);  /* 1970-01-01 = Thu */
}

uint8_t rtc_is_leap_year(uint16_t year)
{
    return is_leap_year(year);
}

/* =========================================================================
 *  ISR handler
 * ========================================================================= */

void RTC_IRQHandler(void) __attribute__((interrupt("machine")));
void RTC_IRQHandler(void)
{
    if (RTC_GetITStatus(RTC_IT_SEC) != RESET) {
        RTC_ClearITPendingBit(RTC_IT_SEC);
        seccount_to_datetime(RTC_GetCounter(), &current_time);
        if (rtc_second_callback) {
            rtc_second_callback();
        }
    }

    if (RTC_GetITStatus(RTC_IT_ALR) != RESET) {
        RTC_ClearITPendingBit(RTC_IT_ALR);
        rtc_alarm_flag = 1;
        if (rtc_alarm_callback) {
            rtc_alarm_callback();
        }
    }

    RTC_ClearITPendingBit(RTC_IT_OW);
    RTC_WaitForLastTask();
}
