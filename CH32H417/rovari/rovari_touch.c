/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Rovari - rvembedded.com
 *
 * rovari_touch.c - Weak default stubs for common touch interface
 *
 * These are overridden by the real touch driver (FT6336U, XPT2046, etc.)
 * when one is linked. If no touch driver is in the build, these no-op
 * stubs prevent linker errors from display drivers calling
 * touch_set_rotation().
 */

#include "rovari_touch.h"

__attribute__((weak))
void touch_set_rotation(uint8_t rotation)
{
    (void)rotation;
}
