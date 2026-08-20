/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Rovari - rvembedded.com
 *
 * rovari_tone.h - Tone generation for CH32V003
 *
 * Drives a piezo buzzer or speaker using PWM at 50% duty cycle.
 * Output pin must be a TIM1 PWM channel (PD2, PA1, PC3, PC4).
 *
 * C usage:
 *   tone_play(PD2, NOTE_A4);           // 440 Hz continuous
 *   tone_play_ms(PD2, NOTE_C5, 500);   // 523 Hz for 500ms
 *   tone_update();                      // call in app_run
 *   tone_stop(PD2);                     // silence
 *
 * C++ usage:
 *   Tone buzzer(PD2);
 *   buzzer.play(NOTE_A4);
 *   buzzer.play(NOTE_C5, 500);
 *   buzzer.update();
 *   buzzer.stop();
 */

#ifndef ROVARI_TONE_H
#define ROVARI_TONE_H

#include "rovari_defs.h"

/* -----------------------------------------------------------------------
 *  Standard note frequencies (Hz), octaves 3 through 7
 *  Rest (silence) is frequency 0.
 * ----------------------------------------------------------------------- */
#define REST       0

/* Octave 3 */
#define NOTE_C3    131
#define NOTE_CS3   139
#define NOTE_D3    147
#define NOTE_DS3   156
#define NOTE_E3    165
#define NOTE_F3    175
#define NOTE_FS3   185
#define NOTE_G3    196
#define NOTE_GS3   208
#define NOTE_A3    220
#define NOTE_AS3   233
#define NOTE_B3    247

/* Octave 4 (middle) */
#define NOTE_C4    262
#define NOTE_CS4   277
#define NOTE_D4    294
#define NOTE_DS4   311
#define NOTE_E4    330
#define NOTE_F4    349
#define NOTE_FS4   370
#define NOTE_G4    392
#define NOTE_GS4   415
#define NOTE_A4    440
#define NOTE_AS4   466
#define NOTE_B4    494

/* Octave 5 */
#define NOTE_C5    523
#define NOTE_CS5   554
#define NOTE_D5    587
#define NOTE_DS5   622
#define NOTE_E5    659
#define NOTE_F5    698
#define NOTE_FS5   740
#define NOTE_G5    784
#define NOTE_GS5   831
#define NOTE_A5    880
#define NOTE_AS5   932
#define NOTE_B5    988

/* Octave 6 */
#define NOTE_C6    1047
#define NOTE_CS6   1109
#define NOTE_D6    1175
#define NOTE_DS6   1245
#define NOTE_E6    1319
#define NOTE_F6    1397
#define NOTE_FS6   1480
#define NOTE_G6    1568
#define NOTE_GS6   1661
#define NOTE_A6    1760
#define NOTE_AS6   1865
#define NOTE_B6    1976

/* Octave 7 */
#define NOTE_C7    2093
#define NOTE_CS7   2217
#define NOTE_D7    2349
#define NOTE_DS7   2489
#define NOTE_E7    2637
#define NOTE_F7    2794
#define NOTE_FS7   2960
#define NOTE_G7    3136
#define NOTE_GS7   3322
#define NOTE_A7    3520
#define NOTE_AS7   3729
#define NOTE_B7    3951

/* -----------------------------------------------------------------------
 *  C API
 * ----------------------------------------------------------------------- */
#ifdef __cplusplus
extern "C" {
#endif

/**
 * Start a continuous tone at the given frequency.
 * Pass REST (0) to silence.
 */
void tone_play(pin_t pin, uint16_t freq_hz);

/**
 * Play a tone for a duration in milliseconds.
 * Non-blocking: returns immediately, call tone_update()
 * in app_run() to auto-stop when the duration expires.
 */
void tone_play_ms(pin_t pin, uint16_t freq_hz, uint16_t duration_ms);

/**
 * Stop the tone on a pin immediately.
 */
void tone_stop(pin_t pin);

/**
 * Update duration tracking. Call once per app_run() iteration.
 * Stops tones whose duration has expired.
 */
void tone_update(void);

/**
 * Returns 1 if a timed tone is still playing.
 */
uint8_t tone_is_playing(pin_t pin);

/* -----------------------------------------------------------------------
 *  Built-in effects (blocking, bounded per SEVS)
 * ----------------------------------------------------------------------- */

/** Single short click for button/touch feedback. 5ms at 4 kHz. */
void tone_click(pin_t pin);

/** Quick beeps for UI feedback. 100ms on, 80ms off.
 *  @param count  Number of beeps (clamped to 10) */
void tone_beep(pin_t pin, uint8_t count);

/** Success: happy rising two-tone. Like a checkout scanner. */
void tone_success(pin_t pin);

/** Error: harsh descending tone. Wrong answer buzzer. */
void tone_error(pin_t pin);

/** Warning: three rapid beeps at a sharp frequency. */
void tone_warning(pin_t pin);

/** Startup chime: ascending three-note sequence. Power on. */
void tone_startup(pin_t pin);

/** Shutdown chime: descending three-note sequence. Power off. */
void tone_shutdown(pin_t pin);

/** Doorbell: classic ding-dong two-note. */
void tone_doorbell(pin_t pin);

/** Notification: gentle two-note ping. */
void tone_notification(pin_t pin);

/** Alarm: urgent rapid high-pitched intermittent beeping.
 *  @param cycles  Number of on/off cycles (clamped to 20) */
void tone_alarm(pin_t pin, uint8_t cycles);

/** Ring: phone-style ring pattern. Ring for 1s, silence 1s.
 *  @param count  Number of rings (clamped to 10) */
void tone_ring(pin_t pin, uint8_t count);

/** Countdown: (count-1) short beeps then one long beep.
 *  @param count  Total beeps including final (clamped to 10) */
void tone_countdown(pin_t pin, uint16_t freq_hz, uint8_t count);

/** Chirp: very short rising sweep. Bird/sensor feedback. */
void tone_chirp(pin_t pin);

/** Siren: sweep low to high and back in one cycle.
 *  @param duration_ms  Total time for one full sweep cycle */
void tone_siren(pin_t pin, uint16_t low_hz, uint16_t high_hz, uint16_t duration_ms);

/** Frequency sweep in one direction.
 *  @param duration_ms  Total sweep time */
void tone_sweep(pin_t pin, uint16_t start_hz, uint16_t end_hz, uint16_t duration_ms);

/** Two-tone alternating alert. 200ms per tone.
 *  @param count  Number of A/B cycles (clamped to 20) */
void tone_alert(pin_t pin, uint16_t freq_a, uint16_t freq_b, uint8_t count);

/** Trill: rapid alternation between two close frequencies.
 *  Like a phone dial tone or cricket.
 *  @param duration_ms  Total trill time */
void tone_trill(pin_t pin, uint16_t freq_a, uint16_t freq_b, uint16_t duration_ms);

/** Play a melody from note and duration arrays.
 *  Blocking. Plays all notes in sequence with a 30ms gap.
 *  @param notes      Array of frequencies (use NOTE_xx defines, REST for silence)
 *  @param durations  Array of durations in milliseconds
 *  @param count      Number of notes (clamped to 64) */
void tone_melody(pin_t pin, const uint16_t* notes, const uint16_t* durations, uint8_t count);

#ifdef __cplusplus
}
#endif

/* -----------------------------------------------------------------------
 *  C++ API
 * ----------------------------------------------------------------------- */
#ifdef __cplusplus

class Tone {
public:
    Tone(pin_t pin) : _pin(pin) {}

    void play(uint16_t freq)
    {
        tone_play(_pin, freq);
    }

    void play(uint16_t freq, uint16_t duration_ms)
    {
        tone_play_ms(_pin, freq, duration_ms);
    }

    void stop()
    {
        tone_stop(_pin);
    }

    void update()
    {
        tone_update();
    }

    bool isPlaying()
    {
        return tone_is_playing(_pin) != 0;
    }

    void click()                    { tone_click(_pin); }
    void beep(uint8_t count = 1)    { tone_beep(_pin, count); }
    void success()                  { tone_success(_pin); }
    void error()                    { tone_error(_pin); }
    void warning()                  { tone_warning(_pin); }
    void startup()                  { tone_startup(_pin); }
    void shutdown()                 { tone_shutdown(_pin); }
    void doorbell()                 { tone_doorbell(_pin); }
    void notification()             { tone_notification(_pin); }
    void alarm(uint8_t cycles)      { tone_alarm(_pin, cycles); }
    void ring(uint8_t count)        { tone_ring(_pin, count); }
    void chirp()                    { tone_chirp(_pin); }

    void countdown(uint16_t freq, uint8_t count)
    {
        tone_countdown(_pin, freq, count);
    }

    void siren(uint16_t low, uint16_t high, uint16_t duration_ms)
    {
        tone_siren(_pin, low, high, duration_ms);
    }

    void sweep(uint16_t start, uint16_t end, uint16_t duration_ms)
    {
        tone_sweep(_pin, start, end, duration_ms);
    }

    void alert(uint16_t freq_a, uint16_t freq_b, uint8_t count)
    {
        tone_alert(_pin, freq_a, freq_b, count);
    }

    void trill(uint16_t freq_a, uint16_t freq_b, uint16_t duration_ms)
    {
        tone_trill(_pin, freq_a, freq_b, duration_ms);
    }

    void playMelody(const uint16_t* notes, const uint16_t* durations, uint8_t count)
    {
        tone_melody(_pin, notes, durations, count);
    }

private:
    pin_t _pin;
};

#endif

#endif /* ROVARI_TONE_H */
