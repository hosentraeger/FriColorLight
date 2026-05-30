#include "FriConfig.h"
#include <stdint.h>
#include <stdio.h>

static inline const char *build_date_yyyymmdd(void)
{
    static char buf[9]; // "20260530\0"

    // __DATE__ = "May 30 2026"
    //             0123456789A
    const char *d = __DATE__;

    // Jahr
    buf[0] = d[7]; buf[1] = d[8]; buf[2] = d[9]; buf[3] = d[10];

    // Monat
    const char months[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
    int m = 0;
    for (; m < 12; m++) {
        if (d[0] == months[m*3] &&
            d[1] == months[m*3+1] &&
            d[2] == months[m*3+2]) break;
    }
    buf[4] = '0' + (m + 1) / 10;
    buf[5] = '0' + (m + 1) % 10;

    // Tag (Leerzeichen bei z.B. " 3" → "03")
    buf[6] = (d[4] == ' ') ? '0' : d[4];
    buf[7] = d[5];
    buf[8] = '\0';

    return buf;
}

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