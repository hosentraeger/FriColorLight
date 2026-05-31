#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

// #################################################################################################################################
// Zigbee configuration
// #################################################################################################################################
#define MAX_CHILDREN                      10                                    /* the max amount of connected devices */
#define INSTALLCODE_POLICY_ENABLE         false                                 /* enable the install code policy for security */
// Endpoint-Nummern definieren
#define HA_COLOR_DIMMABLE_LIGHT_ENDPOINT_1  1   // Erster RGB Endpunkt (bereits vorhanden)
#define HA_COLOR_DIMMABLE_LIGHT_ENDPOINT_2  2   // Zweiter RGB Endpunkt
#define HA_COLOR_DIMMABLE_LIGHT_ENDPOINT_3  3   // Tunable White Endpunkt
#define ESP_ZB_PRIMARY_CHANNEL_MASK       ESP_ZB_TRANSCEIVER_ALL_CHANNELS_MASK  /* Zigbee primary channel mask use in the example */
#define ESP_ZB_HA_COLOR_TEMPERATURE_LIGHT_DEVICE_ID 0x010c

// #define ED_AGING_TIMEOUT                    ESP_ZB_ED_AGING_TIMEOUT_256MIN          /* End device ages time */
// #define ED_KEEP_ALIVE                       30000                                   /* 30000 millisecond */

// #################################################################################################################################
// Device configuration
// #################################################################################################################################
#define MAX_ZIGBEE_STRING_LENGTH 32
#define DEVICE_MAJOR_VERSION      1
#define DEVICE_MINOR_VERSION      0
#define DEVICE_PATCH_VERSION      5
#define DEVICE_APP_VERSION        5
// Automatisches Build-Datum – kein manuelles Pflegen mehr nötig
#define DEVICE_DATE  build_date_yyyymmdd()

#define DEVICE_VENDOR "redfivedesigns"
#define DEVICE_MANUFACTURER_ID 0xDDBB

#define DEVICE_MODEL "rgbww-colorlight"
#define DEVICE_MODEL_ID 0x0001

#define DEVICE_STACK_VERSION 2   /* The attribute indicates the Zigbee stack version of the device  */
#define DEVICE_HW_VERSION   1   /* The attribute indicates the version of hardware */

// #################################################################################################################################
// OTA configuration
// #################################################################################################################################
#define ESP_OTA_CLIENT_ENDPOINT             5                                       /* OTA endpoint identifier */

uint32_t build_firmware_version ( );
void build_date_code ( uint8_t *date_code, size_t max_length );
void build_sw_build ( uint8_t *sw_build, size_t max_length );
void build_model_id ( uint8_t *model_id, size_t max_length );
void build_vendor ( uint8_t *vendor, size_t max_length );