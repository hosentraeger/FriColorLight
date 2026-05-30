/*
 * SPDX-FileCopyrightText: 2021-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 *
 * Zigbee customized client Example
 *
 * This example code is in the Public Domain (or CC0 licensed, at your option.)
 *
 * Unless required by applicable law or agreed to in writing, this
 * software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
 * CONDITIONS OF ANY KIND, either express or implied.
 */

#include "esp_zigbee_core.h"

#define OTA_ELEMENT_HEADER_LEN              6       /* OTA element format header size include tag identifier and length field */
#define OTA_UPGRADE_MAX_DATA_SIZE           223                                     /* The recommended OTA image block size */

/**
 * @name Enumeration for the tag identifier denotes the type and format of the data within the element
 * @anchor esp_ota_element_tag_id_t
 */
typedef enum esp_ota_element_tag_id_e {
    UPGRADE_IMAGE                               = 0x0000,           /*!< Upgrade image */
} esp_ota_element_tag_id_t;

esp_err_t zb_ota_upgrade_status_handler(esp_zb_zcl_ota_upgrade_value_message_t message);
esp_err_t zb_ota_upgrade_query_image_resp_handler(esp_zb_zcl_ota_upgrade_query_image_resp_message_t message);
esp_err_t zb_register_ota_upgrade_client_device(esp_zb_cluster_list_t *cluster_list);
