#ifndef GW_MACHINE_H
#define GW_MACHINE_H

#include "gw_package.h"
#include "gw_platform.h"

#include <stddef.h>
#include <stdint.h>

#define GW_INPUT_LEFT  (UINT32_C(1) << 0)
#define GW_INPUT_UP    (UINT32_C(1) << 1)
#define GW_INPUT_RIGHT (UINT32_C(1) << 2)
#define GW_INPUT_DOWN  (UINT32_C(1) << 3)
#define GW_INPUT_A     (UINT32_C(1) << 4)
#define GW_INPUT_B     (UINT32_C(1) << 5)
#define GW_INPUT_TIME  (UINT32_C(1) << 6)
#define GW_INPUT_GAME  (UINT32_C(1) << 7)

typedef struct GWMachine {
    const GWPackage *package;
    GWPlatform platform;
    const uint8_t *program;
    size_t program_size;
    const uint8_t *keyboard;
    int (*execute_one)(void);
    uint64_t oscillator_cycles;
} GWMachine;

int gw_machine_init(GWMachine *machine, const GWPackage *package,
                    const GWPlatform *platform);
void gw_machine_reset(GWMachine *machine);
void gw_machine_shutdown(GWMachine *machine);
uint32_t gw_machine_run_cycles(GWMachine *machine, uint32_t cycles);
void gw_machine_emit_lcd(const GWMachine *machine);
int gw_machine_set_time(GWMachine *machine, unsigned hour, unsigned minute,
                        unsigned second);

/* Narrow host hooks used by the minimally adapted SM5xx core. */
uint8_t gw_machine_core_read_program(uint16_t address);
uint8_t gw_machine_core_read_k(uint8_t select);
uint8_t gw_machine_core_read_ba(void);
uint8_t gw_machine_core_read_b(void);
void gw_machine_core_write_buzzer(uint8_t value);
void gw_machine_core_advance_clock(void);

#endif
