/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Rovari - rvembedded.com
 * rovari_capture.h - Input capture for CH32V003 (TIM1 only)
 */
#ifndef ROVARI_CAPTURE_H
#define ROVARI_CAPTURE_H
#include "rovari_defs.h"
typedef void (*CaptureCallback)(uint16_t value);
#ifdef __cplusplus
extern "C" {
#endif
void capture_init(pin_t pin, uint16_t prescaler, CaptureCallback callback);
void capture_stop(pin_t pin);
#ifdef __cplusplus
}
#endif
#endif
