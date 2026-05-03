/*
 * SPDX-FileCopyrightText: 2021-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 *
 * Zigbee light driver example
 *
 * This example code is in the Public Domain (or CC0 licensed, at your option.)
 *
 * Unless required by applicable law or agreed to in writing, this
 * software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
 * CONDITIONS OF ANY KIND, either express or implied.
 */


#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
* @brief Set light power (on/off).
*
* @param  power  The light power to be set
*/
void pwm_rgb_driver_set_power(bool power);

/**
* @brief color light driver init, be invoked where you want to use color light
*
* @param power power on/off
*/
void pwm_rgb_driver_init(uint8_t pwm_red_pin, uint8_t pwm_green_pin, uint8_t pwm_blue_pin);

/**
* @brief Set light level
*
* @param  level  The light level to be set
*/
void pwm_rgb_driver_set_level(uint8_t level);

/**
* @brief Set light color from RGB
*
* @param  red    The red color to be set
* @param  green  The green color to be set
* @param  blue   The blue color to be set
*/
void pwm_rgb_driver_set_color_RGB(uint8_t red, uint8_t green, uint8_t blue);

/**
* @brief Set light color from color xy
*
* @param  color_currentx  The color x to be set
* @param  color_currenty  The color y to be set
*/
void pwm_rgb_driver_set_color_xy(uint16_t color_current_x, uint16_t color_current_y);

/**
* @brief Set light color from hue saturation
*
* @param  hue  The hue to be set
* @param  sat  The sat to be set
*/
void pwm_rgb_driver_set_color_hue_sat(uint8_t hue, uint8_t sat);

/**
* @brief Apply the current light settings
*/
void pwm_driver_apply ( );

uint8_t pwm_rgb_driver_get_level(void);

#ifdef __cplusplus
} // extern "C"
#endif
