/*
 * SPDX-FileCopyrightText: 2019 Inria
 * SPDX-License-Identifier: LGPL-2.1-only
 */

#pragma once

/**
 * @ingroup     boards_im880b
 * @brief       Support for im880b
 * @{
 *
 * @file
 * @brief       Board specific definitions for the im880b board.
 *
 * @author      Francisco Molina <francois-xavier.molina@inria.fr>
 */

#include <stdint.h>

#include "cpu.h"
#include "periph_conf.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @name    xtimer configuration
 * @{
 */
#define XTIMER_BACKOFF              (11)
#define XTIMER_WIDTH                (16)
/** @} */

/**
 * @name    sx1272 configuration
 * @{
 */
#define SX127X_PARAM_SPI_NSS                GPIO_PIN(PORT_B, 0)

#define SX127X_PARAM_RESET                  GPIO_PIN(PORT_A, 2)
#define SX127X_PARAM_DIO0                   GPIO_PIN(PORT_B, 1)
#define SX127X_PARAM_DIO1                   GPIO_PIN(PORT_B, 10)
#define SX127X_PARAM_DIO2                   GPIO_PIN(PORT_B, 11)
/* stm32l1xxx Errata: Pull-up on PB7 when configured in analog mode */
#define SX127X_PARAM_DIO3                   GPIO_PIN(PORT_B, 7)

#define SX127X_PARAM_PASELECT               (SX127X_PA_BOOST)

#define SX127X_PARAM_RX_SWITCH              GPIO_PIN(PORT_C, 13)
#define SX127X_PARAM_TX_SWITCH              GPIO_PIN(PORT_A, 4)
/** @} */

#ifdef __cplusplus
}
#endif

/** @} */
