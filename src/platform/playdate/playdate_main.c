/*
 * Playdate device frontend for LCD Game Emulator.
 * Copyright (C) 2026 LCD Game Emulator contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "pd_api.h"
#include "gw_machine.h"
#include "gw_package.h"
#include "gw_audio_playdate.h"
#include "gw_renderer_playdate.h"
#include "rom_picker.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#if defined(TARGET_SIMULATOR)
#error "LCD Game Emulator is a Playdate device-only application"
#endif

static PlaydateAPI *pd;

typedef enum AppScreen {
    APP_SCREEN_PICKER,
    APP_SCREEN_ERROR,
    APP_SCREEN_PACKAGE,
    APP_SCREEN_RESET_CONFIRM,
    APP_SCREEN_CONTROLS
} AppScreen;

static AppScreen screen;
static const char *error_title;
static const char *error_line_1;
static const char *error_line_2;
static GWPackage loaded_package;
static GWMachine machine;
static GWPlaydateRenderer renderer;
static uint64_t last_update_us;
static uint64_t cycle_fraction;
static int64_t cycle_budget;
static int return_to_picker;
static int scheduler_resync_pending;

#if defined(GW_DEVELOPMENT_METRICS)
static uint32_t metrics_window_ms;
static uint32_t metrics_expected_cycles;
static uint32_t metrics_emulated_cycles;
static uint32_t metrics_longest_update_us;
#endif

static void synchronize_clock(void)
{
    struct PDDateTime date_time;
    uint32_t epoch;

    if (screen != APP_SCREEN_PACKAGE && screen != APP_SCREEN_CONTROLS &&
        screen != APP_SCREEN_RESET_CONFIRM)
        return;
    epoch = pd->system->getSecondsSinceEpoch(NULL);
    pd->system->convertEpochToDateTime(epoch, &date_time);
    if (!gw_machine_set_time(&machine, date_time.hour, date_time.minute,
                             date_time.second))
        pd->system->logToConsole("LCD Game Emulator: package has no usable clock mapping");
}

static void synchronize_clock_menu(void *userdata)
{
    (void)userdata;
    synchronize_clock();
}

static void reset_game_menu(void *userdata)
{
    (void)userdata;
    if (screen == APP_SCREEN_PACKAGE) {
        gw_pd_audio_stop();
        screen = APP_SCREEN_RESET_CONFIRM;
    }
}

static void show_controls_menu(void *userdata)
{
    (void)userdata;
    if (screen == APP_SCREEN_PACKAGE) {
        gw_pd_audio_stop();
        screen = APP_SCREEN_CONTROLS;
    }
}

static void return_to_picker_menu(void *userdata)
{
    (void)userdata;
    if (screen == APP_SCREEN_PACKAGE)
        return_to_picker = 1;
}

static uint32_t read_inputs(void *userdata)
{
    PDButtons current;
    uint32_t inputs = 0;
    (void)userdata;

    pd->system->getButtonState(&current, NULL, NULL);
    if (current & kButtonLeft) inputs |= GW_INPUT_LEFT;
    if (current & kButtonRight) inputs |= GW_INPUT_RIGHT;
    if (current & kButtonUp) inputs |= GW_INPUT_UP;
    if (current & kButtonDown) inputs |= GW_INPUT_DOWN;
    if (current & kButtonA) inputs |= GW_INPUT_A | GW_INPUT_GAME;
    if (current & kButtonB) inputs |= GW_INPUT_B | GW_INPUT_TIME;
    return inputs;
}

static uint64_t get_time_us(void *userdata)
{
    (void)userdata;
    return (uint64_t)pd->system->getCurrentTimeMilliseconds() * 1000u;
}

static void resync_scheduler(void)
{
    last_update_us = get_time_us(NULL);
    cycle_fraction = 0;
    cycle_budget = 0;
}

static void run_machine_elapsed(void)
{
    uint64_t now = get_time_us(NULL);
    uint64_t elapsed = now - last_update_us;
    uint64_t scaled;
    uint32_t due;
#if defined(GW_DEVELOPMENT_METRICS)
    uint64_t update_start = get_time_us(NULL);
#endif

    last_update_us = now;
    if (elapsed > 250000u) {
        /* Never execute an unbounded backlog after lock/menu/USB pauses. */
        cycle_fraction = 0;
        cycle_budget = 0;
        return;
    }

    scaled = elapsed * UINT64_C(32768) + cycle_fraction;
    due = (uint32_t)(scaled / UINT64_C(1000000));
    cycle_fraction = scaled % UINT64_C(1000000);
    cycle_budget += due;
#if defined(GW_DEVELOPMENT_METRICS)
    metrics_expected_cycles += due;
#endif
    if (cycle_budget > 0) {
        uint32_t requested = cycle_budget > 4096 ? 4096u :
                             (uint32_t)cycle_budget;
        uint32_t executed = gw_machine_run_cycles(&machine, requested);
        cycle_budget -= executed;
#if defined(GW_DEVELOPMENT_METRICS)
        metrics_emulated_cycles += executed;
#endif
    }
#if defined(GW_DEVELOPMENT_METRICS)
    {
        uint32_t duration = (uint32_t)(get_time_us(NULL) - update_start);
        uint32_t now_ms = pd->system->getCurrentTimeMilliseconds();
        if (duration > metrics_longest_update_us)
            metrics_longest_update_us = duration;
        if (now_ms - metrics_window_ms >= 5000u) {
            pd->system->logToConsole(
                "[timing] emu=%u expected=%u deficit=%d longest_us=%u audio_underruns=%u",
                metrics_emulated_cycles, metrics_expected_cycles,
                (int)(metrics_expected_cycles - metrics_emulated_cycles),
                metrics_longest_update_us, gw_pd_audio_underruns());
            metrics_window_ms = now_ms;
            metrics_expected_cycles = 0;
            metrics_emulated_cycles = 0;
            metrics_longest_update_us = 0;
        }
    }
#endif
}

static GWPackageResult read_package(const char *path, GWPackage *package)
{
    SDFile *file;
    uint8_t *data;
    int file_size;
    size_t total = 0;
    GWPackageResult result;

    file = pd->file->open(path, kFileRead);
    if (file == NULL)
        return GW_PACKAGE_ERR_IO;
    if (pd->file->seek(file, 0, SEEK_END) != 0) {
        pd->file->close(file);
        return GW_PACKAGE_ERR_IO;
    }
    file_size = pd->file->tell(file);
    if (file_size < 0 || (unsigned)file_size > GW_PACKAGE_MAX_FILE_SIZE) {
        pd->file->close(file);
        return GW_PACKAGE_ERR_TOO_LARGE;
    }
    if (pd->file->seek(file, 0, SEEK_SET) != 0) {
        pd->file->close(file);
        return GW_PACKAGE_ERR_IO;
    }

    data = malloc((size_t)file_size);
    if (data == NULL) {
        pd->file->close(file);
        return GW_PACKAGE_ERR_MEMORY;
    }
    while (total < (size_t)file_size) {
        int count = pd->file->read(file, data + total,
                                   (unsigned)((size_t)file_size - total));
        if (count <= 0)
            break;
        total += (size_t)count;
    }
    pd->file->close(file);
    if (total != (size_t)file_size) {
        free(data);
        return GW_PACKAGE_ERR_IO;
    }

    result = gw_package_parse(data, total, package);
    free(data);
    return result;
}

static int has_extension(const char *path, const char *extension)
{
    size_t path_length = strlen(path);
    size_t extension_length = strlen(extension);
    const char *suffix;
    size_t i;

    if (path_length < extension_length)
        return 0;

    suffix = path + path_length - extension_length;
    for (i = 0; i < extension_length; ++i) {
        char a = suffix[i];
        char b = extension[i];
        if (a >= 'A' && a <= 'Z')
            a = (char)(a + ('a' - 'A'));
        if (a != b)
            return 0;
    }
    return 1;
}

static void show_error(const char *title, const char *line_1, const char *line_2)
{
    error_title = title;
    error_line_1 = line_1;
    error_line_2 = line_2;
    screen = APP_SCREEN_ERROR;
}

static void on_package_picked(const char *path, void *userdata)
{
    GWPackageResult result;

    (void)userdata;

    if (has_extension(path, ".mgw")) {
        show_error("Unsupported .mgw file",
                   "Madrigal/Libretro simulations are not supported.",
                   "Use a compatible LCD-Game-Shrinker .gw package.");
        return;
    }

    gw_package_free(&loaded_package);
    result = read_package(path, &loaded_package);
    if (result != GW_PACKAGE_OK) {
        show_error("Package rejected", gw_package_result_string(result),
                   "Press B to return to the picker.");
        return;
    }

    pd->system->logToConsole("Validated %s package: %u-byte payload",
                             loaded_package.game->machine_id,
                             (unsigned)loaded_package.payload_size);
    {
        GWPlatform platform;
        memset(&platform, 0, sizeof(platform));
        platform.read_inputs = read_inputs;
        platform.set_buzzer = gw_pd_audio_set_buzzer;
        platform.advance_clock = gw_pd_audio_advance_clock;
        platform.get_time_us = get_time_us;
        platform.set_lcd_segment = gw_pd_renderer_set_segment;
        platform.userdata = &renderer;
        if (!gw_pd_renderer_init(&renderer, pd, &loaded_package)) {
            gw_package_free(&loaded_package);
            show_error("Artwork not supported",
                       "This milestone requires an RGB565 background.",
                       "Regenerate Ball without JPEG artwork.");
            return;
        }
        if (!gw_machine_init(&machine, &loaded_package, &platform)) {
            gw_package_free(&loaded_package);
            show_error("Core initialization failed",
                       "The validated package could not start safely.",
                       "Press B to return to the picker.");
            return;
        }
    }
    resync_scheduler();
    gw_pd_audio_start();
    screen = APP_SCREEN_PACKAGE;
    synchronize_clock();
}

static void init_picker(void)
{
    static const char *extensions[] = {"gw", "mgw", NULL};
    RomPickerConfig config;

    memset(&config, 0, sizeof(config));
    config.folder = "/Shared/Emulation/game-and-watch/games/";
    config.extensions = extensions;
    config.on_select = on_package_picked;
    rom_picker_init(pd, &config);
    screen = APP_SCREEN_PICKER;
}

static void draw_error(void)
{
    PDButtons pushed;

    pd->system->getButtonState(NULL, &pushed, NULL);
    if (pushed & kButtonB) {
        screen = APP_SCREEN_PICKER;
        return;
    }

    pd->graphics->clear(kColorWhite);
    pd->graphics->drawText(error_title, strlen(error_title), kASCIIEncoding,
                           16, 62);
    pd->graphics->drawText(error_line_1, strlen(error_line_1), kASCIIEncoding,
                           16, 102);
    pd->graphics->drawText(error_line_2, strlen(error_line_2), kASCIIEncoding,
                           16, 124);
    pd->graphics->drawText("B: Back", sizeof("B: Back") - 1, kASCIIEncoding,
                           16, 174);
}

static void draw_package(void)
{
    if (return_to_picker) {
        return_to_picker = 0;
        gw_pd_audio_stop();
        gw_machine_shutdown(&machine);
        gw_package_free(&loaded_package);
        screen = APP_SCREEN_PICKER;
        return;
    }

    run_machine_elapsed();
    gw_machine_emit_lcd(&machine);
    gw_pd_renderer_draw(&renderer);
}

static void draw_reset_confirmation(void)
{
    PDButtons pushed;
    pd->system->getButtonState(NULL, &pushed, NULL);
    if (pushed & kButtonA) {
        gw_machine_reset(&machine);
        synchronize_clock();
        resync_scheduler();
        gw_pd_audio_start();
        screen = APP_SCREEN_PACKAGE;
        return;
    }
    if (pushed & kButtonB) {
        gw_pd_audio_start();
        screen = APP_SCREEN_PACKAGE;
        resync_scheduler();
        return;
    }
    pd->graphics->clear(kColorWhite);
    pd->graphics->drawText("Reset Ball and lose current progress?",
                           sizeof("Reset Ball and lose current progress?") - 1,
                           kASCIIEncoding, 30, 92);
    pd->graphics->drawText("A: Reset    B: Cancel",
                           sizeof("A: Reset    B: Cancel") - 1,
                           kASCIIEncoding, 92, 132);
}

static void draw_controls(void)
{
    PDButtons pushed;
    pd->system->getButtonState(NULL, &pushed, NULL);
    if (pushed & kButtonB) {
        gw_pd_audio_start();
        screen = APP_SCREEN_PACKAGE;
        resync_scheduler();
        return;
    }
    pd->graphics->clear(kColorWhite);
    pd->graphics->drawText("Ball controls", sizeof("Ball controls") - 1,
                           kASCIIEncoding, 16, 45);
    pd->graphics->drawText("Left / Right: move", sizeof("Left / Right: move") - 1,
                           kASCIIEncoding, 16, 82);
    pd->graphics->drawText("A: right / Game A", sizeof("A: right / Game A") - 1,
                           kASCIIEncoding, 16, 108);
    pd->graphics->drawText("B: left / Game B", sizeof("B: left / Game B") - 1,
                           kASCIIEncoding, 16, 134);
    pd->graphics->drawText("B: close this screen", sizeof("B: close this screen") - 1,
                           kASCIIEncoding, 16, 178);
}

static int update(void *userdata)
{
    (void)userdata;

    if (screen == APP_SCREEN_PICKER)
        rom_picker_update();
    else if (screen == APP_SCREEN_ERROR)
        draw_error();
    else if (screen == APP_SCREEN_PACKAGE)
        draw_package();
    else if (screen == APP_SCREEN_RESET_CONFIRM)
        draw_reset_confirmation();
    else
        draw_controls();
    return 1;
}

#ifdef _WINDLL
__declspec(dllexport)
#endif
int eventHandler(PlaydateAPI *playdate, PDSystemEvent event, uint32_t arg)
{
    (void)arg;

    if (event == kEventInit) {
        pd = playdate;
        pd->display->setRefreshRate(30.0f);
        if (!gw_pd_audio_init(pd))
            pd->system->logToConsole("LCD Game Emulator: audio source init failed");
        init_picker();
        pd->system->addMenuItem("Return to game picker",
                                return_to_picker_menu, NULL);
        pd->system->addMenuItem("Synchronize clock",
                                synchronize_clock_menu, NULL);
        pd->system->addMenuItem("Reset game", reset_game_menu, NULL);
        pd->system->addMenuItem("Show controls", show_controls_menu, NULL);
        pd->system->setUpdateCallback(update, NULL);
        pd->system->logToConsole("LCD Game Emulator: ROM picker initialized");
    } else if (event == kEventPause || event == kEventLock) {
        if (screen == APP_SCREEN_PACKAGE)
            gw_pd_audio_stop();
        scheduler_resync_pending = 1;
    } else if (event == kEventResume || event == kEventUnlock) {
        if (screen == APP_SCREEN_PACKAGE)
            gw_pd_audio_start();
        scheduler_resync_pending = 1;
    } else if (event == kEventTerminate) {
        gw_pd_audio_shutdown();
        gw_machine_shutdown(&machine);
        gw_package_free(&loaded_package);
        rom_picker_free();
    }

    if (scheduler_resync_pending && pd != NULL) {
        resync_scheduler();
        scheduler_resync_pending = 0;
    }

    return 0;
}
