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

#include "zcl/esp_zigbee_zcl_on_off.h"
#include "zcl/esp_zigbee_zcl_level.h"
#include "zcl/esp_zigbee_zcl_color_control.h"
#include "zboss_api_buf.h"

static uint16_t s_color_x = ESP_ZB_ZCL_COLOR_CONTROL_CURRENT_X_DEF_VALUE;
static uint16_t s_color_y = ESP_ZB_ZCL_COLOR_CONTROL_CURRENT_Y_DEF_VALUE;


#if !defined CONFIG_ZB_ZCZR
#error Define ZB_ZCZR in idf.py menuconfig to compile light (Router) source code.
#endif

#define CLAMP(x, lo, hi) ((x) < (lo) ? (lo) : ((x) > (hi) ? (hi) : (x)))

static uint8_t model_id[] = { 16, 'r','g','b','w','w','-','c','o','l','o','r','l','i','g','h','t' };
static uint8_t vendor[]   = { 14, 'r','e','d','f','i','v','e','d','e','s','i','g','n','s' };

static const char *TAG = "ESP_ZB_COLOR_DIMM_LIGHT";


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

/*
    esp_zb_zcl_attr_t *x_attr = esp_zb_zcl_get_attribute(
        HA_COLOR_DIMMABLE_LIGHT_ENDPOINT_1,
        ESP_ZB_ZCL_CLUSTER_ID_COLOR_CONTROL,
        ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
        ESP_ZB_ZCL_ATTR_COLOR_CONTROL_CURRENT_X_ID
    );

    esp_zb_zcl_attr_t *y_attr = esp_zb_zcl_get_attribute(
        HA_COLOR_DIMMABLE_LIGHT_ENDPOINT_1,
        ESP_ZB_ZCL_CLUSTER_ID_COLOR_CONTROL,
        ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
        ESP_ZB_ZCL_ATTR_COLOR_CONTROL_CURRENT_Y_ID
    );

    uint16_t x_val = x_attr ? *(uint16_t*)x_attr->data_p : 0xFFFF;
    uint16_t y_val = y_attr ? *(uint16_t*)y_attr->data_p : 0xFFFF;
    ESP_LOGI(TAG, "RAW: stored XY = (%u, %u)", x_val, y_val);
*/

    return false;

/*
    ESP_LOGI(TAG, "buf_len=%d", zb_buf_len(bufid));
    uint8_t *raw = (uint8_t *)zb_buf_begin(bufid);
    ESP_LOGI(TAG, "raw bytes: %02x %02x %02x %02x %02x %02x %02x %02x",
            raw[0], raw[1], raw[2], raw[3], raw[4], raw[5], raw[6], raw[7]);

    if (cmd_info->cluster_id != ESP_ZB_ZCL_CLUSTER_ID_COLOR_CONTROL) {
        return false;
    }

    ESP_LOGI(TAG, "COLOR CMD: cmd_id=0x%02x", cmd_info->cmd_id);

    switch (cmd_info->cmd_id)
    {
        case ZB_ZCL_CMD_COLOR_CONTROL_MOVE_TO_COLOR: {
            ESP_LOGI(TAG, "MOVE_TO_COLOR: parsing...");
            zb_zcl_color_control_move_to_color_req_t req;
            zb_zcl_parse_status_t status;
            ZB_ZCL_COLOR_CONTROL_GET_MOVE_TO_COLOR_REQ(bufid, req, status);

            ESP_LOGI(TAG, "MOVE_TO_COLOR: status=%d x=%d y=%d trans=%d",
                     status, req.color_x, req.color_y, req.transition_time);

            if (status != ZB_ZCL_PARSE_STATUS_SUCCESS) {
                ESP_LOGW(TAG, "MOVE_TO_COLOR parse failed");
                return false;
            }

            if (req.color_x == 0 && req.color_y == 0) {
                ESP_LOGI(TAG, "MOVE_TO_COLOR 0/0 ignoriert");
                return false;
            }

            s_color_x = req.color_x;
            s_color_y = req.color_y;
            pwm_rgb_driver_set_color_xy(s_color_x, s_color_y);
            pwm_driver_apply();

            esp_zb_zcl_set_attribute_val(
                HA_COLOR_DIMMABLE_LIGHT_ENDPOINT_1,
                ESP_ZB_ZCL_CLUSTER_ID_COLOR_CONTROL,
                ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
                ESP_ZB_ZCL_ATTR_COLOR_CONTROL_CURRENT_X_ID,
                &s_color_x, false);
            esp_zb_zcl_set_attribute_val(
                HA_COLOR_DIMMABLE_LIGHT_ENDPOINT_1,
                ESP_ZB_ZCL_CLUSTER_ID_COLOR_CONTROL,
                ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
                ESP_ZB_ZCL_ATTR_COLOR_CONTROL_CURRENT_Y_ID,
                &s_color_y, false);
            break;
        }

        case ZB_ZCL_CMD_COLOR_CONTROL_MOVE_TO_COLOR_TEMPERATURE: {
            ESP_LOGI(TAG, "MOVE_TO_COLOR_TEMP: parsing...");
            zb_zcl_color_control_move_to_color_temperature_req_t req;
            zb_zcl_parse_status_t status;
            ZB_ZCL_COLOR_CONTROL_GET_MOVE_TO_COLOR_TEMPERATURE_REQ(bufid, req, status);

            ESP_LOGI(TAG, "MOVE_TO_COLOR_TEMP: status=%d mireds=%d trans=%d",
                     status, req.color_temperature, req.transition_time);

            if (status != ZB_ZCL_PARSE_STATUS_SUCCESS) {
                ESP_LOGW(TAG, "MOVE_TO_COLOR_TEMP parse failed");
                return false;
            }
            // für RGB-Leuchte vorerst ignorieren
            break;
        }

        default:
            ESP_LOGI(TAG, "COLOR CMD 0x%02x unbehandelt", cmd_info->cmd_id);
            break;
    }

    return false;
*/
}

static esp_err_t zb_action_handler(esp_zb_core_action_callback_id_t callback_id, const void *message)
{
    if (callback_id != ESP_ZB_CORE_SET_ATTR_VALUE_CB_ID) {
        ESP_LOGD(TAG, "unhandled CB 0x%04x", callback_id);
        return ESP_OK;
    }

    esp_zb_zcl_attr_t *attr_x_ptr = esp_zb_zcl_get_attribute(
        HA_COLOR_DIMMABLE_LIGHT_ENDPOINT_1,
        ESP_ZB_ZCL_CLUSTER_ID_COLOR_CONTROL,
        ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
        ESP_ZB_ZCL_ATTR_COLOR_CONTROL_CURRENT_X_ID
    );

    esp_zb_zcl_attr_t *attr_y_ptr = esp_zb_zcl_get_attribute(
        HA_COLOR_DIMMABLE_LIGHT_ENDPOINT_1,
        ESP_ZB_ZCL_CLUSTER_ID_COLOR_CONTROL,
        ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
        ESP_ZB_ZCL_ATTR_COLOR_CONTROL_CURRENT_Y_ID
    );

    uint16_t x_val = attr_x_ptr ? *(uint16_t*)attr_x_ptr->data_p : 0xFFFF;
    uint16_t y_val = attr_y_ptr ? *(uint16_t*)attr_y_ptr->data_p : 0xFFFF;

    ESP_LOGI(TAG, "ACTION: stored XY = (%u, %u)", x_val, y_val);

    esp_zb_zcl_set_attr_value_message_t *msg =
        (esp_zb_zcl_set_attr_value_message_t *)message;

    uint16_t cluster = msg->info.cluster;
    uint16_t attr    = msg->attribute.id;

    ESP_LOGI(TAG, "ACTION: cluster=0x%04X attr=0x%04X", cluster, attr);

    switch (cluster) {

        // ── ON / OFF ──────────────────────────────────────────────────────
        case ESP_ZB_ZCL_CLUSTER_ID_ON_OFF: {
            if (attr == ESP_ZB_ZCL_ATTR_ON_OFF_ON_OFF_ID) {
                bool on = *(bool *)msg->attribute.data.value;
                ESP_LOGI(TAG, "ON/OFF → %s", on ? "ON" : "OFF");
                pwm_rgb_driver_set_power(on);
                pwm_driver_apply();

                // beim OFF die Farbattribute auf letzten bekannten Wert einfrieren
                // damit z2m beim nächsten ON nicht von 0/0 interpoliert
                if (!on) {
                    esp_zb_zcl_set_attribute_val(
                        HA_COLOR_DIMMABLE_LIGHT_ENDPOINT_1,
                        ESP_ZB_ZCL_CLUSTER_ID_COLOR_CONTROL,
                        ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
                        ESP_ZB_ZCL_ATTR_COLOR_CONTROL_CURRENT_X_ID,
                        &s_color_x, false);
                    esp_zb_zcl_set_attribute_val(
                        HA_COLOR_DIMMABLE_LIGHT_ENDPOINT_1,
                        ESP_ZB_ZCL_CLUSTER_ID_COLOR_CONTROL,
                        ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
                        ESP_ZB_ZCL_ATTR_COLOR_CONTROL_CURRENT_Y_ID,
                        &s_color_y, false);
                }
            }
            break;
        }

        // ── LEVEL ─────────────────────────────────────────────────────────
        case ESP_ZB_ZCL_CLUSTER_ID_LEVEL_CONTROL: {
            if (attr == ESP_ZB_ZCL_ATTR_LEVEL_CONTROL_CURRENT_LEVEL_ID) {
                uint8_t level = *(uint8_t *)msg->attribute.data.value;
                ESP_LOGI(TAG, "LEVEL → %d", level);
                pwm_rgb_driver_set_level(level);
                pwm_driver_apply();
    
                esp_zb_zcl_set_attribute_val(
                    HA_COLOR_DIMMABLE_LIGHT_ENDPOINT_1,
                    ESP_ZB_ZCL_CLUSTER_ID_LEVEL_CONTROL,
                    ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
                    ESP_ZB_ZCL_ATTR_LEVEL_CONTROL_CURRENT_LEVEL_ID,
                    &level, false);
                }
            break;
        }

        // ── COLOR CONTROL ─────────────────────────────────────────────────
        case ESP_ZB_ZCL_CLUSTER_ID_COLOR_CONTROL: {
            
            switch (attr) {

                case ESP_ZB_ZCL_ATTR_COLOR_CONTROL_CURRENT_HUE_ID: {
                    uint8_t s_current_hue = *(uint8_t *)msg->attribute.data.value;
                    ESP_LOGI(TAG, "HUE → %d", s_current_hue);
                    break;
                }

                case ESP_ZB_ZCL_ATTR_COLOR_CONTROL_CURRENT_SATURATION_ID: {
                    uint8_t s_current_sat = *(uint8_t *)msg->attribute.data.value;
                    ESP_LOGI(TAG, "SAT → %d", s_current_sat);
                    break;
                }

                case ESP_ZB_ZCL_ATTR_COLOR_CONTROL_ENHANCED_CURRENT_HUE_ID: {
                    ESP_LOGI(TAG, "ENHANCED_HUE" );
                    break;
                }

                case ESP_ZB_ZCL_ATTR_COLOR_CONTROL_CURRENT_X_ID:
                    s_color_x = *(uint16_t *)msg->attribute.data.value;
                    break;

                case ESP_ZB_ZCL_ATTR_COLOR_CONTROL_CURRENT_Y_ID: {
                    uint16_t y = *(uint16_t *)msg->attribute.data.value;
                    
                    // 0/0 ist kein gültiger Farbwert – ignorieren
                    if (s_color_x == 0 && y == 0) {
                        ESP_LOGD(TAG, "XY 0/0 ignoriert");
                        break;
                    }
                    
                    s_color_y = y;
                    pwm_rgb_driver_set_color_xy(s_color_x, s_color_y);
                    pwm_driver_apply();

                    esp_err_t err_x = esp_zb_zcl_set_attribute_val(
                        HA_COLOR_DIMMABLE_LIGHT_ENDPOINT_1,
                        ESP_ZB_ZCL_CLUSTER_ID_COLOR_CONTROL,
                        ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
                        ESP_ZB_ZCL_ATTR_COLOR_CONTROL_CURRENT_X_ID,
                        &s_color_x, false);
                    esp_err_t err_y = esp_zb_zcl_set_attribute_val(
                        HA_COLOR_DIMMABLE_LIGHT_ENDPOINT_1,
                        ESP_ZB_ZCL_CLUSTER_ID_COLOR_CONTROL,
                        ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
                        ESP_ZB_ZCL_ATTR_COLOR_CONTROL_CURRENT_Y_ID,
                        &s_color_y, false);
                    break;
                }

                case ESP_ZB_ZCL_ATTR_COLOR_CONTROL_COLOR_TEMPERATURE_ID: {
                    uint16_t mireds = *(uint16_t *)msg->attribute.data.value;
                    ESP_LOGI(TAG, "COLOR_TEMP → %d mireds (RGB-Leuchte, ignoriert)", mireds);
                    break;
                }

                default:
                    ESP_LOGD(TAG, "COLOR attr=0x%04x (unbekannt)", attr);
                    break;
            }
            break;
        }

        default:
            ESP_LOGD(TAG, "cluster=0x%04x attr=0x%04x (ignoriert)", cluster, attr);
            break;
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

    // ── fehlende Attribute zum Color-Cluster hinzufügen ───────────────────
    esp_zb_cluster_list_t *cluster_list_1 = esp_zb_ep_list_get_ep(
        ep_list, HA_COLOR_DIMMABLE_LIGHT_ENDPOINT_1);

    esp_zb_attribute_list_t *color_attrs_1 = esp_zb_cluster_list_get_cluster(
        cluster_list_1,
        ESP_ZB_ZCL_CLUSTER_ID_COLOR_CONTROL,
        ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

    uint8_t  sat         = 0;
    uint16_t enh_hue     = 0;
    uint16_t capabilities = 0x0008; // Bit3 = XY supported

    esp_zb_color_control_cluster_add_attr(
        color_attrs_1,
        ESP_ZB_ZCL_ATTR_COLOR_CONTROL_CURRENT_SATURATION_ID, 
        &sat);

    esp_zb_color_control_cluster_add_attr(
        color_attrs_1,
        ESP_ZB_ZCL_ATTR_COLOR_CONTROL_ENHANCED_CURRENT_HUE_ID, 
        &enh_hue);

/*
    esp_zb_color_control_cluster_add_attr(
        color_attrs_1,
        ESP_ZB_ZCL_ATTR_COLOR_CONTROL_COLOR_CAPABILITIES_ID, 
        &capabilities);
*/
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
    esp_zb_color_control_cluster_add_attr(
        color_attrs,
        ESP_ZB_ZCL_ATTR_COLOR_CONTROL_COLOR_TEMP_PHYSICAL_MIN_MIREDS_ID, 
        &ct_min);
        
    esp_zb_color_control_cluster_add_attr(
        color_attrs,
        ESP_ZB_ZCL_ATTR_COLOR_CONTROL_COLOR_TEMP_PHYSICAL_MAX_MIREDS_ID, 
        &ct_max);

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

    // Danach nur noch Werte setzen für bereits existierende Attribute:
    uint8_t color_mode = 1; // XY
    esp_zb_zcl_set_attribute_val(
        HA_COLOR_DIMMABLE_LIGHT_ENDPOINT_1,
        ESP_ZB_ZCL_CLUSTER_ID_COLOR_CONTROL,
        ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
        ESP_ZB_ZCL_ATTR_COLOR_CONTROL_COLOR_MODE_ID, 
        &color_mode, 
        false);

    esp_zb_zcl_set_attribute_val(
        HA_COLOR_DIMMABLE_LIGHT_ENDPOINT_1,
        ESP_ZB_ZCL_CLUSTER_ID_COLOR_CONTROL,
        ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
        ESP_ZB_ZCL_ATTR_COLOR_CONTROL_CURRENT_X_ID,
        &s_color_x, 
        false
    );

    esp_zb_zcl_set_attribute_val(
        HA_COLOR_DIMMABLE_LIGHT_ENDPOINT_1,
        ESP_ZB_ZCL_CLUSTER_ID_COLOR_CONTROL,
        ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
        ESP_ZB_ZCL_ATTR_COLOR_CONTROL_CURRENT_Y_ID,
        &s_color_y, 
        false
    );

    uint16_t caps = 0x0008; // XY only
    esp_zb_zcl_set_attribute_val(
        HA_COLOR_DIMMABLE_LIGHT_ENDPOINT_1,
        ESP_ZB_ZCL_CLUSTER_ID_COLOR_CONTROL,
        ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
        ESP_ZB_ZCL_ATTR_COLOR_CONTROL_COLOR_CAPABILITIES_ID,
        &caps,
        false
    );
    
/*
    uint16_t ct_min = 153;  // 6500K
    uint16_t ct_max = 500;  // 2000K

    esp_zb_zcl_set_attribute_val(
        HA_COLOR_TEMP_LIGHT_ENDPOINT,
        ESP_ZB_ZCL_CLUSTER_ID_COLOR_CONTROL,
        ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
        ESP_ZB_ZCL_ATTR_COLOR_CONTROL_COLOR_TEMP_PHYSICAL_MIN_MIREDS_ID,
        &ct_min,
        false);

    esp_zb_zcl_set_attribute_val(
        HA_COLOR_TEMP_LIGHT_ENDPOINT,
        ESP_ZB_ZCL_CLUSTER_ID_COLOR_CONTROL,
        ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
        ESP_ZB_ZCL_ATTR_COLOR_CONTROL_COLOR_TEMP_PHYSICAL_MAX_MIREDS_ID,
        &ct_max,
        false);
*/
    esp_zb_zcl_reporting_info_t report_x = {
        .direction = ESP_ZB_ZCL_CMD_DIRECTION_TO_CLI,
        .ep = HA_COLOR_DIMMABLE_LIGHT_ENDPOINT_1,
        .cluster_id = ESP_ZB_ZCL_CLUSTER_ID_COLOR_CONTROL,
        .cluster_role = ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
        .attr_id = ESP_ZB_ZCL_ATTR_COLOR_CONTROL_CURRENT_X_ID,
        .flags = 0,
        .u.send_info.min_interval = 1,
        .u.send_info.max_interval = 0,
        .u.send_info.delta.u16 = 1,
        .u.send_info.def_min_interval = 1,
        .u.send_info.def_max_interval = 0,
        .dst.profile_id = ESP_ZB_AF_HA_PROFILE_ID,
        .manuf_code = ESP_ZB_ZCL_ATTR_NON_MANUFACTURER_SPECIFIC,
    };
    esp_zb_zcl_update_reporting_info(&report_x);

    esp_zb_zcl_reporting_info_t report_y = report_x;
    report_y.attr_id = ESP_ZB_ZCL_ATTR_COLOR_CONTROL_CURRENT_Y_ID;
    esp_zb_zcl_update_reporting_info(&report_y);
        
    esp_zb_set_primary_network_channel_set(ESP_ZB_PRIMARY_CHANNEL_MASK);
    esp_zb_raw_command_handler_register(zb_raw_command_handler);

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
