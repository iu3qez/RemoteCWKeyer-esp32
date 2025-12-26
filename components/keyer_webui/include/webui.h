#ifndef KEYER_WEBUI_H
#define KEYER_WEBUI_H

#include "esp_err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t webui_init(void);
esp_err_t webui_start(void);
esp_err_t webui_stop(void);

void webui_timeline_push(const char *event_type, const char *json_data);
void webui_decoder_push_char(char c, uint8_t wpm);
void webui_decoder_push_word(void);

#ifdef __cplusplus
}
#endif

#endif /* KEYER_WEBUI_H */
