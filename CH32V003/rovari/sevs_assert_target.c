/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Armstrong Subero
 */
/**
 * @file sevs_assert_target.c
 * @brief SEVS assertion handler for CH32V003.
 * On failure: writes crash record to top of SRAM, then halts.
 * Crash record address: 0x20000700 (top 256 bytes of 2K SRAM).
 */
#include "sevs_runtime.h"
#include <stdint.h>
#include <string.h>

#ifndef SEVS_CRASH_RECORD_ADDR
#define SEVS_CRASH_RECORD_ADDR 0x20000700U
#endif

typedef struct {
    uint32_t magic;
    uint32_t kind;
    uint32_t line;
    char     file[32];
    char     func[32];
    char     expr[32];
} sevs_crash_record_t;

#define SEVS_CRASH_MAGIC 0x53455653U  /* "SEVS" */

void sevs_assert_fail(sevs_fail_kind_t kind,
                      const char       *expr,
                      const char       *file,
                      int               line,
                      const char       *func)
{
    /* Disable interrupts */
    __asm volatile("csrc mstatus, %0" :: "r"(0x88));

    volatile sevs_crash_record_t* rec =
        (volatile sevs_crash_record_t*)SEVS_CRASH_RECORD_ADDR;

    rec->magic = SEVS_CRASH_MAGIC;
    rec->kind  = (uint32_t)kind;
    rec->line  = (uint32_t)line;

    /* Safe copy (truncate to fit) */
    if (file) {
        strncpy((char*)rec->file, file, sizeof(rec->file) - 1);
        ((char*)rec->file)[sizeof(rec->file) - 1] = '\0';
    }
    if (func) {
        strncpy((char*)rec->func, func, sizeof(rec->func) - 1);
        ((char*)rec->func)[sizeof(rec->func) - 1] = '\0';
    }
    if (expr) {
        strncpy((char*)rec->expr, expr, sizeof(rec->expr) - 1);
        ((char*)rec->expr)[sizeof(rec->expr) - 1] = '\0';
    }

    /* Halt: infinite loop with interrupts disabled */
    while (1) {
        __asm volatile("nop");
    }
}
