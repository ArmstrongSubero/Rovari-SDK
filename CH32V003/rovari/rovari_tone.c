/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Rovari - rvembedded.com
 */

/**
 * @file rovari_tone.c
 * @brief Tone generation for CH32V003.
 *
 * Uses TIM1 PWM at 50% duty to produce square waves on a buzzer.
 * Duration tracking is non-blocking via millis() and tone_update().
 *
 * Limitation: TIM1 is shared across all PWM channels. Changing
 * the frequency on one pin affects the base frequency for all
 * TIM1 channels. One tone at a time in practice.
 *
 * @req REQ-ROVARI-TONE-0010
 */

#include <stdint.h>
#include "debug.h"
#include "rovari_tone.h"
#include "rovari_pwm.h"

extern uint32_t millis(void);

/* Duration tracking for one active timed tone */
static pin_t    s_pin       = 0;
static uint8_t  s_active    = 0;
static uint32_t s_start_ms  = 0;
static uint16_t s_duration  = 0;

/**
 * @brief Start a continuous tone at the given frequency.
 *
 * Internally calls pwm_init() to set the timer frequency,
 * then pwm_write_pct() at 50% for a square wave.
 * Pass REST (0) to silence the pin.
 *
 * @param[in] pin     PWM capable pin (PD2, PA1, PC3, PC4)
 * @param[in] freq_hz Tone frequency in Hz, or 0 for silence
 * @req REQ-ROVARI-TONE-0010
 */
void tone_play(pin_t pin, uint16_t freq_hz)
{
    if (freq_hz == 0)
    {
        pwm_stop(pin);
        return;
    }

    pwm_init(pin, (uint32_t)freq_hz);
    pwm_write_pct(pin, 50);
}

/**
 * @brief Play a tone for a specified duration.
 *
 * Non-blocking: returns immediately. Call tone_update()
 * in app_run() to auto-stop when the time expires.
 *
 * @param[in] pin         PWM capable pin
 * @param[in] freq_hz     Tone frequency in Hz, or 0 for rest
 * @param[in] duration_ms How long to play in milliseconds
 * @req REQ-ROVARI-TONE-0011
 */
void tone_play_ms(pin_t pin, uint16_t freq_hz, uint16_t duration_ms)
{
    if (freq_hz == 0)
    {
        /* Rest: just silence and track the duration for timing */
        pwm_stop(pin);
    }
    else
    {
        pwm_init(pin, (uint32_t)freq_hz);
        pwm_write_pct(pin, 50);
    }

    s_pin      = pin;
    s_active   = 1;
    s_start_ms = millis();
    s_duration = duration_ms;
}

/**
 * @brief Stop the tone immediately.
 *
 * @param[in] pin PWM capable pin
 * @req REQ-ROVARI-TONE-0012
 */
void tone_stop(pin_t pin)
{
    pwm_stop(pin);

    if (s_active && s_pin == pin)
    {
        s_active = 0;
    }
}

/**
 * @brief Update duration tracking. Call once per app_run() iteration.
 *
 * Checks if the timed tone has expired and stops it automatically.
 * Safe to call even when no tone is playing.
 *
 * @req REQ-ROVARI-TONE-0013
 */
void tone_update(void)
{
    if (!s_active)
    {
        return;
    }

    uint32_t elapsed = millis() - s_start_ms;
    if (elapsed >= s_duration)
    {
        pwm_stop(s_pin);
        s_active = 0;
    }
}

/**
 * @brief Check if a timed tone is still playing.
 *
 * @param[in] pin PWM capable pin
 * @return 1 if a timed tone is active on this pin, 0 otherwise
 * @req REQ-ROVARI-TONE-0014
 */
uint8_t tone_is_playing(pin_t pin)
{
    if (s_active && s_pin == pin)
    {
        tone_update();
        return s_active;
    }
    return 0;
}

/* -----------------------------------------------------------------------
 *  Built-in effects
 *  All blocking with bounded loops per SEVS.
 *  Uses Delay_Ms from WCH HAL for timing.
 * ----------------------------------------------------------------------- */

/**
 * @brief Single short click for tactile feedback.
 * @req REQ-ROVARI-TONE-0020
 */
void tone_click(pin_t pin)
{
    tone_play(pin, 4000);
    Delay_Ms(5);
    tone_stop(pin);
}

/**
 * @brief Quick UI beeps.
 * @req REQ-ROVARI-TONE-0021
 */
void tone_beep(pin_t pin, uint8_t count)
{
    if (count > 10) { count = 10; }

    for (uint8_t i = 0; i < count; i++)
    {
        tone_play(pin, NOTE_A5);
        Delay_Ms(100);
        tone_stop(pin);
        Delay_Ms(80);
    }
}

/**
 * @brief Success: rising two-tone. Checkout scanner feel.
 * @req REQ-ROVARI-TONE-0022
 */
void tone_success(pin_t pin)
{
    tone_play(pin, NOTE_E5);
    Delay_Ms(100);
    tone_play(pin, NOTE_A5);
    Delay_Ms(200);
    tone_stop(pin);
}

/**
 * @brief Error: descending harsh buzz.
 * @req REQ-ROVARI-TONE-0023
 */
void tone_error(pin_t pin)
{
    tone_play(pin, NOTE_A4);
    Delay_Ms(150);
    tone_play(pin, NOTE_E4);
    Delay_Ms(150);
    tone_play(pin, NOTE_C4);
    Delay_Ms(300);
    tone_stop(pin);
}

/**
 * @brief Warning: three rapid sharp beeps.
 * @req REQ-ROVARI-TONE-0024
 */
void tone_warning(pin_t pin)
{
    for (uint8_t i = 0; i < 3; i++)
    {
        tone_play(pin, NOTE_E6);
        Delay_Ms(80);
        tone_stop(pin);
        Delay_Ms(60);
    }
}

/**
 * @brief Startup chime: ascending three notes.
 * @req REQ-ROVARI-TONE-0025
 */
void tone_startup(pin_t pin)
{
    tone_play(pin, NOTE_C5);
    Delay_Ms(120);
    tone_play(pin, NOTE_E5);
    Delay_Ms(120);
    tone_play(pin, NOTE_G5);
    Delay_Ms(200);
    tone_stop(pin);
}

/**
 * @brief Shutdown chime: descending three notes.
 * @req REQ-ROVARI-TONE-0026
 */
void tone_shutdown(pin_t pin)
{
    tone_play(pin, NOTE_G5);
    Delay_Ms(120);
    tone_play(pin, NOTE_E5);
    Delay_Ms(120);
    tone_play(pin, NOTE_C5);
    Delay_Ms(200);
    tone_stop(pin);
}

/**
 * @brief Doorbell: classic ding-dong.
 * @req REQ-ROVARI-TONE-0027
 */
void tone_doorbell(pin_t pin)
{
    tone_play(pin, NOTE_E5);
    Delay_Ms(300);
    tone_play(pin, NOTE_C5);
    Delay_Ms(400);
    tone_stop(pin);
}

/**
 * @brief Notification: gentle two-note ping.
 * @req REQ-ROVARI-TONE-0028
 */
void tone_notification(pin_t pin)
{
    tone_play(pin, NOTE_G5);
    Delay_Ms(80);
    tone_stop(pin);
    Delay_Ms(40);
    tone_play(pin, NOTE_B5);
    Delay_Ms(150);
    tone_stop(pin);
}

/**
 * @brief Alarm: urgent rapid high-pitched beeping.
 * @req REQ-ROVARI-TONE-0029
 */
void tone_alarm(pin_t pin, uint8_t cycles)
{
    if (cycles > 20) { cycles = 20; }

    for (uint8_t i = 0; i < cycles; i++)
    {
        tone_play(pin, NOTE_A6);
        Delay_Ms(100);
        tone_stop(pin);
        Delay_Ms(100);
    }
}

/**
 * @brief Ring: phone-style ring pattern.
 * @req REQ-ROVARI-TONE-0030
 */
void tone_ring(pin_t pin, uint8_t count)
{
    if (count > 10) { count = 10; }

    for (uint8_t i = 0; i < count; i++)
    {
        /* Ring burst: two quick trills */
        for (uint8_t j = 0; j < 10; j++)
        {
            tone_play(pin, NOTE_A5);
            Delay_Ms(25);
            tone_play(pin, NOTE_E5);
            Delay_Ms(25);
        }
        tone_stop(pin);
        Delay_Ms(500);

        for (uint8_t j = 0; j < 10; j++)
        {
            tone_play(pin, NOTE_A5);
            Delay_Ms(25);
            tone_play(pin, NOTE_E5);
            Delay_Ms(25);
        }
        tone_stop(pin);
        Delay_Ms(1000);
    }
}

/**
 * @brief Countdown: short beeps then one long final beep.
 * @req REQ-ROVARI-TONE-0031
 */
void tone_countdown(pin_t pin, uint16_t freq_hz, uint8_t count)
{
    if (count > 10) { count = 10; }
    if (count == 0) { return; }

    for (uint8_t i = 0; i < count - 1; i++)
    {
        tone_play(pin, freq_hz);
        Delay_Ms(150);
        tone_stop(pin);
        Delay_Ms(350);
    }

    tone_play(pin, freq_hz);
    Delay_Ms(500);
    tone_stop(pin);
}

/**
 * @brief Chirp: very short rising sweep.
 * @req REQ-ROVARI-TONE-0032
 */
void tone_chirp(pin_t pin)
{
    for (uint16_t f = 2000; f <= 4000; f += 200)
    {
        tone_play(pin, f);
        Delay_Ms(5);
    }
    tone_stop(pin);
}

/**
 * @brief Siren: sweep low to high and back.
 * @req REQ-ROVARI-TONE-0033
 */
void tone_siren(pin_t pin, uint16_t low_hz, uint16_t high_hz, uint16_t duration_ms)
{
    if (low_hz >= high_hz) { return; }

    uint16_t steps = 40;
    uint16_t step_ms = duration_ms / (steps * 2);
    if (step_ms == 0) { step_ms = 1; }

    uint16_t range = high_hz - low_hz;

    for (uint16_t i = 0; i <= steps; i++)
    {
        uint16_t freq = low_hz + (uint16_t)(((uint32_t)range * i) / steps);
        tone_play(pin, freq);
        Delay_Ms(step_ms);
    }

    for (uint16_t i = steps; i > 0; i--)
    {
        uint16_t freq = low_hz + (uint16_t)(((uint32_t)range * i) / steps);
        tone_play(pin, freq);
        Delay_Ms(step_ms);
    }

    tone_stop(pin);
}

/**
 * @brief Frequency sweep in one direction.
 * @req REQ-ROVARI-TONE-0034
 */
void tone_sweep(pin_t pin, uint16_t start_hz, uint16_t end_hz, uint16_t duration_ms)
{
    uint16_t steps = 40;
    uint16_t step_ms = duration_ms / steps;
    if (step_ms == 0) { step_ms = 1; }

    int32_t range = (int32_t)end_hz - (int32_t)start_hz;

    for (uint16_t i = 0; i <= steps; i++)
    {
        int32_t freq = (int32_t)start_hz + (range * (int32_t)i) / (int32_t)steps;
        if (freq < 20) { freq = 20; }
        tone_play(pin, (uint16_t)freq);
        Delay_Ms(step_ms);
    }

    tone_stop(pin);
}

/**
 * @brief Two-tone alternating alert.
 * @req REQ-ROVARI-TONE-0035
 */
void tone_alert(pin_t pin, uint16_t freq_a, uint16_t freq_b, uint8_t count)
{
    if (count > 20) { count = 20; }

    for (uint8_t i = 0; i < count; i++)
    {
        tone_play(pin, freq_a);
        Delay_Ms(200);
        tone_play(pin, freq_b);
        Delay_Ms(200);
    }

    tone_stop(pin);
}

/**
 * @brief Trill: rapid alternation between two close frequencies.
 * @req REQ-ROVARI-TONE-0036
 */
void tone_trill(pin_t pin, uint16_t freq_a, uint16_t freq_b, uint16_t duration_ms)
{
    uint16_t elapsed = 0;
    uint16_t toggle_ms = 30;

    /* @sevs-bound: bounded by duration_ms */
    while (elapsed < duration_ms)
    {
        tone_play(pin, freq_a);
        Delay_Ms(toggle_ms);
        elapsed += toggle_ms;

        if (elapsed >= duration_ms) { break; }

        tone_play(pin, freq_b);
        Delay_Ms(toggle_ms);
        elapsed += toggle_ms;
    }

    tone_stop(pin);
}

/**
 * @brief Play a melody from arrays of notes and durations.
 * @req REQ-ROVARI-TONE-0037
 */
void tone_melody(pin_t pin, const uint16_t* notes, const uint16_t* durations, uint8_t count)
{
    if (notes == 0 || durations == 0) { return; }
    if (count > 64) { count = 64; }

    for (uint8_t i = 0; i < count; i++)
    {
        if (notes[i] == REST)
        {
            tone_stop(pin);
        }
        else
        {
            tone_play(pin, notes[i]);
        }
        Delay_Ms(durations[i]);
        tone_stop(pin);
        Delay_Ms(30);
    }
}
