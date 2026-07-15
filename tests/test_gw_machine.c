#include "gw_game_db.h"
#include "gw_machine.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define SM5A_PROGRAM_SIZE 0x740u
#define KEYBOARD_SIZE 40u

static uint32_t input_state;
static uint64_t advanced_cycles;
static unsigned emitted_segments;

static uint32_t read_inputs(void *userdata)
{
    (void)userdata;
    return input_state;
}

static void set_buzzer(void *userdata, bool enabled)
{
    (void)userdata;
    (void)enabled;
}

static void advance_clock(void *userdata, uint32_t cycles)
{
    (void)userdata;
    advanced_cycles += cycles;
}

static void set_segment(void *userdata, uint32_t segment, bool enabled)
{
    (void)userdata;
    (void)segment;
    (void)enabled;
    ++emitted_segments;
}

static const GWGameInfo *find_sm5a_game(const char signature[8])
{
    const char cpu[8] = {'S','M','5','A','_','_','_','\0'};
    return gw_game_db_find(signature, cpu);
}

static void exercise_game(const char signature[8], GWInputProfile profile)
{
    uint8_t payload[SM5A_PROGRAM_SIZE + KEYBOARD_SIZE];
    GWPackage package;
    GWPlatform platform;
    GWMachine machine;
    uint32_t executed;

    memset(payload, 0, sizeof(payload));
    memset(&package, 0, sizeof(package));
    memset(&platform, 0, sizeof(platform));
    package.game = find_sm5a_game(signature);
    assert(package.game != NULL);
    assert(package.game->enabled == 1);
    assert(package.game->input_profile == profile);
    memcpy(package.cpu_name, "SM5A___", 8);
    package.payload = payload;
    package.payload_size = sizeof(payload);
    package.program.offset = 0;
    package.program.size = SM5A_PROGRAM_SIZE;
    package.keyboard.offset = SM5A_PROGRAM_SIZE;
    package.keyboard.size = KEYBOARD_SIZE;
    payload[0x700] = 0x5a;

    platform.read_inputs = read_inputs;
    platform.set_buzzer = set_buzzer;
    platform.advance_clock = advance_clock;
    platform.set_lcd_segment = set_segment;

    input_state = GW_INPUT_LEFT | GW_INPUT_A;
    advanced_cycles = 0;
    emitted_segments = 0;
    assert(gw_machine_init(&machine, &package, &platform));
    assert(gw_machine_core_read_program(0x740) == 0x5a);

    executed = gw_machine_run_cycles(&machine, 2048);
    assert(executed > 0 && executed <= 2048);
    assert(machine.oscillator_cycles == executed);
    assert(advanced_cycles == executed);

    gw_machine_emit_lcd(&machine);
    assert(emitted_segments > 0);
    gw_machine_reset(&machine);
    assert(machine.oscillator_cycles == 0);
    gw_machine_shutdown(&machine);
}

int main(void)
{
    const char ball[8] = {'g','n','w','_','b','a','l','l'};
    const char flagman[8] = {'_','f','l','a','g','m','a','n'};

    exercise_game(ball, GW_INPUT_PROFILE_BALL);
    exercise_game(flagman, GW_INPUT_PROFILE_FLAGMAN);
    puts("gw_machine SM5A tests passed");
    return 0;
}
