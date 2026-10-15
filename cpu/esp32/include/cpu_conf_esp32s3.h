/*
 * SPDX-FileCopyrightText: 2022 Gunar Schorcht
 * SPDX-License-Identifier: LGPL-2.1-only
 */

#pragma once

/**
 * @ingroup     cpu_esp32
 * @ingroup     config
 * @brief       Compile-time configuration macros for ESP32-S3 SoCs
 * @{
 *
 * @file
 * @brief       ESP32-S3 specific compile-time configuration
 *
 * @author      Gunar Schorcht <gunar@schorcht.net>
 */

#ifndef ESP_ISR_STACKSIZE
/** Stack size used in interrupt context */
#  define ESP_ISR_STACKSIZE             (THREAD_STACKSIZE_DEFAULT)
#endif /* ESP_ISR_STACKSIZE */

/**
 * @brief   Additional stack size required to save the thread context
 *
 * In addition to the FPU (CP0), the ESP32-S3 has the TIE coprocessor (CP3)
 * whose 208 byte state has to be saved on the stack of each thread. Small
 * stacks are increased accordingly.
 */
#define ESP_THREAD_EXTRA_STACKSIZE_CP   (256)

#ifndef THREAD_STACKSIZE_SMALL
/** Small stack size */
#  define THREAD_STACKSIZE_SMALL ((THREAD_STACKSIZE_MEDIUM / 2) + ESP_THREAD_EXTRA_STACKSIZE_CP)
#endif

#ifndef THREAD_STACKSIZE_TINY
/** Tiny stack size */
#  define THREAD_STACKSIZE_TINY ((THREAD_STACKSIZE_MEDIUM / 4) + ESP_THREAD_EXTRA_STACKSIZE_CP)
#endif

/** Number of DRAM sections that can be used as heap. */
#define NUM_HEAPS (1)

#ifdef __cplusplus
extern "C" {
#endif

#ifdef __cplusplus
}
#endif

/** @} */
