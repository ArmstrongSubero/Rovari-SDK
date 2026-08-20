/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Rovari - rvembedded.com
 */

/**
 * @file rovari_rtc.c
 * @brief Real-time clock with Unix-epoch calendar for CH32V307.
 *
 * Full calendar over a 32-bit seconds counter. LSE/LSI/HSE128 sources.
 * Clock-ready polling is bounded so an absent oscillator cannot hang.
 */

#include <stddef.h>
#include <stdint.h>
#include "sevs_runtime.h"
#include "ch32v30x.h"
#include "ch32v30x_rcc.h"
#include "ch32v30x_pwr.h"
#include "ch32v30x_bkp.h"
#include "ch32v30x_rtc.h"
#include "debug.h"
#include "rovari_rtc.h"

/* Constants */
#define RTC_BKP_MAGIC    0xA1A1
#define PRESCALER_LSE    32767
#define PRESCALER_LSI    39999
#define PRESCALER_HSE128 (SystemCoreClock / 128 - 1)
#define SECS_PER_DAY     86400U
#define SECS_PER_HOUR    3600U
#define SECS_PER_MIN     60U
#define RTC_LSE_RETRY_MAX 250U
#define RTC_LSI_TIMEOUT   1000000U
#define RTC_EPOCH_YEAR_MAX 4000U   /* bound for the epoch-to-year loop */

/* Internal state */
static uint8_t s_rtc_already_running = 0;

static const uint8_t days_in_month[12] = {
    31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
};
static const uint8_t week_table[12] = {
    0, 3, 3, 6, 1, 4, 6, 2, 5, 0, 3, 5
};

/* -----------------------------------------------------------------------
 *  Calendar conversion helpers
 * ----------------------------------------------------------------------- */

/**
 * @brief Test whether a year is a leap year.
 * @param[in] year Gregorian year.
 * @return 1 if leap, 0 otherwise.
 * @req REQ-ROVARI-RTC-0016
 */
uint8_t rtc_is_leap_year(uint16_t year)
{
    if (year % 4 != 0) { return 0; }
    if (year % 100 != 0) { return 1; }
    if (year % 400 == 0) { return 1; }
    return 0;
}

/**
 * @brief Compute the day of week for a date.
 * @param[in] year  Gregorian year.
 * @param[in] month Month 1-12.
 * @param[in] day   Day of month 1-31.
 * @return Day of week (0 = Sunday).
 * @req REQ-ROVARI-RTC-0016
 */
uint8_t rtc_day_of_week(uint16_t year, uint8_t month, uint8_t day)
{
    SEVS_INVARIANT(month >= 1U && month <= 12U);
    uint16_t temp;
    uint8_t  yH = year / 100;
    uint8_t  yL = year % 100;

    if (yH > 19) { yL += 100; }
    temp = yL + yL / 4;
    temp = temp % 7;
    temp = temp + day + week_table[month - 1];
    if (yL % 4 == 0 && month < 3) { temp--; }
    return (uint8_t)(temp % 7);
}

/**
 * @brief Convert a datetime to a Unix epoch.
 * @param[in] year  Year >= 1970.
 * @param[in] month Month 1-12.
 * @param[in] day   Day 1-31.
 * @param[in] hour  Hour 0-23.
 * @param[in] min   Minute 0-59.
 * @param[in] sec   Second 0-59.
 * @return Seconds since 1970-01-01, or 0 if year < 1970.
 * @req REQ-ROVARI-RTC-0016
 */
uint32_t rtc_datetime_to_epoch(uint16_t year, uint8_t month, uint8_t day,
                               uint8_t hour, uint8_t min, uint8_t sec)
{
    SEVS_INVARIANT(month >= 1U && month <= 12U);
    uint32_t secs = 0;

    if (year < 1970) { return 0; }

    for (uint16_t y = 1970; y < year; y++) {
        secs += rtc_is_leap_year(y) ? 31622400U : 31536000U;
    }

    for (uint8_t m = 0; m < month - 1; m++) {
        secs += (uint32_t)days_in_month[m] * SECS_PER_DAY;
        if (rtc_is_leap_year(year) && m == 1) {
            secs += SECS_PER_DAY;  /* Feb 29 */
        }
    }

    secs += (uint32_t)(day - 1) * SECS_PER_DAY;
    secs += (uint32_t)hour * SECS_PER_HOUR;
    secs += (uint32_t)min * SECS_PER_MIN;
    secs += sec;

    return secs;
}

/**
 * @brief Convert a Unix epoch into a calendar datetime.
 * @param[in]  epoch Seconds since 1970-01-01.
 * @param[out] dt    Receives the decomposed datetime.
 * @req REQ-ROVARI-RTC-0016
 * @req REQ-ROVARI-RTC-0021
 */
void rtc_epoch_to_datetime(uint32_t epoch, RtcDateTime *dt)
{
    SEVS_REQUIRE_NOT_NULL(dt);
    uint32_t days = epoch / SECS_PER_DAY;
    uint32_t rem  = epoch % SECS_PER_DAY;

    dt->hour = (uint8_t)(rem / SECS_PER_HOUR);
    rem %= SECS_PER_HOUR;
    dt->min  = (uint8_t)(rem / SECS_PER_MIN);
    dt->sec  = (uint8_t)(rem % SECS_PER_MIN);

    /* Year: bounded by RTC_EPOCH_YEAR_MAX (a 32-bit epoch never exceeds ~2106). */
    uint16_t year = 1970;
    /* @sevs-bound: at most (RTC_EPOCH_YEAR_MAX - 1970) iterations; a uint32
     *              epoch tops out near year 2106, well inside the bound. */
    while (days >= 365 && year < RTC_EPOCH_YEAR_MAX) {
        if (rtc_is_leap_year(year)) {
            if (days >= 366) { days -= 366; } else { break; }
        } else {
            days -= 365;
        }
        year++;
    }
    dt->year = year;

    /* Month: bounded to 12 iterations. */
    uint8_t month = 0;
    /* @sevs-bound: at most 12 iterations (one per month). */
    while (days >= 28 && month < 12) {
        if (rtc_is_leap_year(year) && month == 1) {
            if (days >= 29) { days -= 29; } else { break; }
        } else if (days >= days_in_month[month]) {
            days -= days_in_month[month];
        } else {
            break;
        }
        month++;
    }
    dt->month = (uint8_t)(month + 1);
    dt->day   = (uint8_t)(days + 1);

    dt->weekday = rtc_day_of_week(dt->year, dt->month, dt->day);
}

/* -----------------------------------------------------------------------
 *  Init
 * ----------------------------------------------------------------------- */

/**
 * @brief Initialize the RTC from a clock source.
 *
 * Preserves an already-configured RTC. On fresh configuration sets a
 * default datetime. Clock-ready waits are bounded.
 *
 * @param[in] src Clock source (LSE, LSI, or HSE/128).
 * @return 0 on success, non-zero if the selected clock did not start.
 * @req REQ-ROVARI-RTC-0010
 * @req REQ-ROVARI-RTC-0020
 */
uint8_t rtc_init(RtcClockSource src)
{
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_PWR | RCC_APB1Periph_BKP, ENABLE);
    PWR_BackupAccessCmd(ENABLE);
    RTC_ClearITPendingBit(RTC_IT_ALR);
    RTC_ClearITPendingBit(RTC_IT_SEC);

    if (BKP_ReadBackupRegister(BKP_DR1) == RTC_BKP_MAGIC) {
        s_rtc_already_running = 1;
        RTC_WaitForLastTask();
        RTC_WaitForSynchro();
        RTC_WaitForLastTask();
        return 0;
    }

    s_rtc_already_running = 0;
    BKP_DeInit();

    switch (src) {
    case RTC_CLK_LSE: {
        RCC_LSEConfig(RCC_LSE_ON);
        uint8_t started = 0;
        for (uint32_t retry = 0U; retry < RTC_LSE_RETRY_MAX; retry++) {
            if (RCC_GetFlagStatus(RCC_FLAG_LSERDY) != RESET) {
                started = 1;
                break;
            }
            Delay_Ms(20);
        }
        if (!started) { return 1; }

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
        uint8_t started = 0;
        for (uint32_t i = 0U; i < RTC_LSI_TIMEOUT; i++) {
            if (RCC_GetFlagStatus(RCC_FLAG_LSIRDY) != RESET) {
                started = 1;
                break;
            }
        }
        if (!started) { return 1; }

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

    default:
        return 1;
    }

    /* Default to 2025-01-01 00:00:00 */
    RTC_SetCounter(rtc_datetime_to_epoch(2025, 1, 1, 0, 0, 0));
    RTC_WaitForLastTask();

    BKP_WriteBackupRegister(BKP_DR1, RTC_BKP_MAGIC);
    return 0;
}

/* -----------------------------------------------------------------------
 *  Date and time
 * ----------------------------------------------------------------------- */

/**
 * @brief Set the RTC calendar from a datetime.
 * @param[in] year  Year >= 1970.
 * @param[in] month Month 1-12.
 * @param[in] day   Day 1-31.
 * @param[in] hour  Hour 0-23.
 * @param[in] min   Minute 0-59.
 * @param[in] sec   Second 0-59.
 * @req REQ-ROVARI-RTC-0011
 */
void rtc_set_datetime(uint16_t year, uint8_t month, uint8_t day,
                      uint8_t hour, uint8_t min, uint8_t sec)
{
    SEVS_INVARIANT(month >= 1U && month <= 12U);
    SEVS_INVARIANT(day >= 1U && day <= 31U);
    uint32_t epoch = rtc_datetime_to_epoch(year, month, day, hour, min, sec);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_PWR | RCC_APB1Periph_BKP, ENABLE);
    PWR_BackupAccessCmd(ENABLE);
    RTC_SetCounter(epoch);
    RTC_WaitForLastTask();
}

/**
 * @brief Read the RTC calendar into a datetime struct.
 * @param[out] dt Receives the current datetime.
 * @req REQ-ROVARI-RTC-0011
 * @req REQ-ROVARI-RTC-0021
 */
void rtc_get_datetime(RtcDateTime *dt)
{
    SEVS_REQUIRE_NOT_NULL(dt);
    uint32_t epoch = RTC_GetCounter();
    rtc_epoch_to_datetime(epoch, dt);
}

/**
 * @brief Set only the time of day, preserving the date.
 * @param[in] h Hour 0-23.
 * @param[in] m Minute 0-59.
 * @param[in] s Second 0-59.
 * @req REQ-ROVARI-RTC-0012
 */
void rtc_set_time(uint8_t h, uint8_t m, uint8_t s)
{
    SEVS_INVARIANT(h < 24U && m < 60U && s < 60U);
    RtcDateTime dt;
    rtc_get_datetime(&dt);
    rtc_set_datetime(dt.year, dt.month, dt.day, h, m, s);
}

/**
 * @brief Read the current time of day.
 * @param[out] h Receives the hour.
 * @param[out] m Receives the minute.
 * @param[out] s Receives the second.
 * @req REQ-ROVARI-RTC-0012
 * @req REQ-ROVARI-RTC-0021
 */
void rtc_get_time(uint8_t *h, uint8_t *m, uint8_t *s)
{
    SEVS_REQUIRE_NOT_NULL(h);
    SEVS_REQUIRE_NOT_NULL(m);
    SEVS_REQUIRE_NOT_NULL(s);
    RtcDateTime dt;
    rtc_get_datetime(&dt);
    *h = dt.hour;
    *m = dt.min;
    *s = dt.sec;
}

/* -----------------------------------------------------------------------
 *  Raw epoch
 * ----------------------------------------------------------------------- */

/**
 * @brief Read the raw seconds counter.
 * @return Seconds since 1970-01-01.
 * @req REQ-ROVARI-RTC-0013
 */
uint32_t rtc_get_epoch(void)
{
    return RTC_GetCounter();
}

/**
 * @brief Set the raw seconds counter.
 * @param[in] seconds Seconds since 1970-01-01.
 * @req REQ-ROVARI-RTC-0013
 */
void rtc_set_epoch(uint32_t seconds)
{
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_PWR | RCC_APB1Periph_BKP, ENABLE);
    PWR_BackupAccessCmd(ENABLE);
    RTC_SetCounter(seconds);
    RTC_WaitForLastTask();
}

/* -----------------------------------------------------------------------
 *  Alarm
 * ----------------------------------------------------------------------- */

/**
 * @brief Set an alarm at a specific datetime.
 * @param[in] year  Year.
 * @param[in] month Month 1-12.
 * @param[in] day   Day 1-31.
 * @param[in] hour  Hour 0-23.
 * @param[in] min   Minute 0-59.
 * @param[in] sec   Second 0-59.
 * @req REQ-ROVARI-RTC-0014
 */
void rtc_set_alarm_datetime(uint16_t year, uint8_t month, uint8_t day,
                            uint8_t hour, uint8_t min, uint8_t sec)
{
    SEVS_INVARIANT(month >= 1U && month <= 12U);
    uint32_t epoch = rtc_datetime_to_epoch(year, month, day, hour, min, sec);
    rtc_set_alarm_epoch(epoch);
}

/**
 * @brief Set an alarm at h:m:s today (using the current date).
 * @param[in] h Hour 0-23.
 * @param[in] m Minute 0-59.
 * @param[in] s Second 0-59.
 * @req REQ-ROVARI-RTC-0014
 */
void rtc_set_alarm(uint8_t h, uint8_t m, uint8_t s)
{
    SEVS_INVARIANT(h < 24U && m < 60U && s < 60U);
    RtcDateTime dt;
    rtc_get_datetime(&dt);
    uint32_t epoch = rtc_datetime_to_epoch(dt.year, dt.month, dt.day, h, m, s);
    rtc_set_alarm_epoch(epoch);
}

/**
 * @brief Set an alarm at a raw epoch.
 * @param[in] seconds Alarm time in seconds since 1970-01-01.
 * @req REQ-ROVARI-RTC-0014
 */
void rtc_set_alarm_epoch(uint32_t seconds)
{
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_PWR | RCC_APB1Periph_BKP, ENABLE);
    PWR_BackupAccessCmd(ENABLE);
    RTC_SetAlarm(seconds);
    RTC_WaitForLastTask();
    RTC_ClearFlag(RTC_FLAG_ALR);
}

/**
 * @brief Report and clear the alarm-fired flag.
 * @return 1 if the alarm had fired (flag cleared), 0 otherwise.
 * @req REQ-ROVARI-RTC-0014
 */
uint8_t rtc_alarm_fired(void)
{
    if (RTC_GetFlagStatus(RTC_FLAG_ALR) != RESET) {
        RTC_ClearFlag(RTC_FLAG_ALR);
        return 1;
    }
    return 0;
}

/* -----------------------------------------------------------------------
 *  Calibration
 * ----------------------------------------------------------------------- */

/**
 * @brief Set the 7-bit RTC calibration value.
 * @param[in] value Calibration value (0-127; clamped).
 * @req REQ-ROVARI-RTC-0015
 */
void rtc_set_calibration(uint8_t value)
{
    if (value > 127) { value = 127; }

    RCC_APB1PeriphClockCmd(RCC_APB1Periph_PWR | RCC_APB1Periph_BKP, ENABLE);
    PWR_BackupAccessCmd(ENABLE);

    uint16_t reg = BKP->OCTLR;
    reg &= ~((uint16_t)0x007F);
    reg |= (uint16_t)(value & 0x7F);
    BKP->OCTLR = reg;
}

/**
 * @brief Read the 7-bit RTC calibration value.
 * @return Calibration value 0-127.
 * @req REQ-ROVARI-RTC-0015
 */
uint8_t rtc_get_calibration(void)
{
    return (uint8_t)(BKP->OCTLR & 0x7F);
}

/* -----------------------------------------------------------------------
 *  Info
 * ----------------------------------------------------------------------- */

/**
 * @brief Report whether the RTC was already running at init.
 * @return 1 if it was preserved from a prior configuration, 0 if fresh.
 * @req REQ-ROVARI-RTC-0010
 */
uint8_t rtc_was_running(void)
{
    return s_rtc_already_running;
}
