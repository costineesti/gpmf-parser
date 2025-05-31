#define USE_GPMF_READER_INTERFACE
#include "../GPMF_parser.h"
#include "../GPMF_utils.h"
#include "GPMF_mp4reader.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
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

        double in = 0.0, out = 0.0;
        if (GetPayloadTime(mp4handle, i, &in, &out) == GPMF_OK) {
            printf("Payload %u | Timeline Start: %.6f s | End: %.6f s\n", i, in, out);
        }

        // Parse STMP - Reset stream first
        GPMF_ResetState(&gs);
        if (GPMF_OK == GPMF_FindNext(&gs, STR2FOURCC("STMP"), GPMF_RECURSE_LEVELS)) {
            const uint8_t *data = (const uint8_t *)GPMF_RawData(&gs);
            int size = GPMF_RawDataSize(&gs);
            uint32_t samples = GPMF_Repeat(&gs);

            printf("STMP found - Size: %d bytes, Samples: %u\n", size, samples);

            if (data && size >= 8) {
                int timestamp_count = size / 8;
                for (int j = 0; j < timestamp_count && j < samples; j++) {
                    int offset = j * 8;
                    
                    // big-endian first (most common)
                    uint64_t micro_ts_be = ((uint64_t)data[offset + 0] << 56) |
                                          ((uint64_t)data[offset + 1] << 48) |
                                          ((uint64_t)data[offset + 2] << 40) |
                                          ((uint64_t)data[offset + 3] << 32) |
                                          ((uint64_t)data[offset + 4] << 24) |
                                          ((uint64_t)data[offset + 5] << 16) |
                                          ((uint64_t)data[offset + 6] << 8) |
                                          ((uint64_t)data[offset + 7]);

                    // Also try little-endian
                    uint64_t micro_ts_le = ((uint64_t)data[offset + 7] << 56) |
                                          ((uint64_t)data[offset + 6] << 48) |
                                          ((uint64_t)data[offset + 5] << 40) |
                                          ((uint64_t)data[offset + 4] << 32) |
                                          ((uint64_t)data[offset + 3] << 24) |
                                          ((uint64_t)data[offset + 2] << 16) |
                                          ((uint64_t)data[offset + 1] << 8) |
                                          ((uint64_t)data[offset + 0]);

                    printf("STMP[%d] BE: %.6f s (%llu μs) | LE: %.6f s (%llu μs)\n", 
                           j, micro_ts_be / 1e6, (unsigned long long)micro_ts_be, 
                           micro_ts_le / 1e6, (unsigned long long)micro_ts_le);
                }
            }
        }

        // Parse GPSU - Reset stream first
        GPMF_ResetState(&gs);
        if (GPMF_OK == GPMF_FindNext(&gs, STR2FOURCC("GPSU"), GPMF_RECURSE_LEVELS)) {
            const char *data = (const char *)GPMF_RawData(&gs);
            int size = GPMF_RawDataSize(&gs);
            
            printf("GPSU found - Size: %d bytes\n", size);
            
            if (data && size > 0) {

                if (size >= 15) {
                    char gpsu_copy[64] = {0};
                    int copy_len = size < 63 ? size : 63;
                    memcpy(gpsu_copy, data, copy_len);
                    
                    // Method 1: Standard format YYMMDDHHMMSS.sss
                    if (size >= 15) {
                        char year_str[3] = {data[0], data[1], 0};
                        char month_str[3] = {data[2], data[3], 0};
                        char day_str[3] = {data[4], data[5], 0};
                        char hour_str[3] = {data[6], data[7], 0};
                        char min_str[3] = {data[8], data[9], 0};
                        char sec_str[3] = {data[10], data[11], 0};
                        
                        int year = atoi(year_str);
                        int month = atoi(month_str);
                        int day = atoi(day_str);
                        int hour = atoi(hour_str);
                        int minute = atoi(min_str);
                        int sec = atoi(sec_str);
                        
                        // Handle fractional seconds if present
                        int millis = 0;
                        if (size > 12 && data[12] == '.') {
                            char frac_str[5] = {0};
                            int frac_len = (size - 13) < 4 ? (size - 13) : 3;
                            memcpy(frac_str, &data[13], frac_len);
                            // Pad with zeros if needed
                            while (strlen(frac_str) < 3) {
                                strcat(frac_str, "0");
                            }
                            millis = atoi(frac_str);
                        }
                        
                        if (year >= 0 && year <= 99 && month >= 1 && month <= 12 && 
                            day >= 1 && day <= 31 && hour >= 0 && hour <= 23 &&
                            minute >= 0 && minute <= 59 && sec >= 0 && sec <= 59) {
                            
                            struct tm t = {0};
                            t.tm_mday = day;
                            t.tm_mon = month - 1;
                            t.tm_year = (year < 50 ? 2000 + year : 1900 + year) - 1900;
                            t.tm_hour = hour;
                            t.tm_min = minute;
                            t.tm_sec = sec;

                            // Format and display UTC time
                            char formatted_utc[64];
                            strftime(formatted_utc, sizeof(formatted_utc), "%Y-%m-%d %H:%M:%S", &t);
                            printf("GPSU UTC Time: %s.%03d\n", formatted_utc, millis);
                            
                            // For local time conversion, we need to use timegm if available
                            // or manually calculate UTC time_t
                            #ifdef __linux__
                            time_t utc_timestamp = timegm(&t);  // Treats tm as UTC
                            #else
                            // Fallback for systems without timegm
                            time_t utc_timestamp = mktime(&t) - timezone;
                            #endif
                            
                            struct tm *local_tm = localtime(&utc_timestamp);
                            if (local_tm) {
                                char formatted_local[64];
                                strftime(formatted_local, sizeof(formatted_local), "%Y-%m-%d %H:%M:%S", local_tm);
                                printf("GPSU Local Time: %s.%03d\n", formatted_local, millis);
                            }
                        } else {
                            printf("GPSU: Invalid date/time values parsed\n");
                        }
                    }
                }
            }
        }

        printf("---\n");
        GPMF_Free(&gs);
    }

    if (resHandle)
        FreePayloadResource(mp4handle, resHandle);

    CloseSource(mp4handle);
    return 0;
}