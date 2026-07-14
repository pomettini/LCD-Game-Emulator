/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "gw_audio_playdate.h"

#include <string.h>

#define OUTPUT_RATE 44100u
#define OSCILLATOR_RATE 32768u
#define RING_SIZE 4096u
#define RING_MASK (RING_SIZE - 1u)
#define START_SILENCE 1024u
#define BUZZER_LEVEL 7000

static PlaydateAPI *audio_pd;
static SoundSource *audio_source;
static int16_t ring[RING_SIZE];
static volatile uint32_t ring_write;
static volatile uint32_t ring_read;
static volatile uint32_t underrun_count;
static volatile int active;
static int buzzer_state;
static uint32_t resample_phase;
static int32_t highpass_previous_input;
static int32_t highpass_previous_output;

static int audio_callback(void *context, int16_t *left, int16_t *right, int len)
{
    int i;
    int underflowed = 0;
    (void)context;
    (void)right;

    if (!active) {
        memset(left, 0, (size_t)len * sizeof(*left));
        return 1;
    }

    for (i = 0; i < len; ++i) {
        int32_t input;
        int32_t output;
        if (ring_read != ring_write) {
            input = ring[ring_read & RING_MASK];
            ++ring_read;
        } else {
            input = 0;
            underflowed = 1;
        }

        /* Remove the piezo pin's DC component and gently suppress edge clicks. */
        output = input - highpass_previous_input +
                 (highpass_previous_output * 32604) / 32768;
        highpass_previous_input = input;
        highpass_previous_output = output;
        if (output > 32767) output = 32767;
        if (output < -32768) output = -32768;
        left[i] = (int16_t)output;
    }
    if (underflowed)
        ++underrun_count;
    return 1;
}

static void push_sample(int16_t sample)
{
    uint32_t next = ring_write + 1;
    if (next - ring_read > RING_SIZE)
        return;
    ring[ring_write & RING_MASK] = sample;
    ring_write = next;
}

int gw_pd_audio_init(PlaydateAPI *pd)
{
    if (pd == NULL)
        return 0;
    audio_pd = pd;
    audio_source = pd->sound->addSource(audio_callback, NULL, 0);
    return audio_source != NULL;
}

void gw_pd_audio_shutdown(void)
{
    active = 0;
    if (audio_pd != NULL && audio_source != NULL)
        audio_pd->sound->removeSource(audio_source);
    audio_source = NULL;
    audio_pd = NULL;
}

void gw_pd_audio_start(void)
{
    uint32_t i;
    active = 0;
    ring_read = 0;
    ring_write = 0;
    underrun_count = 0;
    buzzer_state = 0;
    resample_phase = 0;
    highpass_previous_input = 0;
    highpass_previous_output = 0;
    for (i = 0; i < START_SILENCE; ++i)
        push_sample(0);
    active = 1;
}

void gw_pd_audio_stop(void)
{
    active = 0;
    ring_read = ring_write = 0;
}

void gw_pd_audio_set_buzzer(void *userdata, bool enabled)
{
    (void)userdata;
    buzzer_state = enabled ? 1 : 0;
}

void gw_pd_audio_advance_clock(void *userdata, uint32_t oscillator_cycles)
{
    (void)userdata;
    while (oscillator_cycles-- != 0) {
        resample_phase += OUTPUT_RATE;
        while (resample_phase >= OSCILLATOR_RATE) {
            resample_phase -= OSCILLATOR_RATE;
            push_sample((int16_t)(buzzer_state ? BUZZER_LEVEL : -BUZZER_LEVEL));
        }
    }
}

uint32_t gw_pd_audio_underruns(void)
{
    return underrun_count;
}
