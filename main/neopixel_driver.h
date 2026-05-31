#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif



/* light intensity level */
#define LIGHT_DEFAULT_ON  1
#define LIGHT_DEFAULT_OFF 0

/* LED strip configuration */
#define CONFIG_EXAMPLE_STRIP_LED_GPIO   CONFIG_GPIO_LED_ON_DEVKIT
#define CONFIG_EXAMPLE_STRIP_LED_NUMBER 1

/**
* @brief Set light power (on/off).
*
* @param  power  The light power to be set
*/
void neopixel_driver_set_power(bool power);

/**
* @brief color light driver init, be invoked where you want to use color light
*
* @param power power on/off
*/
void neopixel_driver_init(uint8_t data_pin, uint8_t num_leds);

/**
* @brief Set light level
*
* @param  level  The light level to be set
*/
void neopixel_driver_set_level(uint8_t level);

/**
* @brief Set light color from RGB
*
* @param  red    The red color to be set
* @param  green  The green color to be set
* @param  blue   The blue color to be set
*/
void neopixel_driver_set_color_RGB(uint8_t red, uint8_t green, uint8_t blue);

/**
* @brief Set light color from color xy
*
* @param  color_currentx  The color x to be set
* @param  color_currenty  The color y to be set
*/
void neopixel_driver_set_color_xy(uint16_t color_current_x, uint16_t color_current_y);

/**
* @brief Set light color from hue saturation
*
* @param  hue  The hue to be set
* @param  sat  The sat to be set
*/
void neopixel_driver_set_color_hue_sat(uint8_t hue, uint8_t sat);

/**
* @brief Apply the current light settings
*/
void neopixel_driver_apply ( );

#ifdef __cplusplus
} // extern "C"
#endif
