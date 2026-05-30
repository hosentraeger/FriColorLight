#include "esp_ota_ops.h"
#if CONFIG_ZB_DELTA_OTA
#include "esp_delta_ota_ops.h"
#endif
#include "FriOta.h"
#include "esp_check.h"
#include "esp_timer.h"
#include "FriConfig.h"

static const char *TAG = "FRI_OTA_CLIENT";

static const esp_partition_t *s_ota_partition = NULL;
static esp_ota_handle_t s_ota_handle = 0;
static bool s_tagid_received = false;


#define OTA_UPGRADE_QUERY_INTERVAL (1 * 60) // 1 minutes

esp_err_t esp_element_ota_data(uint32_t total_size, const void *payload, uint16_t payload_size, void **outbuf, uint16_t *outlen)
{
    static uint16_t tagid = 0;
    void *data_buf = NULL;
    uint16_t data_len;

    if (!s_tagid_received) {
        uint32_t length = 0;
        if (!payload || payload_size <= OTA_ELEMENT_HEADER_LEN) {
            ESP_RETURN_ON_ERROR(ESP_ERR_INVALID_ARG, TAG, "Invalid element format");
        }

        tagid  = *(const uint16_t *)payload;
        length = *(const uint32_t *)(payload + sizeof(tagid));
        if ((length + OTA_ELEMENT_HEADER_LEN) != total_size) {
            ESP_RETURN_ON_ERROR(ESP_ERR_INVALID_ARG, TAG, "Invalid element length [%ld/%ld]", length, total_size);
        }

        s_tagid_received = true;

        data_buf = (void *)(payload + OTA_ELEMENT_HEADER_LEN);
        data_len = payload_size - OTA_ELEMENT_HEADER_LEN;
    } else {
        data_buf = (void *)payload;
        data_len = payload_size;
    }

    switch (tagid) {
        case UPGRADE_IMAGE:
            *outbuf = data_buf;
            *outlen = data_len;
            break;
        default:
            ESP_RETURN_ON_ERROR(ESP_ERR_INVALID_ARG, TAG, "Unsupported element tag identifier %d", tagid);
            break;
    }

    return ESP_OK;
}

esp_err_t zb_ota_upgrade_status_handler(esp_zb_zcl_ota_upgrade_value_message_t message)
{
    static uint32_t total_size = 0;
    static uint32_t offset = 0;
    static int64_t start_time = 0;
    esp_err_t ret = ESP_OK;

    if (message.info.status == ESP_ZB_ZCL_STATUS_SUCCESS) {
        switch (message.upgrade_status) {
        case ESP_ZB_ZCL_OTA_UPGRADE_STATUS_START:
            ESP_LOGI(TAG, "-- OTA upgrade start");
            start_time = esp_timer_get_time();
            s_ota_partition = esp_ota_get_next_update_partition(NULL);
            assert(s_ota_partition);
#if CONFIG_ZB_DELTA_OTA
            ret = esp_delta_ota_begin(s_ota_partition, 0, &s_ota_handle);
#else
            ret = esp_ota_begin(s_ota_partition, 0, &s_ota_handle);
#endif
            ESP_RETURN_ON_ERROR(ret, TAG, "Failed to begin OTA partition, status: %s", esp_err_to_name(ret));
            break;
        case ESP_ZB_ZCL_OTA_UPGRADE_STATUS_RECEIVE:
            total_size = message.ota_header.image_size;
            offset += message.payload_size;
            ESP_LOGI(TAG, "-- OTA Client receives data: progress [%ld/%ld]", offset, total_size);
            if (message.payload_size && message.payload) {
                uint16_t payload_size = 0;
                void    *payload = NULL;
                ret = esp_element_ota_data(total_size, message.payload, message.payload_size, &payload, &payload_size);
                ESP_RETURN_ON_ERROR(ret, TAG, "Failed to element OTA data, status: %s", esp_err_to_name(ret));
#if CONFIG_ZB_DELTA_OTA
                ret = esp_delta_ota_write(s_ota_handle, payload, payload_size);
#else
                ret = esp_ota_write(s_ota_handle, (const void *)payload, payload_size);
#endif
                ESP_RETURN_ON_ERROR(ret, TAG, "Failed to write OTA data to partition, status: %s", esp_err_to_name(ret));
            }
            break;
        case ESP_ZB_ZCL_OTA_UPGRADE_STATUS_APPLY:
            ESP_LOGI(TAG, "-- OTA upgrade apply");
            break;
        case ESP_ZB_ZCL_OTA_UPGRADE_STATUS_CHECK:
            ret = offset == total_size ? ESP_OK : ESP_FAIL;
            offset = 0;
            total_size = 0;
            s_tagid_received = false;
            ESP_LOGI(TAG, "-- OTA upgrade check status: %s", esp_err_to_name(ret));
            break;
        case ESP_ZB_ZCL_OTA_UPGRADE_STATUS_FINISH:
            ESP_LOGI(TAG, "-- OTA Finish");
            ESP_LOGI(TAG, "-- OTA Information: version: 0x%lx, manufacturer code: 0x%x, image type: 0x%x, total size: %ld bytes, cost time: %lld ms,",
                     message.ota_header.file_version, message.ota_header.manufacturer_code, message.ota_header.image_type,
                     message.ota_header.image_size, (esp_timer_get_time() - start_time) / 1000);
#if CONFIG_ZB_DELTA_OTA
            ret = esp_delta_ota_end(s_ota_handle);
#else
            ret = esp_ota_end(s_ota_handle);
#endif
            ESP_RETURN_ON_ERROR(ret, TAG, "Failed to end OTA partition, status: %s", esp_err_to_name(ret));
            ret = esp_ota_set_boot_partition(s_ota_partition);
            ESP_RETURN_ON_ERROR(ret, TAG, "Failed to set OTA boot partition, status: %s", esp_err_to_name(ret));
            ESP_LOGW(TAG, "Prepare to restart system");
            esp_restart();
            break;
        default:
            ESP_LOGI(TAG, "OTA status: %d", message.upgrade_status);
            break;
        }
    }
    return ret;
}

esp_err_t zb_ota_upgrade_query_image_resp_handler(esp_zb_zcl_ota_upgrade_query_image_resp_message_t message)
{
    esp_err_t ret = ESP_OK;
    if (message.info.status == ESP_ZB_ZCL_STATUS_SUCCESS) {
        ESP_LOGI(TAG, "Queried OTA image from address: 0x%04hx, endpoint: %d", message.server_addr.u.short_addr, message.server_endpoint);
        ESP_LOGI(TAG, "Image version: 0x%lx, manufacturer code: 0x%x, image size: %ld", message.file_version, message.manufacturer_code,
                 message.image_size);
    }
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Approving OTA image upgrade");
    } else {
        ESP_LOGI(TAG, "Rejecting OTA image upgrade, status: %s", esp_err_to_name(ret));
    }
    return ret;
}

esp_err_t zb_register_ota_upgrade_client_device(esp_zb_cluster_list_t *cluster_list)
{
    /** Create ota client cluster with attributes.
     *  Manufacturer code, image type and file version should match with configured values for server.
     *  If the client values do not match with configured values then it shall discard the command and
     *  no further processing shall continue.
     */
    esp_zb_ota_cluster_cfg_t ota_cluster_cfg = {
        .ota_upgrade_file_version = 4098,
        .ota_upgrade_downloaded_file_ver = 4098,
        .ota_upgrade_manufacturer = DEVICE_MANUFACTURER_ID,
        .ota_upgrade_image_type = DEVICE_MODEL_ID,
    };
    esp_zb_attribute_list_t *ota_cluster = esp_zb_ota_cluster_create(&ota_cluster_cfg);
    esp_zb_zcl_ota_upgrade_client_variable_t variable_config = {
        .timer_query = ESP_ZB_ZCL_OTA_UPGRADE_QUERY_TIMER_COUNT_DEF,
        .hw_version = DEVICE_HW_VERSION,
        .max_data_size = OTA_UPGRADE_MAX_DATA_SIZE,
    };
    uint16_t ota_upgrade_server_addr = 0xffff;
    uint8_t ota_upgrade_server_ep = 0xff;

    /* Added attributes */
    ESP_ERROR_CHECK(esp_zb_ota_cluster_add_attr(ota_cluster, ESP_ZB_ZCL_ATTR_OTA_UPGRADE_CLIENT_DATA_ID, (void *)&variable_config));
    ESP_ERROR_CHECK(esp_zb_ota_cluster_add_attr(ota_cluster, ESP_ZB_ZCL_ATTR_OTA_UPGRADE_SERVER_ADDR_ID, (void *)&ota_upgrade_server_addr));
    ESP_ERROR_CHECK(esp_zb_ota_cluster_add_attr(ota_cluster, ESP_ZB_ZCL_ATTR_OTA_UPGRADE_SERVER_ENDPOINT_ID, (void *)&ota_upgrade_server_ep));
    /* Added clusters */
    ESP_ERROR_CHECK(esp_zb_cluster_list_add_ota_cluster(cluster_list, ota_cluster, ESP_ZB_ZCL_CLUSTER_CLIENT_ROLE));
    return ESP_OK;
}

