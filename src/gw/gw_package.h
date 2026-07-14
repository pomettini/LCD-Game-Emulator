#ifndef GW_PACKAGE_H
#define GW_PACKAGE_H

#include "gw_game_db.h"

#include <stddef.h>
#include <stdint.h>

#define GW_PACKAGE_HEADER_SIZE 108u
#define GW_PACKAGE_MAX_FILE_SIZE (2u * 1024u * 1024u)
#define GW_PACKAGE_MAX_PAYLOAD_SIZE 400000u
#define GW_PACKAGE_SEGMENT_COUNT 256u

typedef enum GWPackageResult {
    GW_PACKAGE_OK = 0,
    GW_PACKAGE_ERR_IO,
    GW_PACKAGE_ERR_TOO_LARGE,
    GW_PACKAGE_ERR_MEMORY,
    GW_PACKAGE_ERR_FORMAT,
    GW_PACKAGE_ERR_COMPRESSION,
    GW_PACKAGE_ERR_UNSUPPORTED_GAME,
    GW_PACKAGE_ERR_NOT_ENABLED,
    GW_PACKAGE_ERR_CHECKSUM
} GWPackageResult;

typedef struct GWPackageObject {
    uint32_t offset;
    uint32_t size;
} GWPackageObject;

typedef struct GWPackage {
    uint8_t *payload;
    size_t payload_size;
    uint8_t *jpeg_data;
    size_t jpeg_size;
    char cpu_name[8];
    char signature[8];
    uint8_t time_addresses[7];
    uint32_t flags;
    GWPackageObject background;
    GWPackageObject segments;
    GWPackageObject segment_offsets;
    GWPackageObject segment_x;
    GWPackageObject segment_y;
    GWPackageObject segment_height;
    GWPackageObject segment_width;
    GWPackageObject melody;
    GWPackageObject program;
    GWPackageObject keyboard;
    const GWGameInfo *game;
} GWPackage;

GWPackageResult gw_package_parse(const uint8_t *file_data, size_t file_size,
                                 GWPackage *package);
void gw_package_free(GWPackage *package);
const char *gw_package_result_string(GWPackageResult result);

#endif
