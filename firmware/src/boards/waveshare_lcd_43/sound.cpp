#include "../../hal/sound_hal.h"

// No audio codec on this board — same posture as the AMOLED-2.06 and C6 1.8
// ports. The shared chime engine is ready if hardware ever appears.

void sound_hal_init(void) {}
void sound_hal_tick(void) {}
void sound_hal_play_reset(void) {}
