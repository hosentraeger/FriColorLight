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
#include "zboss_api.h"  // für zb_zcl_parsed_hdr_t und ZB_BUF_GET_PARAM

#if !defined CONFIG_ZB_ZCZR
#error Define ZB_ZCZR in idf.py menuconfig to compile light (Router) source code.
#endif

#define CLAMP(x, lo, hi) ((x) < (lo) ? (lo) : ((x) > (hi) ? (hi) : (x)))

static uint8_t model_id[] = { 16, 'r','g','b','w','w','-','c','o','l','o','r','l','i','g','h','t' };
static uint8_t vendor[]   = { 14, 'r','e','d','f','i','v','e','d','e','s','i','g','n','s' };

static const char *TAG = "ESP_ZB_COLOR_DIMM_LIGHT";

typedef enum {
    LIGHT_PARAM_NONE = 0,
    LIGHT_PARAM_LEVEL,
    LIGHT_PARAM_COLORTEMP,
    LIGHT_PARAM_HUE,
} light_param_t;

typedef enum {
    LIGHT_MOVE_CONTINUOUS = 0,  // direction + rate, kein Zielwert
    LIGHT_MOVE_TO_TARGET,       // Zielwert, sofort oder mit Transition
} light_move_mode_t;

typedef struct {
    light_param_t     param;
    light_move_mode_t mode;
    uint16_t          target_value;  // Nur bei LIGHT_MOVE_TO_TARGET
    int8_t            direction;     // +1 = up, -1 = down, 0 = stop (nur CONTINUOUS)
    uint16_t          rate;          // Units/s (CONTINUOUS) oder 0 = sofort (TO_TARGET)
    uint8_t           endpoint;      // Ziel-Endpunkt für das Attribut
    bool              with_onoff;
} light_move_cmd_t;

static QueueHandle_t s_move_queue;

static void zb_report_attribute(uint8_t endpoint, uint16_t cluster_id, uint16_t attr_id);

static void light_off(uint8_t endpoint, uint8_t last_valid_level)
{
    uint8_t level = 0;
    ESP_LOGI(TAG, "LIGHT OFF: EP=%d, last_valid_level=%d", endpoint, last_valid_level);
    esp_zb_lock_acquire(portMAX_DELAY);
    
    // 1. OnLevel setzen (damit er beim nächsten 'On' bekannt ist)
    esp_zb_zcl_set_attribute_val(
        endpoint,
        ESP_ZB_ZCL_CLUSTER_ID_LEVEL_CONTROL,
        ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
        ESP_ZB_ZCL_ATTR_LEVEL_CONTROL_ON_LEVEL_ID,
        &last_valid_level, 
        false);

        esp_zb_zcl_set_attribute_val(
        endpoint,
        ESP_ZB_ZCL_CLUSTER_ID_LEVEL_CONTROL,
        ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
        ESP_ZB_ZCL_ATTR_LEVEL_CONTROL_CURRENT_LEVEL_ID,
        &level, 
        false);


    // 2. Hardware ausschalten
    switch ( endpoint )
    {
        case HA_COLOR_DIMMABLE_LIGHT_ENDPOINT_1:
            pwm_rgb_driver_set_power(false);
            break;
        case HA_COLOR_DIMMABLE_LIGHT_ENDPOINT_2:
            break;
        case HA_COLOR_TEMP_LIGHT_ENDPOINT:
            break;
        default:
            break;
    }
    // 3. OnOff-Attribut in der DB setzen
    uint8_t off = 0;
    esp_zb_zcl_set_attribute_val(
        endpoint, // Korrigiert von cmd.endpoint auf endpoint
        ESP_ZB_ZCL_CLUSTER_ID_ON_OFF,
        ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
        ESP_ZB_ZCL_ATTR_ON_OFF_ON_OFF_ID,
        &off, false);
        
    esp_zb_lock_release();

    // 4. Beides reporten
    zb_report_attribute(endpoint, ESP_ZB_ZCL_CLUSTER_ID_ON_OFF, ESP_ZB_ZCL_ATTR_ON_OFF_ON_OFF_ID);
//    zb_report_attribute(endpoint, ESP_ZB_ZCL_CLUSTER_ID_LEVEL_CONTROL, ESP_ZB_ZCL_ATTR_LEVEL_CONTROL_ON_LEVEL_ID);
}


static void light_set(uint8_t endpoint, uint8_t level, bool with_onoff)
{
    static TickType_t last_report_time = 0;
    TickType_t now = xTaskGetTickCount();

    ESP_LOGI(TAG, "LIGHT SET: EP=%d, level=%d, with_onoff=%d", endpoint, level, with_onoff);

    esp_zb_lock_acquire(portMAX_DELAY);

    // 1. Level in der ZCL-Datenbank aktualisieren
    esp_zb_zcl_set_attribute_val(
        endpoint,
        ESP_ZB_ZCL_CLUSTER_ID_LEVEL_CONTROL,
        ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
        ESP_ZB_ZCL_ATTR_LEVEL_CONTROL_CURRENT_LEVEL_ID,
        &level, 
        false);

    // 2. Hardware-Steuerung
    switch (endpoint)
    {
        case HA_COLOR_DIMMABLE_LIGHT_ENDPOINT_1:
            if ( with_onoff )
            {
                pwm_rgb_driver_set_power(true);
            }
            pwm_rgb_driver_set_level(level);
            break;
        default:
            break;
    }

    // 3. OnOff-Attribut in der DB auf true setzen
    if ( with_onoff )
    {
        uint8_t on = 1;
        esp_zb_zcl_set_attribute_val(
            endpoint,
            ESP_ZB_ZCL_CLUSTER_ID_ON_OFF,
            ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
            ESP_ZB_ZCL_ATTR_ON_OFF_ON_OFF_ID,
            &on, 
            false);
    };

    esp_zb_lock_release();

    // 4. Throttled Reporting
    // Wir prüfen, ob seit dem letzten Report mindestens 200ms vergangen sind
    if ((now - last_report_time) >= pdMS_TO_TICKS(200))
    {
        zb_report_attribute(endpoint, ESP_ZB_ZCL_CLUSTER_ID_ON_OFF, ESP_ZB_ZCL_ATTR_ON_OFF_ON_OFF_ID);
        zb_report_attribute(endpoint, ESP_ZB_ZCL_CLUSTER_ID_LEVEL_CONTROL, ESP_ZB_ZCL_ATTR_LEVEL_CONTROL_CURRENT_LEVEL_ID);
        
        last_report_time = now;
        ESP_LOGD("LIGHT_ON", "Reporting executed for endpoint %d", endpoint);
    }
}

static void rgb_light_move_task(void *pvParameters)
{
    light_move_cmd_t cmd = {0};

    while (true)
    {
        // Neues Command ohne Blockieren prüfen
        xQueueReceive(s_move_queue, &cmd, 0);

        if (cmd.param == LIGHT_PARAM_LEVEL)
        {
            uint8_t current = pwm_rgb_driver_get_level();
            uint8_t new_level = current;

            if (cmd.mode == LIGHT_MOVE_TO_TARGET)
            {
                // --- Zielwert-Modus ---
                if (cmd.rate == 0)
                {
                    // Sofort setzen
                    new_level = (uint8_t)CLAMP(cmd.target_value, 0, 255);
                    cmd.direction = 0; // einmalig, danach idle
                }
                else
                {
                    // Schrittweise zum Ziel
                    int16_t step = ((uint32_t)cmd.rate * 20) / 1000;
                    if (step < 1) step = 1;

                    if (current < cmd.target_value)
                    {
                        int16_t next = (int16_t)current + step;
                        new_level = (next >= cmd.target_value) ? (uint8_t)cmd.target_value : (uint8_t)next;
                    }
                    else if (current > cmd.target_value)
                    {
                        int16_t next = (int16_t)current - step;
                        new_level = (next <= cmd.target_value) ? (uint8_t)cmd.target_value : (uint8_t)next;
                    }

                    // Ziel erreicht → stoppen
                    if (new_level == cmd.target_value)
                        cmd.direction = 0;
                }
            }
            else // LIGHT_MOVE_CONTINUOUS
            {
                // --- Kontinuierlicher Modus ---
                if (cmd.direction != 0)
                {
                    int16_t step = ((uint32_t)cmd.rate * 20) / 1000;
                    if (step < 1) step = 1;

                    int16_t next = (int16_t)current + cmd.direction * step;
                    if (next >= 255) { next = 255; cmd.direction = 0; }
                    if (next <= 0)   { next = 0;   cmd.direction = 0; }
                    new_level = (uint8_t)next;
                }
            }

            if (new_level != current)
            {
                if ( new_level > 0 )
                {
                    light_set ( cmd.endpoint, new_level, pwm_rgb_driver_get_power() == false && new_level > 0 && cmd.with_onoff );
                    ESP_LOGI(TAG, "LGHT MV TASK: level=%d", new_level);
                }
                else
                {
                    light_off ( cmd.endpoint, current );
                    ESP_LOGI(TAG, "LGHT MV TASK: off, last valid level was %d", current);
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

static void zb_report_attribute(uint8_t endpoint, uint16_t cluster_id, uint16_t attr_id)
{
    esp_zb_zcl_report_attr_cmd_t cmd = {0};
    cmd.address_mode        = ESP_ZB_APS_ADDR_MODE_DST_ADDR_ENDP_NOT_PRESENT;
    cmd.attributeID         = attr_id;
    cmd.direction           = ESP_ZB_ZCL_CMD_DIRECTION_TO_CLI;
    cmd.clusterID           = cluster_id;
    cmd.zcl_basic_cmd.src_endpoint = endpoint;

    esp_zb_lock_acquire(portMAX_DELAY);
    esp_zb_zcl_report_attr_cmd_req(&cmd);
    esp_zb_lock_release();
}

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

    uint8_t endpoint = cmd_info->addr_data.common_data.dst_endpoint;
    if (endpoint != HA_COLOR_DIMMABLE_LIGHT_ENDPOINT_1 && endpoint != HA_COLOR_DIMMABLE_LIGHT_ENDPOINT_2 && endpoint != HA_COLOR_TEMP_LIGHT_ENDPOINT)
        return false;

    if (cmd_info->cluster_id == ESP_ZB_ZCL_CLUSTER_ID_ON_OFF) 
    {
        switch (cmd_info->cmd_id) 
        {
            case 0x00: // Off
                ESP_LOGI(TAG, "RAW CMD:   OFF");
                light_off(endpoint, pwm_rgb_driver_get_level());
                return true;

            case 0x01: // On
                ESP_LOGI(TAG, "RAW CMD:   ON");
                light_set(endpoint, pwm_rgb_driver_get_level() > 0 ? pwm_rgb_driver_get_level() : 128, true);
                return true;
                
            case 0x02: // Toggle
            {
                bool current_state = pwm_rgb_driver_get_power();
                bool new_state = !current_state;
                ESP_LOGI(TAG, "RAW CMD: TOGGLE to %d on EP %d", new_state, endpoint);
                if ( new_state )
                {
                    light_set(endpoint, pwm_rgb_driver_get_level() > 0 ? pwm_rgb_driver_get_level() : 128, true);
                }
                else
                {
                    light_off(endpoint, pwm_rgb_driver_get_level());
                }                
                return true; 
            }
        }
    }

    if (cmd_info->cluster_id == ESP_ZB_ZCL_CLUSTER_ID_LEVEL_CONTROL) 
    {
        // Payload nach dem Header auslesen
        uint8_t *payload = (uint8_t *)zb_buf_begin(bufid);
        switch (cmd_info->cmd_id) 
        {
            case 0x00: // Move to Level
            {
                ESP_LOGI(TAG, "RAW CMD:   Move to Level");
                light_move_cmd_t cmd = {
                    .param        = LIGHT_PARAM_LEVEL,
                    .mode         = LIGHT_MOVE_TO_TARGET,
                    .target_value = payload[0],
                    .rate         = (uint16_t)(payload[1] | (payload[2] << 8)),
                    .with_onoff   = false
                };
                xQueueOverwrite(s_move_queue, &cmd);
                return true;
            }

            case 0x01: // Move
            {
                ESP_LOGI(TAG, "RAW CMD:   Move");
                light_move_cmd_t cmd = {
                    .param      = LIGHT_PARAM_LEVEL,
                    .mode       = LIGHT_MOVE_CONTINUOUS,
                    .direction  = (payload[0] == 0x00) ? +1 : -1,
                    .rate       = payload[1],
                    .with_onoff = false
                };
                xQueueOverwrite(s_move_queue, &cmd);
                return true;
            }

            case 0x02: // Step
                ESP_LOGI(TAG, "RAW CMD:   Step");
                break;

            case 0x03: // Stop
            {
                ESP_LOGI(TAG, "RAW CMD:   Stop");
                light_move_cmd_t cmd = { .param = LIGHT_PARAM_LEVEL, .direction = 0 };
                xQueueOverwrite(s_move_queue, &cmd);
                zb_report_attribute(
                    endpoint,
                    ESP_ZB_ZCL_CLUSTER_ID_LEVEL_CONTROL,
                    ESP_ZB_ZCL_ATTR_LEVEL_CONTROL_CURRENT_LEVEL_ID);

                return true;
            }

            case 0x04: // Move to Level (with On/Off)
            {
                ESP_LOGI(TAG, "RAW CMD:   Move to Level with On/Off");
                light_move_cmd_t cmd = {
                    .param        = LIGHT_PARAM_LEVEL,
                    .mode         = LIGHT_MOVE_TO_TARGET,
                    .target_value = payload[0],
                    .rate         = (uint16_t)(payload[1] | (payload[2] << 8)),
                    .with_onoff   = true
                };
                xQueueOverwrite(s_move_queue, &cmd);
                return true;
            }

            case 0x05: // Move (with On/Off)
            {
                ESP_LOGI(TAG, "RAW CMD:   Move with On/Off");
                light_move_cmd_t cmd = {
                    .param      = LIGHT_PARAM_LEVEL,
                    .mode       = LIGHT_MOVE_CONTINUOUS,
                    .direction  = (payload[0] == 0x00) ? +1 : -1,
                    .rate       = payload[1],
                    .with_onoff = true
                };
                xQueueOverwrite(s_move_queue, &cmd);
                return true;
            }

            default:
                return false;
        }
    }

    return false; // alle anderen Commands normal verarbeiten lassen
}

static esp_err_t zb_action_handler(esp_zb_core_action_callback_id_t callback_id, const void *message)
{
    ESP_LOGI(TAG, "ACTION: callback_id=0x%x", callback_id);
    
    if (callback_id == ESP_ZB_CORE_SET_ATTR_VALUE_CB_ID) {
        esp_zb_zcl_set_attr_value_message_t *msg = (esp_zb_zcl_set_attr_value_message_t *)message;
        ESP_LOGI(TAG, "ACTION: SET_ATTR ep=%d cluster=0x%04x attr=0x%04x",
                 msg->info.dst_endpoint,
                 msg->info.cluster,
                 msg->attribute.id);
    }
    return ESP_OK;
}

/********************* Define functions **************************/
static esp_err_t deferred_driver_init(void)
{
    static bool is_inited = false;
    if (!is_inited) 
    {
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
    esp_zb_cfg_t zb_nwk_cfg = ESP_ZB_ZR_CONFIG();
    esp_zb_init(&zb_nwk_cfg);

    zcl_basic_manufacturer_info_t info = {
        .manufacturer_name = (char *)vendor,
        .model_identifier = (char *)model_id,
    };

    // ── Endpunkt 1: RGB (bestehend) ──────────────────────────────────────
    esp_zb_color_dimmable_light_cfg_t light_cfg_1 = ESP_ZB_DEFAULT_COLOR_DIMMABLE_LIGHT_CONFIG();
    esp_zb_ep_list_t *ep_list = esp_zb_color_dimmable_light_ep_create(
        HA_COLOR_DIMMABLE_LIGHT_ENDPOINT_1, &light_cfg_1);
    esp_zcl_utility_add_ep_basic_manufacturer_info(
        ep_list, HA_COLOR_DIMMABLE_LIGHT_ENDPOINT_1, &info);

    // ── Endpunkt 2: RGB ──────────────────────────────────────────────────
    esp_zb_color_dimmable_light_cfg_t light_cfg_2 = ESP_ZB_DEFAULT_COLOR_DIMMABLE_LIGHT_CONFIG();
    esp_zb_cluster_list_t *cluster_list_2 = esp_zb_color_dimmable_light_clusters_create(&light_cfg_2);

    esp_zb_endpoint_config_t ep2_cfg = {
        .endpoint       = HA_COLOR_DIMMABLE_LIGHT_ENDPOINT_2,
        .app_profile_id = ESP_ZB_AF_HA_PROFILE_ID,
        .app_device_id  = ESP_ZB_HA_COLOR_DIMMABLE_LIGHT_DEVICE_ID,
        .app_device_version = 0,
    };
    esp_zb_ep_list_add_ep(ep_list, cluster_list_2, ep2_cfg);
    esp_zcl_utility_add_ep_basic_manufacturer_info(
        ep_list, HA_COLOR_DIMMABLE_LIGHT_ENDPOINT_2, &info);

    // ── Endpunkt 3: Tunable White ────────────────────────────────────────
    esp_zb_color_dimmable_light_cfg_t tw_cfg = ESP_ZB_DEFAULT_COLOR_DIMMABLE_LIGHT_CONFIG();

    // Color Temperature Mode erzwingen:
    // color_mode = 2 → Color Temperature
    // color_capabilities: Bit 4 = Color Temperature supported
    tw_cfg.color_cfg.color_mode          = 0x02;   // ESP_ZB_ZCL_COLOR_CONTROL_COLOR_MODE_TEMPERATURE
    tw_cfg.color_cfg.enhanced_color_mode = 0x02;
    tw_cfg.color_cfg.color_capabilities  = 0x0010; // Bit 4: Color Temperature capability

    esp_zb_cluster_list_t *cluster_list_3 = esp_zb_color_dimmable_light_clusters_create(&tw_cfg);

    // colorTempPhysicalMin (0x400B) und colorTempPhysicalMax (0x400C) setzen
    esp_zb_attribute_list_t *color_attrs = esp_zb_cluster_list_get_cluster(
        cluster_list_3,
        ESP_ZB_ZCL_CLUSTER_ID_COLOR_CONTROL,
        ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

    uint16_t ct_min = 153;  // 6500K
    uint16_t ct_max = 500;  // 2000K
    esp_zb_color_control_cluster_add_attr(color_attrs,
        ESP_ZB_ZCL_ATTR_COLOR_CONTROL_COLOR_TEMP_PHYSICAL_MIN_MIREDS_ID, &ct_min);
    esp_zb_color_control_cluster_add_attr(color_attrs,
        ESP_ZB_ZCL_ATTR_COLOR_CONTROL_COLOR_TEMP_PHYSICAL_MAX_MIREDS_ID, &ct_max);

    esp_zb_endpoint_config_t ep3_cfg = {
        .endpoint           = HA_COLOR_TEMP_LIGHT_ENDPOINT,
        .app_profile_id     = ESP_ZB_AF_HA_PROFILE_ID,
        .app_device_id      = 0x010C,   // Color Temperature Light (Zigbee HA Spec)
        .app_device_version = 0,
    };
    esp_zb_ep_list_add_ep(ep_list, cluster_list_3, ep3_cfg);
        esp_zcl_utility_add_ep_basic_manufacturer_info(
        ep_list, HA_COLOR_TEMP_LIGHT_ENDPOINT, &info);


    // ── Alle Endpunkte registrieren ──────────────────────────────────────
    esp_zb_device_register(ep_list);

    esp_zb_set_primary_network_channel_set(ESP_ZB_PRIMARY_CHANNEL_MASK);
    esp_zb_raw_command_handler_register(zb_raw_command_handler);
    s_move_queue = xQueueCreate(1, sizeof(light_move_cmd_t));
    xTaskCreate(rgb_light_move_task, "rgb_light_move", 2048, NULL, 5, NULL);


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
