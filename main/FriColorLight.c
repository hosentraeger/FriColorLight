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
#include "esp_check.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ha/esp_zigbee_ha_standard.h"
#include "zcl_utility.h"

#if !defined CONFIG_ZB_ZCZR
#error Define ZB_ZCZR in idf.py menuconfig to compile light (Router) source code.
#endif

static uint8_t model_id[] = { 16, 'r','g','b','w','w','-','c','o','l','o','r','l','i','g','h','t' };
static uint8_t vendor[]   = { 14, 'r','e','d','f','i','v','e','d','e','s','i','g','n','s' };

static const char *TAG = "ESP_ZB_COLOR_DIMM_LIGHT";


// Claude -->
#include "zboss_api.h"  // für zb_zcl_parsed_hdr_t und ZB_BUF_GET_PARAM
typedef enum {
    LIGHT_PARAM_NONE = 0,
    LIGHT_PARAM_LEVEL,
    LIGHT_PARAM_COLORTEMP,
    LIGHT_PARAM_HUE,
} light_param_t;

typedef struct {
    light_param_t param;
    int8_t direction;   // +1 = up, -1 = down, 0 = stop
    uint8_t rate;       // Units pro Sekunde laut ZCL
    uint8_t start_level; // für Move to Level: Start-Level, sonst 0
} light_move_cmd_t;

static QueueHandle_t s_move_queue;

static void light_move_task(void *pvParameters)
{
    light_move_cmd_t cmd = {0};
    uint8_t s_level = 0;

    while (true) {
        // Neues Command ohne Blockieren prüfen
        xQueueReceive(s_move_queue, &cmd, 0);

        if (cmd.direction != 0 && cmd.param == LIGHT_PARAM_LEVEL) {
            int16_t step = (cmd.rate * 20) / 1000; // bei 20ms Tick
            if (step < 1) step = 1;
            int16_t new_level = s_level + cmd.direction * step;
            if (new_level >= 255) { new_level = 255; cmd.direction = 0; }
            if (new_level <= 0)   { new_level = 0;   cmd.direction = 0; }
            s_level = (uint8_t)new_level;
            pwm_rgb_driver_set_level(s_level);
            ESP_LOGI(TAG, "Moving level: %d", s_level);
        }

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

static bool zb_raw_command_handler(uint8_t bufid)
{
    zb_zcl_parsed_hdr_t *cmd_info = ZB_BUF_GET_PARAM(bufid, zb_zcl_parsed_hdr_t);
    uint8_t *payload = (uint8_t *)zb_buf_begin(bufid);
    uint8_t payload_len = zb_buf_len(bufid);

    // Alles loggen was reinkommt
    ESP_LOGI(TAG, "RAW CMD: cluster=0x%04x, cmd=0x%02x, ep=%d, payload_len=%d",
             cmd_info->cluster_id,
             cmd_info->cmd_id,
             cmd_info->addr_data.common_data.dst_endpoint,
             payload_len);

    // Payload bytes roh ausgeben
    for (int i = 0; i < payload_len; i++) {
        ESP_LOGI(TAG, "  payload[%d] = 0x%02x (%d)", i, payload[i], payload[i]);
    }

    // Nur unseren Endpoint behandeln
    if (cmd_info->addr_data.common_data.dst_endpoint != HA_COLOR_DIMMABLE_LIGHT_ENDPOINT) {
        return false; // nicht behandelt → Stack verarbeitet weiter
    }

    if (cmd_info->cluster_id == ESP_ZB_ZCL_CLUSTER_ID_ON_OFF) {
        switch (cmd_info->cmd_id) {
            case 0x00: // Off
                esp_zb_zcl_set_attribute_val(
                    HA_COLOR_DIMMABLE_LIGHT_ENDPOINT,
                    ESP_ZB_ZCL_CLUSTER_ID_ON_OFF,
                    ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
                    ESP_ZB_ZCL_ATTR_ON_OFF_ON_OFF_ID,
                    &(bool){false}, false);
                pwm_rgb_driver_set_power(false);
                return true;

            case 0x01: // On
                esp_zb_zcl_set_attribute_val(
                    HA_COLOR_DIMMABLE_LIGHT_ENDPOINT,
                    ESP_ZB_ZCL_CLUSTER_ID_ON_OFF,
                    ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
                    ESP_ZB_ZCL_ATTR_ON_OFF_ON_OFF_ID,
                    &(bool){true}, false);
                pwm_rgb_driver_set_power(true);
                return true;
            case 0x02: // Toggle
                ESP_LOGI(TAG, "RAW: Toggle");
                // aktuellen Zustand aus Attribut lesen
                bool *cur = (bool *)esp_zb_zcl_get_attribute(
                    HA_COLOR_DIMMABLE_LIGHT_ENDPOINT,
                    ESP_ZB_ZCL_CLUSTER_ID_ON_OFF,
                    ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
                    ESP_ZB_ZCL_ATTR_ON_OFF_ON_OFF_ID)->data_p;
                bool new_state = !(*cur);
                esp_zb_zcl_set_attribute_val(
                    HA_COLOR_DIMMABLE_LIGHT_ENDPOINT,
                    ESP_ZB_ZCL_CLUSTER_ID_ON_OFF,
                    ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
                    ESP_ZB_ZCL_ATTR_ON_OFF_ON_OFF_ID,
                    &new_state, false);
                pwm_rgb_driver_set_power(new_state);
                return true;
        }
    }

    if (cmd_info->cluster_id == ESP_ZB_ZCL_CLUSTER_ID_LEVEL_CONTROL) {
        // Payload nach dem Header auslesen
        uint8_t *payload = (uint8_t *)zb_buf_begin(bufid);
        switch (cmd_info->cmd_id) {
            case 0x00: // Move to Level
                ESP_LOGI(TAG, "### RAW: Move to Level ###");
                break;

            case 0x01: // Move
            {
                ESP_LOGI(TAG, "### RAW: Move ###");

                uint8_t current_level = *(uint8_t *)esp_zb_zcl_get_attribute(
                    HA_COLOR_DIMMABLE_LIGHT_ENDPOINT,
                    ESP_ZB_ZCL_CLUSTER_ID_LEVEL_CONTROL,
                    ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
                    ESP_ZB_ZCL_ATTR_LEVEL_CONTROL_CURRENT_LEVEL_ID)->data_p;

                light_move_cmd_t cmd = {
                        .param = LIGHT_PARAM_LEVEL,
                        .direction = (payload[0] == 0x00) ? +1 : -1,
                        .rate = payload[1],
                        .start_level = current_level,
                    };
                    BaseType_t result = xQueueOverwrite(s_move_queue, &cmd);
                    ESP_LOGI(TAG, "Queue result: %d, dir=%d, rate=%d", result, cmd.direction, cmd.rate);

                    return true;
            }

            case 0x02: // Step
                ESP_LOGI(TAG, "### RAW: Step ###");
                break;

            case 0x03: // Stop
            {
                ESP_LOGI(TAG, "### RAW: Stop ###");
                light_move_cmd_t cmd = { .param = LIGHT_PARAM_LEVEL, .direction = 0 };
                xQueueOverwrite(s_move_queue, &cmd);
                return true;
            }

            case 0x04: // Move to Level (with On/Off)
                ESP_LOGI(TAG, "### RAW: Move to Level with On/Off ###");
                break;

            default:
                break;
        }
    }

    return false; // alle anderen Commands normal verarbeiten lassen
}

// <-- Claude

/********************* Define functions **************************/
static esp_err_t deferred_driver_init(void)
{
    static bool is_inited = false;
    if (!is_inited) {
        pwm_rgb_driver_init(PWM_RED_PIN, PWM_GREEN_PIN, PWM_BLUE_PIN );
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
    switch (sig_type) {
    case ESP_ZB_ZDO_SIGNAL_SKIP_STARTUP:
        ESP_LOGI(TAG, "Initialize Zigbee stack");
        esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_INITIALIZATION);
        break;
    case ESP_ZB_BDB_SIGNAL_DEVICE_FIRST_START:
    case ESP_ZB_BDB_SIGNAL_DEVICE_REBOOT:
        if (err_status == ESP_OK) {
            ESP_LOGI(TAG, "Deferred driver initialization %s", deferred_driver_init() ? "failed" : "successful");
            ESP_LOGI(TAG, "Device started up in%s factory-reset mode", esp_zb_bdb_is_factory_new() ? "" : " non");
            if (esp_zb_bdb_is_factory_new()) {
                ESP_LOGI(TAG, "Start network steering");
                esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_NETWORK_STEERING);
            } else {
                ESP_LOGI(TAG, "Device rebooted");
            }
        } else {
            ESP_LOGW(TAG, "%s failed with status: %s, retrying", esp_zb_zdo_signal_to_string(sig_type),
                     esp_err_to_name(err_status));
            esp_zb_scheduler_alarm((esp_zb_callback_t)bdb_start_top_level_commissioning_cb,
                                   ESP_ZB_BDB_MODE_INITIALIZATION, 1000);
        }
        break;
    case ESP_ZB_BDB_SIGNAL_STEERING:
        if (err_status == ESP_OK) {
            esp_zb_ieee_addr_t extended_pan_id;
            esp_zb_get_extended_pan_id(extended_pan_id);
            ESP_LOGI(TAG, "Joined network successfully (Extended PAN ID: %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x, PAN ID: 0x%04hx, Channel:%d, Short Address: 0x%04hx)",
                     extended_pan_id[7], extended_pan_id[6], extended_pan_id[5], extended_pan_id[4],
                     extended_pan_id[3], extended_pan_id[2], extended_pan_id[1], extended_pan_id[0],
                     esp_zb_get_pan_id(), esp_zb_get_current_channel(), esp_zb_get_short_address());
        } else {
            ESP_LOGI(TAG, "Network steering was not successful (status: %s)", esp_err_to_name(err_status));
            esp_zb_scheduler_alarm((esp_zb_callback_t)bdb_start_top_level_commissioning_cb, ESP_ZB_BDB_MODE_NETWORK_STEERING, 1000);
        }
        break;
    case ESP_ZB_NWK_SIGNAL_PERMIT_JOIN_STATUS:
        if (err_status == ESP_OK) {
            if (*(uint8_t *)esp_zb_app_signal_get_params(p_sg_p)) {
                ESP_LOGI(TAG, "Network(0x%04hx) is open for %d seconds", esp_zb_get_pan_id(), *(uint8_t *)esp_zb_app_signal_get_params(p_sg_p));
            } else {
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

static esp_err_t zb_attribute_handler(const esp_zb_zcl_set_attr_value_message_t *message)
{
    esp_err_t ret = ESP_OK;
    bool light_state = 0;
    uint8_t light_level = 0;
    uint16_t light_color_x = 0;
    uint16_t light_color_y = 0;
    ESP_RETURN_ON_FALSE(message, ESP_FAIL, TAG, "Empty message");
    ESP_RETURN_ON_FALSE(message->info.status == ESP_ZB_ZCL_STATUS_SUCCESS, ESP_ERR_INVALID_ARG, TAG, "Received message: error status(%d)",
                        message->info.status);
    ESP_LOGI(TAG, "Received message: endpoint(%d), cluster(0x%x), attribute(0x%x), data size(%d)", message->info.dst_endpoint, message->info.cluster,
             message->attribute.id, message->attribute.data.size);
    if (message->info.dst_endpoint == HA_COLOR_DIMMABLE_LIGHT_ENDPOINT) {
        switch (message->info.cluster) {
        case ESP_ZB_ZCL_CLUSTER_ID_ON_OFF:
            if (message->attribute.id == ESP_ZB_ZCL_ATTR_ON_OFF_ON_OFF_ID && message->attribute.data.type == ESP_ZB_ZCL_ATTR_TYPE_BOOL) {
                light_state = message->attribute.data.value ? *(bool *)message->attribute.data.value : light_state;
                ESP_LOGI(TAG, "Light sets to %s", light_state ? "On" : "Off");
                pwm_rgb_driver_set_power(light_state);
            } else {
                ESP_LOGW(TAG, "On/Off cluster data: attribute(0x%x), type(0x%x)", message->attribute.id, message->attribute.data.type);
            }
            break;
        case ESP_ZB_ZCL_CLUSTER_ID_COLOR_CONTROL:
            if (message->attribute.id == ESP_ZB_ZCL_ATTR_COLOR_CONTROL_CURRENT_X_ID && message->attribute.data.type == ESP_ZB_ZCL_ATTR_TYPE_U16) {
                light_color_x = message->attribute.data.value ? *(uint16_t *)message->attribute.data.value : light_color_x;
                light_color_y = *(uint16_t *)esp_zb_zcl_get_attribute(message->info.dst_endpoint, message->info.cluster,
                                                                      ESP_ZB_ZCL_CLUSTER_SERVER_ROLE, ESP_ZB_ZCL_ATTR_COLOR_CONTROL_CURRENT_Y_ID)
                                     ->data_p;
                ESP_LOGI(TAG, "Light color x changes to 0x%x", light_color_x);
            } else if (message->attribute.id == ESP_ZB_ZCL_ATTR_COLOR_CONTROL_CURRENT_Y_ID &&
                       message->attribute.data.type == ESP_ZB_ZCL_ATTR_TYPE_U16) {
                light_color_y = message->attribute.data.value ? *(uint16_t *)message->attribute.data.value : light_color_y;
                light_color_x = *(uint16_t *)esp_zb_zcl_get_attribute(message->info.dst_endpoint, message->info.cluster,
                                                                      ESP_ZB_ZCL_CLUSTER_SERVER_ROLE, ESP_ZB_ZCL_ATTR_COLOR_CONTROL_CURRENT_X_ID)
                                     ->data_p;
                ESP_LOGI(TAG, "Light color y changes to 0x%x", light_color_y);
            } else {
                ESP_LOGW(TAG, "Color control cluster data: attribute(0x%x), type(0x%x)", message->attribute.id, message->attribute.data.type);
            }
            pwm_rgb_driver_set_color_xy(light_color_x, light_color_y);
            break;
        case ESP_ZB_ZCL_CLUSTER_ID_LEVEL_CONTROL:
            if (message->attribute.id == ESP_ZB_ZCL_ATTR_LEVEL_CONTROL_CURRENT_LEVEL_ID && message->attribute.data.type == ESP_ZB_ZCL_ATTR_TYPE_U8) {
                light_level = message->attribute.data.value ? *(uint8_t *)message->attribute.data.value : light_level;
                pwm_rgb_driver_set_level((uint8_t)light_level);
                ESP_LOGI(TAG, "Light level changes to %d", light_level);
            } else {
                ESP_LOGW(TAG, "Level Control cluster data: attribute(0x%x), type(0x%x)", message->attribute.id, message->attribute.data.type);
            }
            break;
        default:
            ESP_LOGI(TAG, "Message data: cluster(0x%x), attribute(0x%x)  ", message->info.cluster, message->attribute.id);
        }
    }
    return ret;
}

static esp_err_t zb_action_handler(esp_zb_core_action_callback_id_t callback_id, const void *message)
{
    esp_err_t ret = ESP_OK;
    switch (callback_id) {
    case ESP_ZB_CORE_SET_ATTR_VALUE_CB_ID:
        ret = zb_attribute_handler((esp_zb_zcl_set_attr_value_message_t *)message);
        break;

    case ESP_ZB_CORE_CMD_CUSTOM_CLUSTER_REQ_CB_ID: {
        esp_zb_zcl_custom_cluster_command_message_t *cmd = 
            (esp_zb_zcl_custom_cluster_command_message_t *)message;

        if (cmd->info.cluster == ESP_ZB_ZCL_CLUSTER_ID_ON_OFF) {
            switch (cmd->info.command.id) {
                case ESP_ZB_ZCL_CMD_ON_OFF_OFF_ID:   // 0x00
                    ESP_LOGI(TAG, "Command: Off");
                    pwm_rgb_driver_set_power(false);
                    break;
                case ESP_ZB_ZCL_CMD_ON_OFF_ON_ID:    // 0x01
                    ESP_LOGI(TAG, "Command: On");
                    pwm_rgb_driver_set_power(true);
                    break;
                case ESP_ZB_ZCL_CMD_ON_OFF_TOGGLE_ID: // 0x02
                    ESP_LOGI(TAG, "Command: Toggle");
                    // Toggle-Zustand aus dem Attribut lesen
                    bool current = *(bool *)esp_zb_zcl_get_attribute(
                        cmd->info.dst_endpoint,
                        ESP_ZB_ZCL_CLUSTER_ID_ON_OFF,
                        ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
                        ESP_ZB_ZCL_ATTR_ON_OFF_ON_OFF_ID)->data_p;
                    pwm_rgb_driver_set_power(!current);
                    break;
            }
        } else if (cmd->info.cluster == ESP_ZB_ZCL_CLUSTER_ID_LEVEL_CONTROL) {
            switch (cmd->info.command.id) {
                case ESP_ZB_ZCL_CMD_LEVEL_CONTROL_MOVE_TO_LEVEL_WITH_ON_OFF: // 0x04
                case ESP_ZB_ZCL_CMD_LEVEL_CONTROL_MOVE_TO_LEVEL: {           // 0x00
                    uint8_t level = ((uint8_t *)cmd->data.value)[0];
                    ESP_LOGI(TAG, "MoveToLevel: %d", level);
                    pwm_rgb_driver_set_level(level);
                    // Bei 0x04: auch Power-State setzen
                    if (cmd->info.command.id == 
                        ESP_ZB_ZCL_CMD_LEVEL_CONTROL_MOVE_TO_LEVEL_WITH_ON_OFF) {
                        pwm_rgb_driver_set_power(level > 0);
                    }
                    break;
                }
            }
        }
        break;
    }

    default:
        ESP_LOGW(TAG, "Receive Zigbee action(0x%x) callback", callback_id);
        break;
    }
    return ret;
}

static void esp_zb_task(void *pvParameters)
{
    /* initialize Zigbee stack */
    esp_zb_cfg_t zb_nwk_cfg = ESP_ZB_ZR_CONFIG();
    esp_zb_init(&zb_nwk_cfg);
    esp_zb_color_dimmable_light_cfg_t light_cfg = ESP_ZB_DEFAULT_COLOR_DIMMABLE_LIGHT_CONFIG();
    esp_zb_ep_list_t *esp_zb_color_dimmable_light_ep = esp_zb_color_dimmable_light_ep_create(HA_COLOR_DIMMABLE_LIGHT_ENDPOINT, &light_cfg);

    zcl_basic_manufacturer_info_t info = {
        .manufacturer_name = (char *)vendor,
        .model_identifier = (char *)model_id,
    };

    esp_zcl_utility_add_ep_basic_manufacturer_info(esp_zb_color_dimmable_light_ep, HA_COLOR_DIMMABLE_LIGHT_ENDPOINT, &info);
    esp_zb_device_register(esp_zb_color_dimmable_light_ep);
    esp_zb_core_action_handler_register(zb_action_handler);
    esp_zb_set_primary_network_channel_set(ESP_ZB_PRIMARY_CHANNEL_MASK);
    
    // Claude -->
    esp_zb_raw_command_handler_register(zb_raw_command_handler);
    s_move_queue = xQueueCreate(1, sizeof(light_move_cmd_t));
    xTaskCreate(light_move_task, "light_move", 2048, NULL, 5, NULL);
    // <-- Claude
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
