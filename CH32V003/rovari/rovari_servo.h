/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Rovari - rvembedded.com
 *
 * rovari_servo.h - Servo motor control for CH32V003
 *
 * Uses TIM1 at 50 Hz with PSC=47 for exact 1us tick resolution.
 * Supports up to 2 servos (PD2 = CH1, PC4 = CH4).
 *
 * Features:
 *   - Angle and microsecond pulse control
 *   - Quintic S-curve smooth motion (zero jerk at endpoints)
 *   - Trapezoidal accel/coast/decel sweep
 *   - Safe angle clamping (avoids hard end-stops)
 *   - Current position readback from timer register
 *
 * C usage:
 *   servo_init(PD2);
 *   servo_write_deg(PD2, 90);
 *   servo_move_to(PD2, 180, 1000, 10);
 *
 * C++ usage:
 *   Servo s(PD2);
 *   s.writeDeg(90);
 *   s.moveTo(180, 1000, 10);
 *
 * Note: TIM1 is configured for servo use (PSC=47, ARR=19999).
 *       This conflicts with pwm_init(). Do not mix servo and
 *       PWM on the same timer.
 */

#ifndef ROVARI_SERVO_H
#define ROVARI_SERVO_H

#include "rovari_defs.h"

/* Servo pulse limits (microseconds) */
#define SERVO_MIN_US        500
#define SERVO_MAX_US        2500
#define SERVO_CENTER_US     ((SERVO_MIN_US + SERVO_MAX_US) / 2)

/* Safe angle range to avoid mechanical end-stop damage */
#define SERVO_SAFE_MIN_DEG  5
#define SERVO_SAFE_MAX_DEG  175

/* Maximum number of simultaneous servos */
#define SERVO_MAX           2

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize a pin for servo output.
 * Configures TIM1 at 50 Hz with 1us resolution on first call.
 * Parks the servo at center (1500us).
 *
 * @param[in] pin  PD2 (TIM1 CH1) or PC4 (TIM1 CH4)
 */
void servo_init(pin_t pin);

/**
 * Set servo position by pulse width in microseconds.
 * Clamped to [SERVO_MIN_US .. SERVO_MAX_US].
 */
void servo_write_us(pin_t pin, uint16_t us);

/**
 * Set servo position by angle in degrees (0 to 180).
 * Maps through safe clamping range.
 */
void servo_write_deg(pin_t pin, uint16_t deg);

/**
 * Read the current servo angle from the timer register.
 * @return  Current angle in degrees (0 to 180).
 */
uint16_t servo_read_deg(pin_t pin);

/**
 * Simple linear sweep between two angles.
 * Blocking. Steps at 1 degree increments.
 *
 * @param[in] start_deg    Start angle (0 to 180)
 * @param[in] end_deg      End angle (0 to 180)
 * @param[in] duration_ms  Total sweep time
 */
void servo_sweep(pin_t pin, uint16_t start_deg, uint16_t end_deg,
                 uint32_t duration_ms);

/**
 * Trapezoidal sweep: accelerate, coast, decelerate.
 * Blocking. Smoother than linear sweep.
 *
 * @param[in] start_deg    Start angle (0 to 180)
 * @param[in] end_deg      End angle (0 to 180)
 * @param[in] duration_ms  Total sweep time
 */
void servo_sweep_trapezoid(pin_t pin, uint16_t start_deg, uint16_t end_deg,
                           uint32_t duration_ms);

/**
 * S-curve quintic sweep between two angles.
 * Blocking. Zero jerk at start and end for silky motion.
 *
 * @param[in] start_deg    Start angle (0 to 180)
 * @param[in] end_deg      End angle (0 to 180)
 * @param[in] duration_ms  Total motion time
 * @param[in] tick_ms      Control interval (10 is typical)
 */
void servo_sweep_scurve(pin_t pin, uint16_t start_deg, uint16_t end_deg,
                        uint32_t duration_ms, uint16_t tick_ms);

/**
 * S-curve move from current position to target angle.
 * Reads current angle from timer register, then sweeps.
 * Blocking.
 *
 * @param[in] target_deg   Target angle (0 to 180)
 * @param[in] duration_ms  Total motion time
 * @param[in] tick_ms      Control interval (10 is typical)
 */
void servo_move_to(pin_t pin, uint16_t target_deg,
                   uint32_t duration_ms, uint16_t tick_ms);

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus

class Servo {
public:
    Servo(pin_t pin) : _pin(pin)
    {
        servo_init(pin);
    }

    void writeUs(uint16_t us)         { servo_write_us(_pin, us); }
    void writeDeg(uint16_t deg)       { servo_write_deg(_pin, deg); }
    uint16_t readDeg()                { return servo_read_deg(_pin); }

    void sweep(uint16_t start, uint16_t end, uint32_t duration_ms)
    {
        servo_sweep(_pin, start, end, duration_ms);
    }

    void sweepTrapezoid(uint16_t start, uint16_t end, uint32_t duration_ms)
    {
        servo_sweep_trapezoid(_pin, start, end, duration_ms);
    }

    void sweepSmooth(uint16_t start, uint16_t end,
                     uint32_t duration_ms, uint16_t tick_ms)
    {
        servo_sweep_scurve(_pin, start, end, duration_ms, tick_ms);
    }

    void moveTo(uint16_t target, uint32_t duration_ms, uint16_t tick_ms)
    {
        servo_move_to(_pin, target, duration_ms, tick_ms);
    }

private:
    pin_t _pin;
};

#endif

#endif /* ROVARI_SERVO_H */
