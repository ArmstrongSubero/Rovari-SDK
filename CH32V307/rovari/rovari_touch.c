/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Rovari - rvembedded.com
 */

/**
 * @file rovari_touch.c
 * @brief Weak default stubs for the common touch interface.
 *
 * Overridden by a concrete touch driver (FT6336U, XPT2046, ...) when one
 * is linked. With no touch driver in the build, these no-op stubs prevent
 * linker errors from display drivers calling touch_set_rotation().
 */

#include <stdint.h>
#include "rovari_touch.h"

/**
 * @brief Weak no-op rotation hook; replaced by a real touch driver.
 * @param[in] rotation Display rotation (0-3); ignored by the stub.
 * @req REQ-ROVARI-TOUCH-0010
 */
void __attribute__((weak)) touch_set_rotation(uint8_t rotation)
{
    (void)rotation;
}
