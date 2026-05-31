/*
 * SPDX-FileCopyrightText: 2021-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 *
 * Zigbee HA_color_dimmable_light Example
 *
 * This example code is in the Public Domain (or CC0 licensed, at your option.)
 *
 * Unless required by applicable law or agreed to in writing, this
 * software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
 * CONDITIONS OF ANY KIND, either express or implied.
 */

#include "FriColorLight.h"
#include "pwm_rgb_driver.h"
#include "pwm_tw_driver.h"
#include "neopixel_driver.h"
#include "esp_check.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ha/esp_zigbee_ha_standard.h"
#include "zcl_utility.h"
#include "zboss_api.h"  // für zb_zcl_parsed_hdr_t und ZB_BUF_GET_PARAM

#include "zcl/esp_zigbee_zcl_on_off.h"
#include "zcl/esp_zigbee_zcl_level.h"
#include "zcl/esp_zigbee_zcl_color_control.h"
#include "zboss_api_buf.h"

#include "FriConfig.h"
#include "FriOta.h"

#if !defined CONFIG_ZB_ZCZR
#error Define ZB_ZCZR in idf.py menuconfig to compile light (Router) source code.
#endif

#define CONFIG_FRILIGHT_DEBUG_RAW_CMDS false
#define CLAMP(x, lo, hi) ((x) < (lo) ? (lo) : ((x) > (hi) ? (hi) : (x)))

enum {
    COLOR_CAPABILITY_SUPPORT_HUE_SATURATION = 1 << 0,
    COLOR_CAPABILITY_SUPPORT_ENHANCED_HUE_SATURATION = 1 << 1,
    COLOR_CAPABILITY_SUPPORT_COLOR_LOOP = 1 << 2,
    COLOR_CAPABILITY_SUPPORT_XY = 1 << 3,
    COLOR_CAPABILITY_SUPPORT_COLOR_TEMPERATURE = 1 << 4,
} COLOR_CAPABILITIES;


static uint8_t model_id[MAX_ZIGBEE_STRING_LENGTH] = {0};
static uint8_t vendor[MAX_ZIGBEE_STRING_LENGTH]   = {0};
static uint8_t date_code[MAX_ZIGBEE_STRING_LENGTH] = {0};
static uint8_t sw_build[MAX_ZIGBEE_STRING_LENGTH]  = {0};
static const char *TAG = "ESP_ZB_COLOR_DIMM_LIGHT";

// EP1 – RGB PWM
static uint16_t s_ep1_color_x = ESP_ZB_ZCL_COLOR_CONTROL_CURRENT_X_DEF_VALUE;
static uint16_t s_ep1_color_y = ESP_ZB_ZCL_COLOR_CONTROL_CURRENT_Y_DEF_VALUE;
static uint8_t  s_ep1_hue     = 0;
static uint8_t  s_ep1_sat     = 254;

// EP2 – RGB SK6812
static uint16_t s_ep2_color_x = ESP_ZB_ZCL_COLOR_CONTROL_CURRENT_X_DEF_VALUE;
static uint16_t s_ep2_color_y = ESP_ZB_ZCL_COLOR_CONTROL_CURRENT_Y_DEF_VALUE;
static uint8_t  s_ep2_hue     = 0;
static uint8_t  s_ep2_sat     = 254;

static uint8_t color_mode = 2;
static uint16_t enhanced_hue = 0;

#if CONFIG_FRILIGHT_DEBUG_RAW_CMDS
static bool zb_raw_command_handler(uint8_t bufid)
{
    zb_zcl_parsed_hdr_t *cmd_info = ZB_BUF_GET_PARAM(bufid, zb_zcl_parsed_hdr_t);
    uint8_t *payload = (uint8_t *)zb_buf_begin(bufid);
    uint8_t payload_len = zb_buf_len(bufid);

    // Alles loggen was reinkommt
    ESP_LOGI(TAG, "RAW CMD:   cluster=0x%04x, cmd=0x%02x, ep=%d, payload_len=%d",
             cmd_info->cluster_id,
             cmd_info->cmd_id,
             cmd_info->addr_data.common_data.dst_endpoint,
             payload_len);

    // Payload bytes roh ausgeben
    for (int i = 0; i < payload_len; i++) 
    {
        ESP_LOGI(TAG, "RAW CMD:   payload[%d] = 0x%02x (%d)", i, payload[i], payload[i]);
    }
    return false;
}
#endif

static esp_err_t zb_action_handler(esp_zb_core_action_callback_id_t callback_id, const void *message)
{
    if (callback_id == ESP_ZB_CORE_OTA_UPGRADE_VALUE_CB_ID) {
        ESP_LOGI(TAG, "OTA Upgrade Status Callback received");
        return zb_ota_upgrade_status_handler(*(esp_zb_zcl_ota_upgrade_value_message_t *)message);
    }

    if (callback_id == ESP_ZB_CORE_OTA_UPGRADE_QUERY_IMAGE_RESP_CB_ID) {
        ESP_LOGI(TAG, "OTA Upgrade Query Image Response Callback received");
        return zb_ota_upgrade_query_image_resp_handler(*(esp_zb_zcl_ota_upgrade_query_image_resp_message_t *)message);
    }

    if (callback_id != ESP_ZB_CORE_SET_ATTR_VALUE_CB_ID) {
        ESP_LOGD(TAG, "unhandled CB 0x%04x", callback_id);
        return ESP_OK;
    }

    esp_zb_zcl_set_attr_value_message_t *msg =
        (esp_zb_zcl_set_attr_value_message_t *)message;

    uint8_t  ep      = msg->info.dst_endpoint;
    uint16_t cluster = msg->info.cluster;
    uint16_t attr    = msg->attribute.id;

    ESP_LOGI(TAG, "ACTION: ep=%d cluster=0x%04X attr=0x%04X", ep, cluster, attr);

    switch (cluster) {

        // ── ON / OFF ──────────────────────────────────────────────────────
        case ESP_ZB_ZCL_CLUSTER_ID_ON_OFF: {
            if (attr != ESP_ZB_ZCL_ATTR_ON_OFF_ON_OFF_ID) break;
            bool on = *(bool *)msg->attribute.data.value;
            ESP_LOGI(TAG, "EP%d ON/OFF → %s", ep, on ? "ON" : "OFF");
            switch (ep) {
                case HA_COLOR_DIMMABLE_LIGHT_ENDPOINT_1:
                    pwm_rgb_driver_set_power(on);
                    pwm_driver_apply();
                    break;
                case HA_COLOR_DIMMABLE_LIGHT_ENDPOINT_2:
                    neopixel_driver_set_power(on);
                    break;
                case HA_COLOR_DIMMABLE_LIGHT_ENDPOINT_3:
                    pwm_tw_driver_set_power(on);
                    break;
            }
            break;
        }

        // ── LEVEL ─────────────────────────────────────────────────────────
        case ESP_ZB_ZCL_CLUSTER_ID_LEVEL_CONTROL: {
            if (attr != ESP_ZB_ZCL_ATTR_LEVEL_CONTROL_CURRENT_LEVEL_ID) break;
            uint8_t level = *(uint8_t *)msg->attribute.data.value;
            ESP_LOGI(TAG, "EP%d LEVEL → %d", ep, level);
            switch (ep) {
                case HA_COLOR_DIMMABLE_LIGHT_ENDPOINT_1:
                    pwm_rgb_driver_set_level(level);
                    pwm_driver_apply();
                    break;
                case HA_COLOR_DIMMABLE_LIGHT_ENDPOINT_2:
                    neopixel_driver_set_level(level);
                    break;
                case HA_COLOR_DIMMABLE_LIGHT_ENDPOINT_3:
                    pwm_tw_driver_set_level(level);
                    break;
            }
            break;
        }

        // ── COLOR CONTROL ─────────────────────────────────────────────────
        case ESP_ZB_ZCL_CLUSTER_ID_COLOR_CONTROL: {

            // EP3 (Tunable White) – nur Color Temperature
            if (ep == HA_COLOR_DIMMABLE_LIGHT_ENDPOINT_3) {
                if (attr == ESP_ZB_ZCL_ATTR_COLOR_CONTROL_COLOR_TEMPERATURE_ID) {
                    uint16_t mireds = *(uint16_t *)msg->attribute.data.value;
                    ESP_LOGI(TAG, "EP3 COLOR_TEMP → %d mireds", mireds);
                    pwm_tw_driver_set_color_temperature(mireds);
                } else {
                    ESP_LOGD(TAG, "EP3 COLOR attr=0x%04x (ignoriert)", attr);
                }
                break;
            }

            // EP1 + EP2 – RGB (XY + Hue/Sat + Enhanced Hue)
            // Zustandsvariablen pro Endpoint
            uint16_t *color_x   = (ep == HA_COLOR_DIMMABLE_LIGHT_ENDPOINT_1) ? &s_ep1_color_x   : &s_ep2_color_x;
            uint16_t *color_y   = (ep == HA_COLOR_DIMMABLE_LIGHT_ENDPOINT_1) ? &s_ep1_color_y   : &s_ep2_color_y;
            uint8_t  *hue       = (ep == HA_COLOR_DIMMABLE_LIGHT_ENDPOINT_1) ? &s_ep1_hue       : &s_ep2_hue;
            uint8_t  *sat       = (ep == HA_COLOR_DIMMABLE_LIGHT_ENDPOINT_1) ? &s_ep1_sat       : &s_ep2_sat;

            switch (attr) {

                case ESP_ZB_ZCL_ATTR_COLOR_CONTROL_CURRENT_HUE_ID: {
                    *hue = *(uint8_t *)msg->attribute.data.value;
                    ESP_LOGI(TAG, "EP%d HUE → %d", ep, *hue);
                    if (ep == HA_COLOR_DIMMABLE_LIGHT_ENDPOINT_1) {
                        pwm_rgb_driver_set_color_hue_sat(*hue, *sat);
                        pwm_driver_apply();
                    } else {
                        neopixel_driver_set_color_hue_sat(*hue, *sat);
                    }
                    break;
                }

                case ESP_ZB_ZCL_ATTR_COLOR_CONTROL_CURRENT_SATURATION_ID: {
                    *sat = *(uint8_t *)msg->attribute.data.value;
                    ESP_LOGI(TAG, "EP%d SAT → %d", ep, *sat);
                    if (ep == HA_COLOR_DIMMABLE_LIGHT_ENDPOINT_1) {
                        pwm_rgb_driver_set_color_hue_sat(*hue, *sat);
                        pwm_driver_apply();
                    } else {
                        neopixel_driver_set_color_hue_sat(*hue, *sat);
                    }
                    break;
                }

                case ESP_ZB_ZCL_ATTR_COLOR_CONTROL_ENHANCED_CURRENT_HUE_ID: {
                    uint16_t enh_hue = *(uint16_t *)msg->attribute.data.value;
                    *hue = (uint8_t)((uint32_t)enh_hue * 254 / 65535);
                    ESP_LOGI(TAG, "EP%d ENHANCED_HUE → %d (raw %d)", ep, *hue, enh_hue);
                    if (ep == HA_COLOR_DIMMABLE_LIGHT_ENDPOINT_1) {
                        pwm_rgb_driver_set_color_hue_sat(*hue, *sat);
                        pwm_driver_apply();
                    } else {
                        neopixel_driver_set_color_hue_sat(*hue, *sat);
                    }
                    break;
                }

                case ESP_ZB_ZCL_ATTR_COLOR_CONTROL_CURRENT_X_ID: {
                    *color_x = *(uint16_t *)msg->attribute.data.value;
                    ESP_LOGD(TAG, "EP%d X → %d", ep, *color_x);
                    break;
                }

                case ESP_ZB_ZCL_ATTR_COLOR_CONTROL_CURRENT_Y_ID: {
                    uint16_t y = *(uint16_t *)msg->attribute.data.value;
                    if (*color_x == 0 && y == 0) {
                        ESP_LOGD(TAG, "EP%d XY 0/0 ignoriert", ep);
                        break;
                    }
                    *color_y = y;
                    ESP_LOGI(TAG, "EP%d XY → %d/%d", ep, *color_x, *color_y);
                    if (ep == HA_COLOR_DIMMABLE_LIGHT_ENDPOINT_1) {
                        pwm_rgb_driver_set_color_xy(*color_x, *color_y);
                        pwm_driver_apply();
                    } else {
                        neopixel_driver_set_color_xy(*color_x, *color_y);
                    }
                    break;
                }

                default:
                    ESP_LOGD(TAG, "EP%d COLOR attr=0x%04x (unbekannt)", ep, attr);
                    break;
            }
            break;
        }

        default:
            ESP_LOGD(TAG, "EP%d cluster=0x%04x attr=0x%04x (ignoriert)", ep, cluster, attr);
            break;
    }

    return ESP_OK;
}

static esp_err_t deferred_driver_init(void)
{
    static bool is_inited = false;
    if (!is_inited) 
    {
        pwm_rgb_driver_init(PWM_RED_PIN, PWM_GREEN_PIN, PWM_BLUE_PIN );
        pwm_tw_driver_init(PWM_TW_CC_PIN, PWM_TW_CW_PIN);
        neopixel_driver_init(NEOPIXEL_DATA_PIN, NEOPIXEL_NUM_LEDS);
        is_inited = true;
    }
    return is_inited ? ESP_OK : ESP_FAIL;
}

static void bdb_start_top_level_commissioning_cb(uint8_t mode_mask)
{
    ESP_RETURN_ON_FALSE(esp_zb_bdb_start_top_level_commissioning(mode_mask) == ESP_OK, , TAG, "Failed to start Zigbee commissioning");
}

void esp_zb_app_signal_handler(esp_zb_app_signal_t *signal_struct)
{
    uint32_t *p_sg_p = signal_struct->p_app_signal;
    esp_err_t err_status = signal_struct->esp_err_status;
    esp_zb_app_signal_type_t sig_type = *p_sg_p;
    switch (sig_type) 
    {
        case ESP_ZB_ZDO_SIGNAL_SKIP_STARTUP:
            ESP_LOGI(TAG, "Initialize Zigbee stack");
            esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_INITIALIZATION);
            break;
        case ESP_ZB_BDB_SIGNAL_DEVICE_FIRST_START:
        case ESP_ZB_BDB_SIGNAL_DEVICE_REBOOT:
            if (err_status == ESP_OK) 
            {
                ESP_LOGI(TAG, "Deferred driver initialization %s", deferred_driver_init() ? "failed" : "successful");
                ESP_LOGI(TAG, "Device started up in%s factory-reset mode", esp_zb_bdb_is_factory_new() ? "" : " non");
                if (esp_zb_bdb_is_factory_new()) 
                {
                    ESP_LOGI(TAG, "Start network steering");
                    esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_NETWORK_STEERING);
                }
                else 
                {
                    ESP_LOGI(TAG, "Device rebooted");
                }
            } else 
            {
                ESP_LOGW(TAG, "%s failed with status: %s, retrying", esp_zb_zdo_signal_to_string(sig_type),
                        esp_err_to_name(err_status));
                esp_zb_scheduler_alarm((esp_zb_callback_t)bdb_start_top_level_commissioning_cb,
                                    ESP_ZB_BDB_MODE_INITIALIZATION, 1000);
            }
            break;
        case ESP_ZB_BDB_SIGNAL_STEERING:
            if (err_status == ESP_OK) 
            {
                esp_zb_ieee_addr_t extended_pan_id;
                esp_zb_get_extended_pan_id(extended_pan_id);
                ESP_LOGI(TAG, "Joined network successfully (Extended PAN ID: %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x, PAN ID: 0x%04hx, Channel:%d, Short Address: 0x%04hx)",
                        extended_pan_id[7], extended_pan_id[6], extended_pan_id[5], extended_pan_id[4],
                        extended_pan_id[3], extended_pan_id[2], extended_pan_id[1], extended_pan_id[0],
                        esp_zb_get_pan_id(), esp_zb_get_current_channel(), esp_zb_get_short_address());
            }
            else 
            {
                ESP_LOGI(TAG, "Network steering was not successful (status: %s)", esp_err_to_name(err_status));
                esp_zb_scheduler_alarm((esp_zb_callback_t)bdb_start_top_level_commissioning_cb, ESP_ZB_BDB_MODE_NETWORK_STEERING, 1000);
            }
            break;
        case ESP_ZB_NWK_SIGNAL_PERMIT_JOIN_STATUS:
            if (err_status == ESP_OK) 
            {
                if (*(uint8_t *)esp_zb_app_signal_get_params(p_sg_p)) 
                {
                    ESP_LOGI(TAG, "Network(0x%04hx) is open for %d seconds", esp_zb_get_pan_id(), *(uint8_t *)esp_zb_app_signal_get_params(p_sg_p));
                }
                else 
                {
                    ESP_LOGW(TAG, "Network(0x%04hx) closed, devices joining not allowed.", esp_zb_get_pan_id());
                }
            }
            break;
        case ESP_ZB_BDB_SIGNAL_FINDING_AND_BINDING_TARGET_FINISHED:
        default:
            ESP_LOGI(TAG, "ZDO signal: %s (0x%x), status: %s", esp_zb_zdo_signal_to_string(sig_type), sig_type, esp_err_to_name(err_status));
            break;
    }
}

static void esp_zb_task(void *pvParameters)
{
    uint8_t app_version  = DEVICE_APP_VERSION;
    uint8_t stack_version = DEVICE_STACK_VERSION;
    uint8_t hw_version   = DEVICE_HW_VERSION;
    uint16_t zero        = 0;
    uint8_t  zero_u8     = 0;
    uint8_t  max_sat     = 254;
    uint16_t mireds_max  = 500;
    uint16_t mireds_min  = 153;

    build_date_code(date_code, MAX_ZIGBEE_STRING_LENGTH);
    build_sw_build (sw_build,  MAX_ZIGBEE_STRING_LENGTH);
    build_model_id (model_id,  MAX_ZIGBEE_STRING_LENGTH);
    build_vendor   (vendor,    MAX_ZIGBEE_STRING_LENGTH);

    esp_zb_cfg_t zb_nwk_cfg = ESP_ZB_ZR_CONFIG();
    esp_zb_init(&zb_nwk_cfg);

    /* ------------------------------------------------------------------ */
    /*  Shared cluster configs                                              */
    /* ------------------------------------------------------------------ */
    esp_zb_on_off_cluster_cfg_t on_off_cfg   = { .on_off        = ESP_ZB_ZCL_ON_OFF_ON_OFF_DEFAULT_VALUE };
    esp_zb_level_cluster_cfg_t  level_cfg    = { .current_level = 0x7f };
    esp_zb_basic_cluster_cfg_t  basic_cfg    = { .zcl_version   = 8, .power_source = 1 };
    esp_zb_identify_cluster_cfg_t identity_cfg = { .identify_time = 0 };
    esp_zb_groups_cluster_cfg_t groups_cfg   = { .groups_name_support_id = 0 };
    esp_zb_scenes_cluster_cfg_t scenes_cfg   = { 0, 0, 0, false, 0 };

    uint16_t color_capabilities_rgb =
          COLOR_CAPABILITY_SUPPORT_XY
        | COLOR_CAPABILITY_SUPPORT_HUE_SATURATION
        | COLOR_CAPABILITY_SUPPORT_ENHANCED_HUE_SATURATION;

    /* EP3: only color temperature */
    uint16_t color_capabilities_tw  = COLOR_CAPABILITY_SUPPORT_COLOR_TEMPERATURE;
    uint8_t  color_mode_tw          = ZB_ZCL_COLOR_CONTROL_COLOR_MODE_TEMPERATURE;
    uint8_t  enhanced_color_mode_tw = 0x02;                                           // ZCL Spec: Color Temperature, kein ESP-Define vorhanden
    uint16_t mireds_default         = 370; /* warm-ish default */

    /* ------------------------------------------------------------------ */
    /*  Helper macro: build a full basic cluster with all vendor attrs     */
    /* ------------------------------------------------------------------ */
#define BUILD_BASIC_CLUSTER(cfg_ptr)                                                                      \
    ({                                                                                                    \
        esp_zb_attribute_list_t *_bc = esp_zb_basic_cluster_create(cfg_ptr);                             \
        ESP_ERROR_CHECK(esp_zb_basic_cluster_add_attr(_bc, ESP_ZB_ZCL_ATTR_BASIC_APPLICATION_VERSION_ID, &app_version));  \
        ESP_ERROR_CHECK(esp_zb_basic_cluster_add_attr(_bc, ESP_ZB_ZCL_ATTR_BASIC_STACK_VERSION_ID,       &stack_version)); \
        ESP_ERROR_CHECK(esp_zb_basic_cluster_add_attr(_bc, ESP_ZB_ZCL_ATTR_BASIC_HW_VERSION_ID,          &hw_version));   \
        ESP_ERROR_CHECK(esp_zb_basic_cluster_add_attr(_bc, ESP_ZB_ZCL_ATTR_BASIC_DATE_CODE_ID,            date_code));    \
        ESP_ERROR_CHECK(esp_zb_basic_cluster_add_attr(_bc, ESP_ZB_ZCL_ATTR_BASIC_SW_BUILD_ID,             sw_build));     \
        ESP_ERROR_CHECK(esp_zb_basic_cluster_add_attr(_bc, ESP_ZB_ZCL_ATTR_BASIC_MANUFACTURER_NAME_ID,    vendor));       \
        ESP_ERROR_CHECK(esp_zb_basic_cluster_add_attr(_bc, ESP_ZB_ZCL_ATTR_BASIC_MODEL_IDENTIFIER_ID,     model_id));     \
        _bc;                                                                                              \
    })

    /* ------------------------------------------------------------------ */
    /*  Helper: build RGB color cluster (XY + Hue/Sat + Enhanced Hue)     */
    /* ------------------------------------------------------------------ */
#define BUILD_RGB_COLOR_CLUSTER()                                                                                                                          \
    ({                                                                                                                                                     \
        esp_zb_attribute_list_t *_cc = esp_zb_zcl_attr_list_create(ESP_ZB_ZCL_CLUSTER_ID_COLOR_CONTROL);                                                  \
        ESP_ERROR_CHECK(esp_zb_color_control_cluster_add_attr(_cc, ESP_ZB_ZCL_ATTR_COLOR_CONTROL_COLOR_CAPABILITIES_ID,            &color_capabilities_rgb)); \
        ESP_ERROR_CHECK(esp_zb_color_control_cluster_add_attr(_cc, ESP_ZB_ZCL_ATTR_COLOR_CONTROL_CURRENT_X_ID,                     &zero));               \
        ESP_ERROR_CHECK(esp_zb_color_control_cluster_add_attr(_cc, ESP_ZB_ZCL_ATTR_COLOR_CONTROL_CURRENT_Y_ID,                     &zero));               \
        ESP_ERROR_CHECK(esp_zb_color_control_cluster_add_attr(_cc, ESP_ZB_ZCL_ATTR_COLOR_CONTROL_COLOR_MODE_ID,                    &color_mode));         \
        ESP_ERROR_CHECK(esp_zb_color_control_cluster_add_attr(_cc, ESP_ZB_ZCL_ATTR_COLOR_CONTROL_OPTIONS_ID,                       &zero_u8));            \
        ESP_ERROR_CHECK(esp_zb_color_control_cluster_add_attr(_cc, ESP_ZB_ZCL_ATTR_COLOR_CONTROL_ENHANCED_COLOR_MODE_ID,           &color_mode));         \
        ESP_ERROR_CHECK(esp_zb_color_control_cluster_add_attr(_cc, ESP_ZB_ZCL_ATTR_COLOR_CONTROL_CURRENT_HUE_ID,                   &zero_u8));            \
        ESP_ERROR_CHECK(esp_zb_color_control_cluster_add_attr(_cc, ESP_ZB_ZCL_ATTR_COLOR_CONTROL_CURRENT_SATURATION_ID,            &max_sat));            \
        ESP_ERROR_CHECK(esp_zb_color_control_cluster_add_attr(_cc, ESP_ZB_ZCL_ATTR_COLOR_CONTROL_ENHANCED_CURRENT_HUE_ID,          &enhanced_hue));       \
        ESP_ERROR_CHECK(esp_zb_color_control_cluster_add_attr(_cc, ESP_ZB_ZCL_ATTR_COLOR_CONTROL_COLOR_TEMP_PHYSICAL_MIN_MIREDS_ID, &mireds_min));        \
        ESP_ERROR_CHECK(esp_zb_color_control_cluster_add_attr(_cc, ESP_ZB_ZCL_ATTR_COLOR_CONTROL_COLOR_TEMP_PHYSICAL_MAX_MIREDS_ID, &mireds_max));        \
        _cc;                                                                                                                                               \
    })

    /* ------------------------------------------------------------------ */
    /*  EP1 – RGB PWM                                                       */
    /* ------------------------------------------------------------------ */
    esp_zb_endpoint_config_t ep1_config = {
        .endpoint       = HA_COLOR_DIMMABLE_LIGHT_ENDPOINT_1,
        .app_profile_id = ESP_ZB_AF_HA_PROFILE_ID,
        .app_device_id  = ESP_ZB_HA_COLOR_DIMMABLE_LIGHT_DEVICE_ID,
        .app_device_version = 0,
    };

    esp_zb_cluster_list_t *cluster_list_ep1 = esp_zb_zcl_cluster_list_create();
    ESP_ERROR_CHECK(esp_zb_cluster_list_add_basic_cluster   (cluster_list_ep1, BUILD_BASIC_CLUSTER(&basic_cfg),                           ESP_ZB_ZCL_CLUSTER_SERVER_ROLE));
    ESP_ERROR_CHECK(esp_zb_cluster_list_add_identify_cluster(cluster_list_ep1, esp_zb_identify_cluster_create(&identity_cfg),             ESP_ZB_ZCL_CLUSTER_SERVER_ROLE));
    ESP_ERROR_CHECK(esp_zb_cluster_list_add_groups_cluster  (cluster_list_ep1, esp_zb_groups_cluster_create(&groups_cfg),                 ESP_ZB_ZCL_CLUSTER_SERVER_ROLE));
    ESP_ERROR_CHECK(esp_zb_cluster_list_add_scenes_cluster  (cluster_list_ep1, esp_zb_scenes_cluster_create(&scenes_cfg),                 ESP_ZB_ZCL_CLUSTER_SERVER_ROLE));
    ESP_ERROR_CHECK(esp_zb_cluster_list_add_on_off_cluster  (cluster_list_ep1, esp_zb_on_off_cluster_create(&on_off_cfg),                 ESP_ZB_ZCL_CLUSTER_SERVER_ROLE));
    ESP_ERROR_CHECK(esp_zb_cluster_list_add_level_cluster   (cluster_list_ep1, esp_zb_level_cluster_create(&level_cfg),                   ESP_ZB_ZCL_CLUSTER_SERVER_ROLE));
    ESP_ERROR_CHECK(esp_zb_cluster_list_add_color_control_cluster(cluster_list_ep1, BUILD_RGB_COLOR_CLUSTER(),                            ESP_ZB_ZCL_CLUSTER_SERVER_ROLE));
    ESP_ERROR_CHECK(esp_zb_cluster_list_add_on_off_cluster  (cluster_list_ep1, esp_zb_zcl_attr_list_create(ESP_ZB_ZCL_CLUSTER_ID_ON_OFF),           ESP_ZB_ZCL_CLUSTER_CLIENT_ROLE));
    ESP_ERROR_CHECK(esp_zb_cluster_list_add_level_cluster   (cluster_list_ep1, esp_zb_zcl_attr_list_create(ESP_ZB_ZCL_CLUSTER_ID_LEVEL_CONTROL),    ESP_ZB_ZCL_CLUSTER_CLIENT_ROLE));
    ESP_ERROR_CHECK(esp_zb_cluster_list_add_color_control_cluster(cluster_list_ep1, esp_zb_zcl_attr_list_create(ESP_ZB_ZCL_CLUSTER_ID_COLOR_CONTROL), ESP_ZB_ZCL_CLUSTER_CLIENT_ROLE));
    ESP_ERROR_CHECK(zb_register_ota_upgrade_client_device(cluster_list_ep1));

    /* ------------------------------------------------------------------ */
    /*  EP2 – RGB SK6812                                                    */
    /* ------------------------------------------------------------------ */
    esp_zb_endpoint_config_t ep2_config = {
        .endpoint       = HA_COLOR_DIMMABLE_LIGHT_ENDPOINT_2,
        .app_profile_id = ESP_ZB_AF_HA_PROFILE_ID,
        .app_device_id  = ESP_ZB_HA_COLOR_DIMMABLE_LIGHT_DEVICE_ID,
        .app_device_version = 0,
    };

    esp_zb_cluster_list_t *cluster_list_ep2 = esp_zb_zcl_cluster_list_create();
    ESP_ERROR_CHECK(esp_zb_cluster_list_add_basic_cluster   (cluster_list_ep2, BUILD_BASIC_CLUSTER(&basic_cfg),                           ESP_ZB_ZCL_CLUSTER_SERVER_ROLE));
    ESP_ERROR_CHECK(esp_zb_cluster_list_add_identify_cluster(cluster_list_ep2, esp_zb_identify_cluster_create(&identity_cfg),             ESP_ZB_ZCL_CLUSTER_SERVER_ROLE));
    ESP_ERROR_CHECK(esp_zb_cluster_list_add_groups_cluster  (cluster_list_ep2, esp_zb_groups_cluster_create(&groups_cfg),                 ESP_ZB_ZCL_CLUSTER_SERVER_ROLE));
    ESP_ERROR_CHECK(esp_zb_cluster_list_add_scenes_cluster  (cluster_list_ep2, esp_zb_scenes_cluster_create(&scenes_cfg),                 ESP_ZB_ZCL_CLUSTER_SERVER_ROLE));
    ESP_ERROR_CHECK(esp_zb_cluster_list_add_on_off_cluster  (cluster_list_ep2, esp_zb_on_off_cluster_create(&on_off_cfg),                 ESP_ZB_ZCL_CLUSTER_SERVER_ROLE));
    ESP_ERROR_CHECK(esp_zb_cluster_list_add_level_cluster   (cluster_list_ep2, esp_zb_level_cluster_create(&level_cfg),                   ESP_ZB_ZCL_CLUSTER_SERVER_ROLE));
    ESP_ERROR_CHECK(esp_zb_cluster_list_add_color_control_cluster(cluster_list_ep2, BUILD_RGB_COLOR_CLUSTER(),                            ESP_ZB_ZCL_CLUSTER_SERVER_ROLE));
    ESP_ERROR_CHECK(esp_zb_cluster_list_add_on_off_cluster  (cluster_list_ep2, esp_zb_zcl_attr_list_create(ESP_ZB_ZCL_CLUSTER_ID_ON_OFF),           ESP_ZB_ZCL_CLUSTER_CLIENT_ROLE));
    ESP_ERROR_CHECK(esp_zb_cluster_list_add_level_cluster   (cluster_list_ep2, esp_zb_zcl_attr_list_create(ESP_ZB_ZCL_CLUSTER_ID_LEVEL_CONTROL),    ESP_ZB_ZCL_CLUSTER_CLIENT_ROLE));
    ESP_ERROR_CHECK(esp_zb_cluster_list_add_color_control_cluster(cluster_list_ep2, esp_zb_zcl_attr_list_create(ESP_ZB_ZCL_CLUSTER_ID_COLOR_CONTROL), ESP_ZB_ZCL_CLUSTER_CLIENT_ROLE));
    /* OTA only on EP1, not duplicated here */

    /* ------------------------------------------------------------------ */
    /*  EP3 – Tunable White (WW + CW)                                      */
    /* ------------------------------------------------------------------ */
    esp_zb_endpoint_config_t ep3_config = {
        .endpoint       = HA_COLOR_DIMMABLE_LIGHT_ENDPOINT_3,
        .app_profile_id = ESP_ZB_AF_HA_PROFILE_ID,
        .app_device_id  = ESP_ZB_HA_COLOR_TEMPERATURE_LIGHT_DEVICE_ID,
        .app_device_version = 0,
    };

    esp_zb_attribute_list_t *tw_color_cluster = esp_zb_zcl_attr_list_create(ESP_ZB_ZCL_CLUSTER_ID_COLOR_CONTROL);
    ESP_ERROR_CHECK(esp_zb_color_control_cluster_add_attr(tw_color_cluster, ESP_ZB_ZCL_ATTR_COLOR_CONTROL_COLOR_CAPABILITIES_ID,             &color_capabilities_tw));
    ESP_ERROR_CHECK(esp_zb_color_control_cluster_add_attr(tw_color_cluster, ESP_ZB_ZCL_ATTR_COLOR_CONTROL_COLOR_TEMPERATURE_ID,              &mireds_default));
    ESP_ERROR_CHECK(esp_zb_color_control_cluster_add_attr(tw_color_cluster, ESP_ZB_ZCL_ATTR_COLOR_CONTROL_COLOR_MODE_ID,                     &color_mode_tw));
    ESP_ERROR_CHECK(esp_zb_color_control_cluster_add_attr(tw_color_cluster, ESP_ZB_ZCL_ATTR_COLOR_CONTROL_OPTIONS_ID,                        &zero_u8));
    ESP_ERROR_CHECK(esp_zb_color_control_cluster_add_attr(tw_color_cluster, ESP_ZB_ZCL_ATTR_COLOR_CONTROL_COLOR_TEMP_PHYSICAL_MIN_MIREDS_ID,  &mireds_min));
    ESP_ERROR_CHECK(esp_zb_color_control_cluster_add_attr(tw_color_cluster, ESP_ZB_ZCL_ATTR_COLOR_CONTROL_COLOR_TEMP_PHYSICAL_MAX_MIREDS_ID,  &mireds_max));
    ESP_ERROR_CHECK(esp_zb_color_control_cluster_add_attr(tw_color_cluster, ESP_ZB_ZCL_ATTR_COLOR_CONTROL_ENHANCED_COLOR_MODE_ID,            &enhanced_color_mode_tw));

    esp_zb_cluster_list_t *cluster_list_ep3 = esp_zb_zcl_cluster_list_create();
    ESP_ERROR_CHECK(esp_zb_cluster_list_add_basic_cluster   (cluster_list_ep3, BUILD_BASIC_CLUSTER(&basic_cfg),                           ESP_ZB_ZCL_CLUSTER_SERVER_ROLE));
    ESP_ERROR_CHECK(esp_zb_cluster_list_add_identify_cluster(cluster_list_ep3, esp_zb_identify_cluster_create(&identity_cfg),             ESP_ZB_ZCL_CLUSTER_SERVER_ROLE));
    ESP_ERROR_CHECK(esp_zb_cluster_list_add_groups_cluster  (cluster_list_ep3, esp_zb_groups_cluster_create(&groups_cfg),                 ESP_ZB_ZCL_CLUSTER_SERVER_ROLE));
    ESP_ERROR_CHECK(esp_zb_cluster_list_add_scenes_cluster  (cluster_list_ep3, esp_zb_scenes_cluster_create(&scenes_cfg),                 ESP_ZB_ZCL_CLUSTER_SERVER_ROLE));
    ESP_ERROR_CHECK(esp_zb_cluster_list_add_on_off_cluster  (cluster_list_ep3, esp_zb_on_off_cluster_create(&on_off_cfg),                 ESP_ZB_ZCL_CLUSTER_SERVER_ROLE));
    ESP_ERROR_CHECK(esp_zb_cluster_list_add_level_cluster   (cluster_list_ep3, esp_zb_level_cluster_create(&level_cfg),                   ESP_ZB_ZCL_CLUSTER_SERVER_ROLE));
    ESP_ERROR_CHECK(esp_zb_cluster_list_add_color_control_cluster(cluster_list_ep3, tw_color_cluster,                                     ESP_ZB_ZCL_CLUSTER_SERVER_ROLE));
    ESP_ERROR_CHECK(esp_zb_cluster_list_add_on_off_cluster  (cluster_list_ep3, esp_zb_zcl_attr_list_create(ESP_ZB_ZCL_CLUSTER_ID_ON_OFF),           ESP_ZB_ZCL_CLUSTER_CLIENT_ROLE));
    ESP_ERROR_CHECK(esp_zb_cluster_list_add_level_cluster   (cluster_list_ep3, esp_zb_zcl_attr_list_create(ESP_ZB_ZCL_CLUSTER_ID_LEVEL_CONTROL),    ESP_ZB_ZCL_CLUSTER_CLIENT_ROLE));
    ESP_ERROR_CHECK(esp_zb_cluster_list_add_color_control_cluster(cluster_list_ep3, esp_zb_zcl_attr_list_create(ESP_ZB_ZCL_CLUSTER_ID_COLOR_CONTROL), ESP_ZB_ZCL_CLUSTER_CLIENT_ROLE));

    /* ------------------------------------------------------------------ */
    /*  Register all endpoints                                              */
    /* ------------------------------------------------------------------ */
    esp_zb_ep_list_t *ep_list = esp_zb_ep_list_create();
    esp_zb_ep_list_add_ep(ep_list, cluster_list_ep1, ep1_config);
    esp_zb_ep_list_add_ep(ep_list, cluster_list_ep2, ep2_config);
    esp_zb_ep_list_add_ep(ep_list, cluster_list_ep3, ep3_config);

    esp_zb_device_register(ep_list);

    esp_zb_set_primary_network_channel_set(ESP_ZB_PRIMARY_CHANNEL_MASK);

#if CONFIG_FRILIGHT_DEBUG_RAW_CMDS
    esp_zb_raw_command_handler_register(zb_raw_command_handler);
#endif
    esp_zb_core_action_handler_register(zb_action_handler);

    ESP_ERROR_CHECK(esp_zb_start(false));
    esp_zb_stack_main_loop();
}

void app_main(void)
{
    esp_zb_platform_config_t config = {
        .radio_config = ESP_ZB_DEFAULT_RADIO_CONFIG(),
        .host_config = ESP_ZB_DEFAULT_HOST_CONFIG(),
    };
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_zb_platform_config(&config));
    xTaskCreate(esp_zb_task, "Zigbee_main", 4096, NULL, 5, NULL);
}
