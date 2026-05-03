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

#ifdef __cplusplus
extern "C" {
#endif

/**
* @brief Set light power (on/off).
*
* @param  power  The light power to be set
*/
void pwm_tw_driver_set_power(bool power);

/**
* @brief color light driver init, be invoked where you want to use color light
*
* @param power power on/off
*/
void pwm_tw_driver_init(uint8_t pwm_cc_pin, uint8_t pwm_cw_pin );

/**
* @brief Set light level
*
* @param  level  The light level to be set
*/
void pwm_tw_driver_set_level(uint8_t level);

/**
* @brief Set light color from RGB
*
* @param  red    The red color to be set
* @param  green  The green color to be set
* @param  blue   The blue color to be set
*/
void pwm_tw_driver_set_color_temperature(uint8_t K);

#ifdef __cplusplus
} // extern "C"
#endif
