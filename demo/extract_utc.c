#define USE_GPMF_READER_INTERFACE
#include "../GPMF_parser.h"
#include "../GPMF_utils.h"
#include "GPMF_mp4reader.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Usage: %s <video.MP4>\n", argv[0]);
        return 1;
    }

    const char *filename = argv[1];
    size_t mp4handle = OpenMP4Source((char *)filename, MOV_GPMF_TRAK_TYPE, MOV_GPMF_TRAK_SUBTYPE, 0);
    if (!mp4handle) {
        printf("Failed to open file or no GPMF track found.\n");
        return 1;
    }

    uint32_t payloads = GetNumberPayloads(mp4handle);
    if (payloads == 0) {
        printf("No GPMF data found in %s\n", filename);
        CloseSource(mp4handle);
        return 1;
    }

    size_t resHandle = 0;

    for (uint32_t i = 0; i < payloads; i++) {
        uint32_t payload_size = GetPayloadSize(mp4handle, i);
        resHandle = GetPayloadResource(mp4handle, resHandle, payload_size);
        const uint32_t *payload = GetPayload(mp4handle, resHandle, i);

        if (!payload || payload_size == 0)
            continue;

        GPMF_stream gs;
        if (GPMF_OK != GPMF_Init(&gs, (uint32_t *)payload, payload_size))
            continue;

        if (GPMF_OK == GPMF_FindNext(&gs, STR2FOURCC("STMP"), GPMF_RECURSE_LEVELS)) {
            const uint8_t *data = (const uint8_t *)GPMF_RawData(&gs);
            int size = GPMF_RawDataSize(&gs);

            if (data && size >= 8) {
                for (int offset = 0; offset + 8 <= size; offset += 8) {
                    uint64_t micro_ts = ((uint64_t)data[offset + 0] << 56) |
                                        ((uint64_t)data[offset + 1] << 48) |
                                        ((uint64_t)data[offset + 2] << 40) |
                                        ((uint64_t)data[offset + 3] << 32) |
                                        ((uint64_t)data[offset + 4] << 24) |
                                        ((uint64_t)data[offset + 5] << 16) |
                                        ((uint64_t)data[offset + 6] << 8) |
                                        ((uint64_t)data[offset + 7]);

                    printf("STMP microsecond timestamp: %.6f s\n", micro_ts / 1e6);
                }
            }
        }

        if (GPMF_OK == GPMF_FindNext(&gs, STR2FOURCC("GPSU"), GPMF_RECURSE_LEVELS)) {
            const char *data = (const char *)GPMF_RawData(&gs);
            int size = GPMF_RawDataSize(&gs);
            
            if (data && size >= 15) {
                char gpsu_raw[32] = {0};
                memcpy(gpsu_raw, data, size < 31 ? size : 31);

                int day, month, year, hour, minute, sec;
                float fractional_sec = 0.0f;
                sscanf(gpsu_raw, "%2d%2d%2d%2d%2d%2d%f", &day, &month, &year, &hour, &minute, &sec, &fractional_sec);
                int millis = (int)((fractional_sec - (int)fractional_sec) * 1000.0f);

                struct tm t = {0};
                t.tm_mday = day;
                t.tm_mon = month - 1;
                t.tm_year = 2000 + year - 1900;
                t.tm_hour = hour;
                t.tm_min = minute;
                t.tm_sec = sec;

                char formatted[64];
                strftime(formatted, sizeof(formatted), "%Y-%m-%d %H:%M:%S", &t);
                printf("GPSU UTC Timedata: %s.%03d\n", formatted, millis);
            }
        }

        if (GPMF_OK == GPMF_FindNext(&gs, STR2FOURCC("SROT"), GPMF_RECURSE_LEVELS)) {
            const uint8_t *data = (const uint8_t *)GPMF_RawData(&gs);
            if (data) {
                uint32_t temp = ((uint32_t)data[0] << 24) |
                                ((uint32_t)data[1] << 16) |
                                ((uint32_t)data[2] << 8)  |
                                ((uint32_t)data[3]);
                float srot_ms;
                memcpy(&srot_ms, &temp, sizeof(float));
                printf("Sensor Read Out Time (SROT): %.3f ms\n", srot_ms);
            }
        }

        GPMF_Free(&gs);
    }

    if (resHandle)
        FreePayloadResource(mp4handle, resHandle);

    CloseSource(mp4handle);
    return 0;
}