/*
 * SPDX-FileCopyrightText: 2024 Marian Buschsieweke
 * SPDX-License-Identifier: LGPL-2.1-only
 */

#pragma once

/**
 * @ingroup     boards_gd32vf103c_start
 * @{
 *
 * @file
 * @brief       Board specific definitions for the GD32VF103C-START board
 *
 * @author      Marian Buschsieweke <marian.buschsieweke@posteo.net>
 */

#include "board_common.h"
#include "periph/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @name    Button pin definitions
 * @{
 */
#define BTN0_PIN            GPIO_PIN(PORT_A, 0)
#define BTN0_MODE           GPIO_IN_PU
#define BTN0_INT_FLANK      GPIO_FALLING
/** @} */

/**
 * @name    LED (on-board) configuration
 * @{
 */
#define LED0_PIN            GPIO_PIN(PORT_A, 7)
#define LED0_MASK           (1 << 7)
#define LED0_ON             (GPIOA->BC = LED0_MASK)
#define LED0_OFF            (GPIOA->BOP = LED0_MASK)
#define LED0_TOGGLE         (GPIOA->OCTL ^= LED0_MASK)
/** @} */

#ifdef __cplusplus
}
#endif

/** @} */
