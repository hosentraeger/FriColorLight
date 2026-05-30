#include "FriConfig.h"
#include <stdint.h>
#include <time.h>
#include <stdio.h>

uint32_t build_firmware_version ( )
{
    return (DEVICE_MAJOR_VERSION << 12) | (DEVICE_MINOR_VERSION << 8) | DEVICE_PATCH_VERSION;
}

void build_date_code ( uint8_t *date_code, size_t max_length )
{
    strncpy((char *)(date_code + 1), DEVICE_DATE, max_length - 1);
    date_code[0] = strlen((char *)(date_code + 1));
}

void build_sw_build ( uint8_t *sw_build, size_t max_length )
{
    snprintf((char *)(sw_build + 1), max_length - 1, "%d.%d.%d", DEVICE_MAJOR_VERSION, DEVICE_MINOR_VERSION, DEVICE_PATCH_VERSION);
    sw_build[0] = strlen((char *)(sw_build + 1));
}

void build_model_id ( uint8_t *model_id, size_t max_length )
{
    strncpy((char *)(model_id + 1), DEVICE_MODEL, max_length - 1);
    model_id[0] = strlen ( (char *)(model_id + 1) );
}

void build_vendor ( uint8_t *vendor, size_t max_length )
{
    strncpy((char *)(vendor + 1), DEVICE_VENDOR, max_length - 1);
    vendor[0] = strlen ( (char *)(vendor + 1) );
}