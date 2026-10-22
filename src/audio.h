#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Streams 8-bit 16 kHz mono PCM from the SD card to the speaker: TIM16
 * runs the speaker pin as PWM (1000 levels), TIM2 paces the samples
 * (upsampled to 32 kHz) and DMA feeds them from a double buffer that a
 * thread refills. */

#define AUDIO_RATE 16000 /* of the streams */

typedef struct Audio Audio;

/* NULL if the speaker is busy or the Flipper is in stealth (silent) mode. */
Audio* audio_start(void);
void audio_stop(Audio* a);

#define AUDIO_NO_LOOP UINT32_MAX

/* Play /ext/apps_data/second/<name> from offset_ms; name NULL = silence.
 * At the end of the file, carry on from sample loop (AUDIO_NO_LOOP: stop). */
void audio_play(Audio* a, const char* name, uint32_t offset_ms, uint32_t loop);
void audio_set_volume(Audio* a, int volume); /* 0..256 */
void audio_pause(Audio* a, bool paused);

/* Buffer halves skipped because a refill came too late (glitches). */
uint32_t audio_late_slots(Audio* a);
/* Slowest buffer refill (ms) since the last call. */
uint32_t audio_take_fill_max(Audio* a);
