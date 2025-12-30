#ifndef WIFI_CONNECT_H
#define WIFI_CONNECT_H

#include <stdbool.h>
#include "esp_netif.h"

#ifdef __cplusplus
extern "C" {
#endif

bool wifi_connect(void);
bool wifi_is_connected(void);
esp_ip4_addr_t wifi_get_ip_addr(void);
const char* wifi_get_ssid(void);

#ifdef __cplusplus
}
#endif

#endif