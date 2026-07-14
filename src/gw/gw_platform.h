#ifndef GW_PLATFORM_H
#define GW_PLATFORM_H

#include <stdbool.h>
#include <stdint.h>

typedef struct GWPlatform {
    void (*set_lcd_segment)(void *userdata, uint32_t segment, bool enabled);
    uint32_t (*read_inputs)(void *userdata);
    void (*set_buzzer)(void *userdata, bool enabled);
    void (*advance_clock)(void *userdata, uint32_t oscillator_cycles);
    uint64_t (*get_time_us)(void *userdata);
    void *userdata;
} GWPlatform;

#endif
