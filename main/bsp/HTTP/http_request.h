#ifndef HTTP_REQUEST_H
#define HTTP_REQUEST_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <time.h>
#include "esp_err.h"

typedef struct {
    int heart_rate;
    int breathing_rate;
    char sleep_status[32];
} health_data_t;

#define ALARM_MAX_COUNT       16
#define ALARM_TIME_STR_LEN    9   /* HH:MM:SS */
#define ALARM_DATE_STR_LEN    11  /* YYYY-MM-DD */
#define ALARM_REPEAT_STR_LEN  64

typedef enum {
    ALARM_TYPE_ONCE   = 1,
    ALARM_TYPE_REPEAT = 2,
} alarm_type_t;

typedef struct {
    int id;
    alarm_type_t type;
    char alarm_time[ALARM_TIME_STR_LEN];
    char target_date[ALARM_DATE_STR_LEN];
    char repeat_days[ALARM_REPEAT_STR_LEN];
    int status;
    uint8_t repeat_mask;
    time_t next_trigger;
} alarm_info_t;

typedef struct {
    alarm_info_t items[ALARM_MAX_COUNT];
    size_t count;
} alarm_list_t;

typedef void (*alarm_trigger_cb_t)(const alarm_info_t *alarm, void *user_ctx);

void wifi_init_sta(void);
bool wifi_is_connected(void);
bool wifi_wait_connected(uint32_t timeout_ms);
esp_err_t http_send_health_data(const health_data_t *data);
esp_err_t http_set_alarm_server(const char *host, uint16_t port);
esp_err_t http_set_alarm_user(const char *user_id);
esp_err_t http_fetch_alarms(alarm_list_t *out_list);
esp_err_t http_update_alarm_status(int alarm_id, int status);
time_t alarm_compute_next_trigger(const alarm_info_t *alarm, const struct tm *now_local);
bool alarm_is_due(const alarm_info_t *alarm, const struct tm *now_local);
esp_err_t alarm_service_start(uint32_t fetch_interval_ms, alarm_trigger_cb_t cb, void *cb_ctx);

#endif // HTTP_REQUEST_H
