#ifndef GW_GAME_DB_H
#define GW_GAME_DB_H

#include <stddef.h>
#include <stdint.h>

typedef enum GWDisplayConfig {
    GW_DISPLAY_SINGLE_320X240 = 1
} GWDisplayConfig;

typedef struct GWGameInfo {
    const char *title;
    const char *expected_filename;
    const char *machine_id;
    char package_signature[9];
    char cpu_name[9];
    const char *input_config;
    GWDisplayConfig display_config;
    uint32_t program_size;
    uint32_t program_crc32;
    int enabled;
} GWGameInfo;

const GWGameInfo *gw_game_db_find(const char signature[8],
                                  const char cpu_name[8]);
size_t gw_game_db_count(void);
const GWGameInfo *gw_game_db_at(size_t index);

#endif
