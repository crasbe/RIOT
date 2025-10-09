/*
 * Copyright (C) 2025 Technische Universität Hamburg
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

/**
 * @ingroup         boards_waveshare_rp2040_128_lcd
 * @{
 *
 * @file
 * @brief           Board specific definitions for the Waveshare RP2040 1.28"
 *                  Round LCD board
 *
 * @author          Christopher Büchse <christopher.buechse@tuhh.de>
 */

#ifndef BOARD_H
#define BOARD_H

#include "cpu.h"
#include "periph_conf.h"
#include "periph_cpu.h"
#include "periph/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifdef BOARD_RPI_PICO
/**
 * @name    LED configuration
 * @{
 */
#define LED0_PIN        GPIO_PIN(0, 25)
#define LED0_ON         do {SIO->GPIO_OUT_SET = 1UL << 25;} while (0)
#define LED0_OFF        do {SIO->GPIO_OUT_CLR = 1UL << 25;} while (0)
#define LED0_TOGGLE     do {SIO->GPIO_OUT_XOR = 1UL << 25;} while (0)
#define LED0_NAME       "LED(Green)"
/** @} */

#endif

/**
 * @name LCD Pins
 * @{
 */
#define LCD_DC      GPIO_PIN(0, 8)
#define LCD_CS      GPIO_PIN(0, 9)
#define LCD_CLK     GPIO_PIN(0, 10)
#define LCD_DIN     GPIO_PIN(0, 11)
#define LCD_RST     GPIO_PIN(0, 12)
#define LCD_BL      GPIO_PIN(0, 25)

/** @} */



#ifdef __cplusplus
}
#endif

#endif /* BOARD_H */
/** @} */
