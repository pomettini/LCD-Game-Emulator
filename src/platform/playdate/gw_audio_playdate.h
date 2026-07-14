#ifndef GW_AUDIO_PLAYDATE_H
#define GW_AUDIO_PLAYDATE_H

#include "pd_api.h"

#include <stdbool.h>
#include <stdint.h>

int gw_pd_audio_init(PlaydateAPI *pd);
void gw_pd_audio_shutdown(void);
void gw_pd_audio_start(void);
void gw_pd_audio_stop(void);
void gw_pd_audio_set_buzzer(void *userdata, bool enabled);
void gw_pd_audio_advance_clock(void *userdata, uint32_t oscillator_cycles);
uint32_t gw_pd_audio_underruns(void);

#endif
