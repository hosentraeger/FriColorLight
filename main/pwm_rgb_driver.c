#include "pwm_rgb_driver.h"

#include "driver/ledc.h"
#include "driver/gpio.h"
#include "light_driver.h"

static uint8_t s_red = 255, s_green = 255, s_blue = 255, s_level = 255;
static bool s_state = 0;

void pwm_rgb_driver_set_power(bool power)
{
    s_state = power;
    pwm_driver_apply ( );
};

void pwm_rgb_driver_init(uint8_t pwm_red_pin, uint8_t pwm_green_pin, uint8_t pwm_blue_pin)
{
    // 1. Timer-Konfiguration
    ledc_timer_config_t ledc_timer = {
        .speed_mode       = LEDC_LOW_SPEED_MODE, // ESP32-C6 unterstützt Low Speed Mode
        .timer_num        = LEDC_TIMER_0,
        .duty_resolution  = LEDC_TIMER_8_BIT,    // 0-255 für einfache RGB-Werte
        .freq_hz          = 5000,                // 5 kHz Frequenz
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ledc_timer_config(&ledc_timer);

    // 2. Kanäle für R, G und B definieren
    uint8_t pins[3] = {pwm_red_pin, pwm_green_pin, pwm_blue_pin};
    ledc_channel_t channels[3] = {LEDC_CHANNEL_0, LEDC_CHANNEL_1, LEDC_CHANNEL_2};

    for (int i = 0; i < 3; i++) {
        ledc_channel_config_t ledc_channel = {
            .speed_mode     = LEDC_LOW_SPEED_MODE,
            .channel        = channels[i],
            .timer_sel      = LEDC_TIMER_0,
            .intr_type      = LEDC_INTR_DISABLE,
            .gpio_num       = pins[i],           // Hier wird die uint8_t Nummer übergeben
            .duty           = 0,                 // Startet bei 0 (aus)
            .hpoint         = 0
        };
        ledc_channel_config(&ledc_channel);
    }
};

void pwm_rgb_driver_set_level(uint8_t level)
{
    s_level = level;
    pwm_driver_apply ( );
}

void pwm_rgb_driver_set_color_RGB(uint8_t red, uint8_t green, uint8_t blue)
{
    s_red = red;
    s_green = green;
    s_blue = blue;
    pwm_driver_apply ( );
};

void pwm_rgb_driver_set_color_xy(uint16_t color_current_x, uint16_t color_current_y)
{
    float red_f = 0, green_f = 0, blue_f = 0, color_x, color_y;
    color_x = (float)color_current_x / 65535;
    color_y = (float)color_current_y / 65535;
    /* assume color_Y is full light level value 1  (0-1.0) */
    float color_X = color_x / color_y;
    float color_Z = (1 - color_x - color_y) / color_y;
    /* change from xy to linear RGB NOT sRGB */
    XYZ_to_RGB(color_X, 1, color_Z, red_f, green_f, blue_f);
    s_red = (uint8_t)(red_f * (float)255);
    s_green = (uint8_t)(green_f * (float)255);
    s_blue = (uint8_t)(blue_f * (float)255);
    pwm_driver_apply ( );
}

void pwm_rgb_driver_set_color_hue_sat(uint8_t hue, uint8_t sat)
{
    float red_f, green_f, blue_f;
    HSV_to_RGB(hue, sat, UINT8_MAX, red_f, green_f, blue_f);
    s_red = (uint8_t)red_f;
    s_green = (uint8_t)green_f;
    s_blue = (uint8_t)blue_f;
    pwm_driver_apply ( );
}

void pwm_driver_apply ( )
{
    uint8_t red = s_state ? (s_red * s_level) / 255 : 0;
    uint8_t green = s_state ? (s_green * s_level) / 255 : 0;
    uint8_t blue = s_state ? (s_blue * s_level) / 255 : 0;

    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, red);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, green);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_2, blue);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_2);
}

uint8_t pwm_rgb_driver_get_level()
{
    return s_level;
}

uint8_t pwm_rgb_driver_get_power()
{
    return s_state;
}