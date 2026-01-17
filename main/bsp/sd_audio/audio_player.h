#pragma once

#include <stdbool.h>
#include "esp_err.h"

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include "esp_err.h"

esp_err_t audio_player_init(void);
esp_err_t audio_player_start(void);
void audio_player_stop(void);
bool audio_player_is_running(void);
void audio_player_next(void);
void audio_player_prev(void);

#ifdef __cplusplus
}
#endif
