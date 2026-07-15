/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "gw_machine.h"

#include "sm500.h"
#include "sm510.h"

#include <string.h>

static GWMachine *active_machine;

static uint32_t read_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint32_t logical_inputs(void)
{
    GWMachine *machine = active_machine;
    if (machine == NULL || machine->platform.read_inputs == NULL)
        return 0;
    return machine->platform.read_inputs(machine->platform.userdata) & 0xff;
}

static int execute_sm5a_one(void)
{
    int consumed;
    m_icount = 1;
    sm5a_execute_run();
    consumed = 1 - m_icount;
    m_icount = 0;
    return consumed > 0 ? consumed : 1;
}

int gw_machine_init(GWMachine *machine, const GWPackage *package,
                    const GWPlatform *platform)
{
    if (machine == NULL || package == NULL || platform == NULL ||
        package->game == NULL || !package->game->enabled)
        return 0;

    memset(machine, 0, sizeof(*machine));
    machine->package = package;
    machine->platform = *platform;
    machine->program = package->payload + package->program.offset;
    machine->program_size = package->program.size;
    machine->keyboard = package->payload + package->keyboard.offset;

    /* The first execution-enabled family uses the shared SM5A core. */
    if (memcmp(package->cpu_name, "SM5A___", 8) != 0)
        return 0;

    active_machine = machine;
    flag_lcd_deflicker_level = (uint8_t)((package->flags >> 6) & 3);
    machine->execute_one = execute_sm5a_one;
    sm5a_device_start();
    sm5a_device_reset();
    return 1;
}

void gw_machine_reset(GWMachine *machine)
{
    if (machine == NULL || machine != active_machine)
        return;
    machine->oscillator_cycles = 0;
    sm5a_device_reset();
}

void gw_machine_shutdown(GWMachine *machine)
{
    if (machine != NULL && machine == active_machine) {
        active_machine = NULL;
        memset(machine, 0, sizeof(*machine));
    }
}

uint32_t gw_machine_run_cycles(GWMachine *machine, uint32_t cycles)
{
    int64_t remaining = cycles;
    uint32_t executed = 0;

    if (machine == NULL || machine != active_machine ||
        machine->execute_one == NULL)
        return 0;

    m_k_active = logical_inputs() != 0;
    while (remaining >= m_clk_div) {
        uint64_t before = machine->oscillator_cycles;
        uint32_t oscillator_clocks;
        machine->execute_one();
        oscillator_clocks = (uint32_t)(machine->oscillator_cycles - before);
        if (oscillator_clocks == 0)
            oscillator_clocks = (uint32_t)m_clk_div;
        remaining -= oscillator_clocks;
        executed += oscillator_clocks;
    }
    return executed;
}

void gw_machine_emit_lcd(const GWMachine *machine)
{
    unsigned common;
    unsigned output;

    if (machine == NULL || machine != active_machine ||
        machine->platform.set_lcd_segment == NULL)
        return;

    for (common = 0; common < 2; ++common) {
        for (output = 0; output < (unsigned)m_o_pins; ++output) {
            uint8_t bits = flag_lcd_deflicker_level != 0
                ? (common ? m_ox_state[output] : m_o_state[output])
                : (common ? m_ox[output] : m_o[output]);
            unsigned digit;
            for (digit = 0; digit < 4; ++digit) {
                uint32_t segment = 8u * output + 2u * digit + common;
                machine->platform.set_lcd_segment(
                    machine->platform.userdata, segment,
                    m_bp != 0 && (bits & (1u << digit)) != 0);
            }
        }
    }
}

int gw_machine_set_time(GWMachine *machine, unsigned hour, unsigned minute,
                        unsigned second)
{
    const uint8_t *address;
    unsigned display_hour;
    unsigned pm_mask;

    if (machine == NULL || machine != active_machine || hour > 23 ||
        minute > 59 || second > 59)
        return 0;
    address = machine->package->time_addresses;
    if (address[0] >= sizeof(gw_ram) || address[1] >= sizeof(gw_ram) ||
        address[2] >= sizeof(gw_ram) || address[3] >= sizeof(gw_ram) ||
        address[4] >= sizeof(gw_ram) || address[5] >= sizeof(gw_ram) ||
        (address[0] == 0 && address[1] == 0))
        return 0;

    pm_mask = address[6];
    display_hour = hour % 12;
    if (display_hour == 0)
        display_hour = 12;
    gw_ram[address[0]] = (uint8_t)(display_hour / 10);
    gw_ram[address[1]] = (uint8_t)(display_hour % 10);
    if (hour >= 12 && pm_mask != 0)
        gw_ram[address[0]] |= (uint8_t)pm_mask;
    gw_ram[address[2]] = (uint8_t)(minute / 10);
    gw_ram[address[3]] = (uint8_t)(minute % 10);
    gw_ram[address[4]] = (uint8_t)(second / 10);
    gw_ram[address[5]] = (uint8_t)(second % 10);
    return 1;
}

uint8_t gw_machine_core_read_program(uint16_t address)
{
    if (active_machine == NULL)
        return 0;

    /*
     * SM5A maps 0x700-0x73f across the whole 0x700-0x7ff range.  Packages
     * store only the physical 0x740 bytes, so emulate the address-line mirror
     * before applying the package bounds check.
     */
    address &= 0x7ff;
    if (address >= 0x740)
        address = (uint16_t)(0x700 | (address & 0x3f));
    if (address >= active_machine->program_size)
        return 0;
    return active_machine->program[address];
}

uint8_t gw_machine_core_read_k(uint8_t select)
{
    uint32_t buttons = logical_inputs();
    uint8_t result = 0;
    unsigned row;
    unsigned column;

    if (active_machine == NULL || buttons == 0)
        return 0;
    if (select == 0)
        select = 2;

    for (row = 0; row < 8; ++row) {
        uint32_t mapping;
        if ((select & (1u << row)) == 0)
            continue;
        mapping = read_le32(active_machine->keyboard + row * 4);
        for (column = 0; column < 4; ++column) {
            if (((mapping >> (column * 8)) & buttons & 0xff) != 0)
                result |= (uint8_t)(1u << column);
        }
    }
    return result;
}

static uint8_t read_direct_input(unsigned index)
{
    uint32_t mapping;
    uint32_t buttons = logical_inputs();
    if (active_machine == NULL || buttons == 0)
        return 1;
    mapping = read_le32(active_machine->keyboard + index * 4);
    return (mapping & buttons) != 0 ? 0 : 1;
}

uint8_t gw_machine_core_read_ba(void)
{
    return read_direct_input(8);
}

uint8_t gw_machine_core_read_b(void)
{
    return read_direct_input(9);
}

void gw_machine_core_write_buzzer(uint8_t value)
{
    unsigned sound_mode;
    bool enabled;
    if (active_machine == NULL)
        return;

    sound_mode = (active_machine->package->flags >> 1) & 7u;
    switch (sound_mode) {
    case 2: enabled = ((value >> 1) & 1) != 0; break;
    case 3: enabled = (value & 3) != 0; break;
    default: enabled = (value & 1) != 0; break;
    }
    if (active_machine->platform.set_buzzer != NULL)
        active_machine->platform.set_buzzer(active_machine->platform.userdata,
                                             enabled);
}

void gw_machine_core_advance_clock(void)
{
    if (active_machine == NULL)
        return;
    ++active_machine->oscillator_cycles;
    if (active_machine->platform.advance_clock != NULL)
        active_machine->platform.advance_clock(
            active_machine->platform.userdata, 1);
}
