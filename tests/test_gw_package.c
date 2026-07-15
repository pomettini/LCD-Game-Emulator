#include "gw_game_db.h"
#include "gw_package.h"

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static void test_database(void)
{
    const GWGameInfo *ball;
    const GWGameInfo *flagman;
    const char ball_signature[8] = {'g','n','w','_','b','a','l','l'};
    const char flagman_signature[8] = {'_','f','l','a','g','m','a','n'};
    const char ball_cpu[8] = {'S','M','5','A','_','_','_','\0'};

    assert(gw_game_db_count() == 25);
    ball = gw_game_db_find(ball_signature, ball_cpu);
    assert(ball != NULL);
    assert(strcmp(ball->machine_id, "gnw_ball") == 0);
    assert(ball->enabled == 1);
    assert(ball->input_profile == GW_INPUT_PROFILE_BALL);
    assert(ball->program_crc32 == UINT32_C(0xac94e6e4));

    flagman = gw_game_db_find(flagman_signature, ball_cpu);
    assert(flagman != NULL);
    assert(strcmp(flagman->machine_id, "gnw_flagman") == 0);
    assert(flagman->enabled == 1);
    assert(flagman->input_profile == GW_INPUT_PROFILE_FLAGMAN);
    assert(flagman->program_crc32 == UINT32_C(0xcc7a99e4));
}

static void test_rejections(void)
{
    GWPackage package;
    uint8_t tiny[4] = {0, 1, 2, 3};
    uint8_t unknown[GW_PACKAGE_HEADER_SIZE];
    uint8_t bad_lz4[7] = {0x04, 0x22, 0x4d, 0x18, 0x40, 0x70, 0};

    memset(unknown, 0, sizeof(unknown));
    memcpy(unknown, "SM510__", 7);

    assert(gw_package_parse(tiny, sizeof(tiny), &package) ==
           GW_PACKAGE_ERR_FORMAT);
    assert(gw_package_parse(unknown, sizeof(unknown), &package) !=
           GW_PACKAGE_OK);
    assert(gw_package_parse(bad_lz4, sizeof(bad_lz4), &package) ==
           GW_PACKAGE_ERR_COMPRESSION);
}

static void test_randomized_headers(void)
{
    GWPackage package;
    uint8_t data[512];
    uint32_t state = UINT32_C(0x6d2b79f5);
    unsigned iteration;

    for (iteration = 0; iteration < 5000; ++iteration) {
        size_t size = GW_PACKAGE_HEADER_SIZE + (state %
                      (sizeof(data) - GW_PACKAGE_HEADER_SIZE + 1));
        size_t i;
        for (i = 0; i < size; ++i) {
            state = state * UINT32_C(1664525) + UINT32_C(1013904223);
            data[i] = (uint8_t)(state >> 24);
        }
        if (iteration & 1)
            memcpy(data, "SM5", 3);
        else {
            data[0] = 0x04;
            data[1] = 0x22;
            data[2] = 0x4d;
            data[3] = 0x18;
        }
        if (gw_package_parse(data, size, &package) == GW_PACKAGE_OK)
            gw_package_free(&package);
    }
}

static int validate_file(const char *path)
{
    FILE *file = fopen(path, "rb");
    GWPackage package;
    GWPackageResult result;
    long length;
    uint8_t *data;

    if (file == NULL)
        return 2;
    if (fseek(file, 0, SEEK_END) != 0 || (length = ftell(file)) < 0 ||
        fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return 2;
    }
    data = malloc((size_t)length);
    if (data == NULL || fread(data, 1, (size_t)length, file) != (size_t)length) {
        free(data);
        fclose(file);
        return 2;
    }
    fclose(file);

    result = gw_package_parse(data, (size_t)length, &package);
    free(data);
    if (result != GW_PACKAGE_OK) {
        fprintf(stderr, "%s: %s\n", path, gw_package_result_string(result));
        return 1;
    }
    printf("%s: valid %s package, %zu-byte payload\n", path,
           package.game->machine_id, package.payload_size);
    gw_package_free(&package);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc == 2)
        return validate_file(argv[1]);
    test_database();
    test_rejections();
    test_randomized_headers();
    puts("gw_package tests passed");
    return 0;
}
