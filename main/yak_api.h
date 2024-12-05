#ifndef YAK_API_H
#define YAK_API_H

#include "esp_err.h"
#include "cJSON.h"

esp_err_t yak_api_publish(const char *stream_name, cJSON *event);

#endif
