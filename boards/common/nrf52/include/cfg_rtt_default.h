/*
 * SPDX-FileCopyrightText: 2018 Inria
 * SPDX-License-Identifier: LGPL-2.1-only
 */

#pragma once

/**
 * @ingroup     boards_common_nrf52
 * @{
 *
 * @file
 * @brief       Default RTT configuration for nRF52 based boards
 *
 * @author      Alexandre Abadie <alexandre.abadie@inria.fr>
 *
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @name    Real time counter configuration
 * @{
 */
#ifndef RTT_DEV
#define RTT_DEV             (1)                 /* NRF_RTC1 */
#endif

#define RTT_MAX_VALUE       (0x00ffffff)         /* 24bit */
#define RTT_MAX_FREQUENCY   (32768U)             /* in Hz */
#define RTT_MIN_FREQUENCY   (8U)                 /* in Hz */
#define RTT_CLOCK_FREQUENCY (32768U)             /* in Hz, LFCLK*/

#ifndef RTT_FREQUENCY
#define RTT_FREQUENCY       (1024U)              /* in Hz */
#endif
/** @} */

#ifdef __cplusplus
}
#endif

/** @} */
