#include "pwm_tw_driver.h"
#include "driver/ledc.h"
#include "driver/gpio.h"

#define TW_LEDC_TIMER    LEDC_TIMER_1
#define TW_LEDC_CHANNEL_CC  LEDC_CHANNEL_3
#define TW_LEDC_CHANNEL_CW  LEDC_CHANNEL_4

static bool    s_state = false;
static uint8_t s_cc    = 255;   /* Kaltweiß-Anteil  0–255 */
static uint8_t s_cw    = 0;     /* Warmweiß-Anteil  0–255 */
static uint8_t s_level = 255;

/* ------------------------------------------------------------------ */
static void tw_driver_apply(void)
{
    uint8_t cc = s_state ? (uint16_t)s_cc * s_level / 255 : 0;
    uint8_t cw = s_state ? (uint16_t)s_cw * s_level / 255 : 0;

    ledc_set_duty(LEDC_LOW_SPEED_MODE, TW_LEDC_CHANNEL_CC, cc);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, TW_LEDC_CHANNEL_CC);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, TW_LEDC_CHANNEL_CW, cw);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, TW_LEDC_CHANNEL_CW);
}

/* ------------------------------------------------------------------ */
void pwm_tw_driver_init(uint8_t pwm_cc_pin, uint8_t pwm_cw_pin)
{
    ledc_timer_config_t ledc_timer = {
        .speed_mode      = LEDC_LOW_SPEED_MODE,
        .timer_num       = TW_LEDC_TIMER,
        .duty_resolution = LEDC_TIMER_8_BIT,
        .freq_hz         = 5000,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ledc_timer_config(&ledc_timer);

    uint8_t          pins[2]     = { pwm_cc_pin,        pwm_cw_pin        };
    ledc_channel_t   channels[2] = { TW_LEDC_CHANNEL_CC, TW_LEDC_CHANNEL_CW };

    for (int i = 0; i < 2; i++) {
        ledc_channel_config_t ch = {
            .speed_mode = LEDC_LOW_SPEED_MODE,
            .channel    = channels[i],
            .timer_sel  = TW_LEDC_TIMER,
            .intr_type  = LEDC_INTR_DISABLE,
            .gpio_num   = pins[i],
            .duty       = 0,
            .hpoint     = 0,
        };
        ledc_channel_config(&ch);
    }
}

/* ------------------------------------------------------------------ */
void pwm_tw_driver_set_power(bool power)
{
    s_state = power;
    tw_driver_apply();
}

void pwm_tw_driver_set_level(uint8_t level)
{
    s_level = level;
    tw_driver_apply();
}

void pwm_tw_driver_set_color_temperature(uint16_t mireds)
{
    // Bereich absichern
    if (mireds < 153) mireds = 153;
    if (mireds > 500) mireds = 500;

    // mireds=153 (6500K, kalt) → cc=255, cw=0
    // mireds=500 (2000K, warm) → cc=0,   cw=255
    uint16_t pos = mireds - 153;           // 0..347
    s_cw = (uint8_t)((uint32_t)pos * 255 / 347);
    s_cc = 255 - s_cw;

    tw_driver_apply();
}