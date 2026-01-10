#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <stdlib.h>
#include <time.h>
#include <ctype.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_netif.h"
#include "cJSON.h"
#include "http_request.h"

#define TAG "HTTP_CLIENT"

/* 如果定义了USE_EXTERNAL_WIFI，则WiFi由外部管理（main项目）*/
#define USE_EXTERNAL_WIFI 1

#if !USE_EXTERNAL_WIFI
// Wi-Fi 配置 - 请根据实际网络修改
#define WIFI_SSID      "TP-LINK"
#define WIFI_PASS      "708708708"
#define MAXIMUM_RETRY  5
#define WIFI_RECONNECT_PERIOD_MS 10000
#endif

// 服务器配置
#define SERVER_URL     "http://118.195.133.25:6060/api/health/upload"

#define ALARM_DEFAULT_HOST   "118.195.133.25"
#define ALARM_DEFAULT_PORT   6060
#define ALARM_FETCH_PERIOD_MS 60000
#define ALARM_TASK_STACK      6144
#define ALARM_TASK_PRIO       4

#if !USE_EXTERNAL_WIFI
static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1
static int s_retry_num = 0;
static TaskHandle_t s_wifi_reconnect_task = NULL;

static void wifi_reconnect_task(void *arg);
#endif

static char s_alarm_host[64] = ALARM_DEFAULT_HOST;
static uint16_t s_alarm_port = ALARM_DEFAULT_PORT;
static char s_alarm_user[32] = "user123";
static uint32_t s_alarm_fetch_period_ms = ALARM_FETCH_PERIOD_MS;
static alarm_list_t s_alarm_list = {0};
static SemaphoreHandle_t s_alarm_mutex = NULL;
static TaskHandle_t s_alarm_fetch_task = NULL;
static TaskHandle_t s_alarm_monitor_task = NULL;
static alarm_trigger_cb_t s_alarm_cb = NULL;
static void *s_alarm_cb_ctx = NULL;

static void log_alarm_snapshot(const alarm_list_t *list)
{
    time_t now_ts = time(NULL);
    struct tm now_tm = {0};
    localtime_r(&now_ts, &now_tm);

    ESP_LOGI(TAG, "RTC now %04d-%02d-%02d %02d:%02d:%02d, alarms: %u",
             now_tm.tm_year + 1900, now_tm.tm_mon + 1, now_tm.tm_mday,
             now_tm.tm_hour, now_tm.tm_min, now_tm.tm_sec,
             list ? (unsigned)list->count : 0);

    if (!list) {
        return;
    }

    for (size_t i = 0; i < list->count; ++i) {
        const alarm_info_t *a = &list->items[i];
        struct tm nt = {0};
        if (a->next_trigger > 0) {
            localtime_r(&a->next_trigger, &nt);
        }
        ESP_LOGI(TAG, "id=%d type=%d status=%d time=%s date=%s repeat=%s next=%04d-%02d-%02d %02d:%02d:%02d",
                 a->id, a->type, a->status,
                 a->alarm_time, a->target_date,
                 a->repeat_days,
                 (a->next_trigger > 0) ? (nt.tm_year + 1900) : 0,
                 (a->next_trigger > 0) ? (nt.tm_mon + 1) : 0,
                 (a->next_trigger > 0) ? nt.tm_mday : 0,
                 (a->next_trigger > 0) ? nt.tm_hour : 0,
                 (a->next_trigger > 0) ? nt.tm_min : 0,
                 (a->next_trigger > 0) ? nt.tm_sec : 0);
    }
}

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} http_resp_buffer_t;

#if USE_EXTERNAL_WIFI
/* 使用外部WiFi管理时，声明外部定义的函数 */
extern bool wifi_is_connected(void);

bool wifi_wait_connected(uint32_t timeout_ms)
{
    uint32_t elapsed = 0;
    const uint32_t step = 100;
    while (elapsed < timeout_ms) {
        if (wifi_is_connected()) {
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(step));
        elapsed += step;
    }
    return wifi_is_connected();
}

void wifi_init_sta(void)
{
    /* WiFi由外部管理，这里不做任何事 */
    ESP_LOGI(TAG, "WiFi managed externally, skipping init");
}
#else
/* 独立WiFi管理 */
bool wifi_is_connected(void)
{
    if (s_wifi_event_group == NULL) return false;
    EventBits_t bits = xEventGroupGetBits(s_wifi_event_group);
    return (bits & WIFI_CONNECTED_BIT) != 0;
}

bool wifi_wait_connected(uint32_t timeout_ms)
{
    if (s_wifi_event_group == NULL) return false;
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           WIFI_CONNECTED_BIT,
                                           pdFALSE,
                                           pdFALSE,
                                           pdMS_TO_TICKS(timeout_ms));
    return (bits & WIFI_CONNECTED_BIT) != 0;
}

static void event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        esp_wifi_connect();
        s_retry_num++;
        if (s_retry_num >= MAXIMUM_RETRY) {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI(TAG, "connect to the AP fail (%d)", s_retry_num);
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

void wifi_init_sta(void)
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .sae_pwe_h2e = WPA3_SAE_PWE_BOTH,
        },
    };

    ESP_LOGI(TAG, "Attempting to connect to Wi-Fi SSID: %s", WIFI_SSID);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE,
                                           pdFALSE,
                                           portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "connected to ap SSID:%s", WIFI_SSID);
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI(TAG, "Failed to connect to SSID:%s", WIFI_SSID);
    } else {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
    }

    if (!s_wifi_reconnect_task) {
        xTaskCreate(wifi_reconnect_task, "wifi_reconnect", 3072, NULL, 4, &s_wifi_reconnect_task);
    }
}

static void wifi_reconnect_task(void *arg)
{
    while (1) {
        if (!wifi_is_connected()) {
            ESP_LOGW(TAG, "Wi-Fi down, reconnecting...");
            esp_wifi_disconnect();
            esp_wifi_connect();
        }
        vTaskDelay(pdMS_TO_TICKS(WIFI_RECONNECT_PERIOD_MS));
    }
}
#endif /* !USE_EXTERNAL_WIFI */

static bool ensure_resp_capacity(http_resp_buffer_t *buf, size_t incoming)
{
    if (buf == NULL || buf->data == NULL) {
        return false;
    }

    if (buf->len + incoming + 1 <= buf->cap) {
        return true;
    }

    size_t new_cap = buf->cap;
    while (new_cap < buf->len + incoming + 1 && new_cap < 16384) {
        new_cap *= 2;
    }

    if (new_cap < buf->len + incoming + 1) {
        return false;
    }

    char *new_data = (char *)realloc(buf->data, new_cap);
    if (!new_data) {
        return false;
    }

    buf->data = new_data;
    buf->cap = new_cap;
    return true;
}

static esp_err_t collect_http_event(esp_http_client_event_t *evt)
{
    if (evt->event_id == HTTP_EVENT_ON_DATA && evt->user_data) {
        http_resp_buffer_t *ctx = (http_resp_buffer_t *)evt->user_data;
        if (!ensure_resp_capacity(ctx, evt->data_len)) {
            ESP_LOGE(TAG, "Response buffer overflow");
            return ESP_FAIL;
        }
        memcpy(ctx->data + ctx->len, evt->data, evt->data_len);
        ctx->len += evt->data_len;
        ctx->data[ctx->len] = '\0';
    }
    return ESP_OK;
}

static bool parse_time_of_day(const char *time_str, int *hour, int *minute, int *second)
{
    if (!time_str || !hour || !minute || !second) {
        return false;
    }

    int h = 0, m = 0, s = 0;
    int parsed = sscanf(time_str, "%d:%d:%d", &h, &m, &s);
    if (parsed < 2) {
        return false;
    }
    if (parsed == 2) {
        s = 0;
    }

    if (h < 0 || h > 23 || m < 0 || m > 59 || s < 0 || s > 59) {
        return false;
    }

    *hour = h;
    *minute = m;
    *second = s;
    return true;
}

static bool parse_date_ymd(const char *date_str, int *year, int *month, int *day)
{
    if (!date_str || !year || !month || !day) {
        return false;
    }

    int y = 0, m = 0, d = 0;
    if (sscanf(date_str, "%d-%d-%d", &y, &m, &d) != 3) {
        return false;
    }

    if (y < 1970 || m < 1 || m > 12 || d < 1 || d > 31) {
        return false;
    }

    *year = y;
    *month = m;
    *day = d;
    return true;
}

static bool build_datetime(const char *date_str, const char *time_str, struct tm *out)
{
    int year = 0, month = 0, day = 0;
    int hour = 0, minute = 0, second = 0;

    if (!parse_date_ymd(date_str, &year, &month, &day)) {
        return false;
    }
    if (!parse_time_of_day(time_str, &hour, &minute, &second)) {
        return false;
    }

    memset(out, 0, sizeof(struct tm));
    out->tm_year = year - 1900;
    out->tm_mon = month - 1;
    out->tm_mday = day;
    out->tm_hour = hour;
    out->tm_min = minute;
    out->tm_sec = second;
    return true;
}

static int weekday_index_from_number(int num)
{
    if (num < 1 || num > 7) {
        return -1;
    }
    return (num == 7) ? 6 : (num - 1);
}

static uint8_t parse_repeat_mask_from_string(const char *str)
{
    if (!str) {
        return 0;
    }

    uint8_t mask = 0;
    const char *p = str;
    while (*p) {
        while (*p == ' ' || *p == ',' || *p == ';') {
            p++;
        }
        if (*p == '\0') {
            break;
        }
        int val = atoi(p);
        int idx = weekday_index_from_number(val);
        if (idx >= 0) {
            mask |= (1U << idx);
        }
        while (*p && *p != ',' && *p != ';') {
            p++;
        }
    }
    return mask;
}

static uint8_t parse_repeat_mask(const cJSON *node, char *repeat_buf, size_t buf_len)
{
    if (repeat_buf && buf_len > 0) {
        repeat_buf[0] = '\0';
    }

    if (!node) {
        return 0;
    }

    if (cJSON_IsString(node) && node->valuestring) {
        if (repeat_buf && buf_len > 0) {
            snprintf(repeat_buf, buf_len, "%s", node->valuestring);
        }
        return parse_repeat_mask_from_string(node->valuestring);
    }

    if (cJSON_IsArray(node)) {
        uint8_t mask = 0;
        size_t written = 0;
        cJSON *child = NULL;
        cJSON_ArrayForEach(child, node) {
            int val = 0;
            if (cJSON_IsNumber(child)) {
                val = (int)child->valuedouble;
            } else if (cJSON_IsString(child) && child->valuestring) {
                val = atoi(child->valuestring);
            } else {
                continue;
            }

            int idx = weekday_index_from_number(val);
            if (idx >= 0) {
                mask |= (1U << idx);
                if (repeat_buf && buf_len > 0 && written < buf_len - 1) {
                    int n = snprintf(repeat_buf + written, buf_len - written, (written > 0) ? ",%d" : "%d", val);
                    if (n > 0) {
                        written += (size_t)n;
                    }
                }
            }
        }
        return mask;
    }

    return 0;
}

static bool time_is_valid(time_t now)
{
    return now > 1600000000; /* ~2020-09-13 */
}

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGD(TAG, "HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED");
            break;
        case HTTP_EVENT_HEADER_SENT:
            ESP_LOGD(TAG, "HTTP_EVENT_HEADER_SENT");
            break;
        case HTTP_EVENT_ON_HEADER:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
            break;
        case HTTP_EVENT_ON_DATA:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
            if (!esp_http_client_is_chunked_response(evt->client)) {
                printf("%.*s", evt->data_len, (char *)evt->data);
            }
            break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
            break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGD(TAG, "HTTP_EVENT_DISCONNECTED");
            break;
        case HTTP_EVENT_REDIRECT:
            ESP_LOGD(TAG, "HTTP_EVENT_REDIRECT");
            break;
        default:
            break;
    }
    return ESP_OK;
}

esp_err_t http_send_health_data(const health_data_t *data)
{
    char *post_data = NULL;
    esp_err_t err = ESP_FAIL;

    if (!data) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!wifi_wait_connected(5000)) {
        ESP_LOGE(TAG, "Wi-Fi not connected, skip upload");
        return ESP_FAIL;
    }

    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif) {
        esp_netif_ip_info_t ip_info;
        esp_netif_get_ip_info(netif, &ip_info);
        if (ip_info.ip.addr == 0) {
            ESP_LOGE(TAG, "Device has no IP address. Check Wi-Fi connection.");
            return ESP_FAIL;
        }
    }

    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        ESP_LOGE(TAG, "Failed to create JSON object");
        return ESP_FAIL;
    }
    cJSON_AddNumberToObject(root, "heartRate", data->heart_rate);
    cJSON_AddNumberToObject(root, "breathingRate", data->breathing_rate);
    cJSON_AddStringToObject(root, "sleepStatus", data->sleep_status[0] ? data->sleep_status : "UNKNOWN");

    post_data = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (post_data == NULL) {
        ESP_LOGE(TAG, "Failed to print JSON string");
        return ESP_FAIL;
    }

    esp_http_client_config_t config = {
        .url = SERVER_URL,
        .event_handler = http_event_handler,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 5000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);

    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, post_data, strlen(post_data));

    err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "HTTP POST Status = %d, content_length = %" PRId64,
                 esp_http_client_get_status_code(client), 
                 esp_http_client_get_content_length(client));
    } else {
        ESP_LOGE(TAG, "HTTP POST request failed: %s", esp_err_to_name(err));
        ESP_LOGE(TAG, "Target URL: %s", SERVER_URL);
        ESP_LOGE(TAG, "Please check if Server IP is correct and Port 6060 is open.");
    }

    esp_http_client_cleanup(client);
    free(post_data);

    return err;
}

esp_err_t http_set_alarm_server(const char *host, uint16_t port)
{
    if (!host || strlen(host) == 0 || strlen(host) >= sizeof(s_alarm_host) || port == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    snprintf(s_alarm_host, sizeof(s_alarm_host), "%s", host);
    s_alarm_port = port;
    return ESP_OK;
}

esp_err_t http_set_alarm_user(const char *user_id)
{
    if (!user_id || strlen(user_id) == 0 || strlen(user_id) >= sizeof(s_alarm_user)) {
        return ESP_ERR_INVALID_ARG;
    }
    snprintf(s_alarm_user, sizeof(s_alarm_user), "%s", user_id);
    return ESP_OK;
}

time_t alarm_compute_next_trigger(const alarm_info_t *alarm, const struct tm *now_local)
{
    if (!alarm || !now_local) {
        return 0;
    }

    int hour = 0, minute = 0, second = 0;
    if (!parse_time_of_day(alarm->alarm_time, &hour, &minute, &second)) {
        return 0;
    }

    struct tm now_copy = *now_local;
    time_t now_ts = mktime(&now_copy);
    if (now_ts == (time_t)-1) {
        return 0;
    }

    if (alarm->type == ALARM_TYPE_ONCE) {
        if (alarm->target_date[0] == '\0') {
            return 0;
        }
        struct tm target_tm;
        if (!build_datetime(alarm->target_date, alarm->alarm_time, &target_tm)) {
            return 0;
        }
        time_t target_ts = mktime(&target_tm);
        if (target_ts == (time_t)-1) {
            return 0;
        }
        /* Allow triggering within the same minute (check hour:min only) */
        struct tm target_check = target_tm;
        struct tm now_check = *now_local;
        target_check.tm_sec = 0;  /* Ignore seconds for comparison */
        now_check.tm_sec = 0;
        time_t target_min_ts = mktime(&target_check);
        time_t now_min_ts = mktime(&now_check);
        if (target_min_ts == (time_t)-1 || now_min_ts == (time_t)-1 || target_min_ts < now_min_ts) {
            return 0;
        }
        return target_ts;
    }

    uint8_t mask = alarm->repeat_mask;
    if (mask == 0) {
        mask = 0x7F; /* 默认每天 */
    }

    for (int offset = 0; offset < 14; ++offset) {
        struct tm cand = *now_local;
        cand.tm_hour = hour;
        cand.tm_min = minute;
        cand.tm_sec = second;
        cand.tm_mday += offset;
        time_t cand_ts = mktime(&cand);
        if (cand_ts == (time_t)-1) {
            continue;
        }
        int idx = (cand.tm_wday == 0) ? 6 : (cand.tm_wday - 1);
        if ((mask & (1U << idx)) != 0 && cand_ts >= now_ts) {
            return cand_ts;
        }
    }

    return 0;
}

bool alarm_is_due(const alarm_info_t *alarm, const struct tm *now_local)
{
    if (!alarm || !now_local || alarm->next_trigger == 0) {
        return false;
    }
    struct tm trigger_tm;
    localtime_r(&alarm->next_trigger, &trigger_tm);
    
    /* Match by hour and minute only, not second */
    return (now_local->tm_hour == trigger_tm.tm_hour && 
            now_local->tm_min == trigger_tm.tm_min);
}

static esp_err_t http_fetch_raw(const char *url, http_resp_buffer_t *resp)
{
    if (!url || !resp) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_http_client_config_t config = {
        .url = url,
        .event_handler = collect_http_event,
        .user_data = resp,
        .method = HTTP_METHOD_GET,
        .timeout_ms = 5000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        return ESP_FAIL;
    }

    esp_err_t err = esp_http_client_perform(client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP GET failed: %s", esp_err_to_name(err));
    } else {
        int status = esp_http_client_get_status_code(client);
        if (status != 200) {
            ESP_LOGE(TAG, "HTTP GET status %d", status);
            err = ESP_FAIL;
        }
    }

    esp_http_client_cleanup(client);
    return err;
}

static esp_err_t http_put_no_body(const char *url)
{
    if (!url) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_PUT,
        .timeout_ms = 5000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        return ESP_FAIL;
    }

    esp_err_t err = esp_http_client_perform(client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP PUT failed: %s", esp_err_to_name(err));
    } else {
        int status = esp_http_client_get_status_code(client);
        if (status != 200) {
            ESP_LOGE(TAG, "HTTP PUT status %d", status);
            err = ESP_FAIL;
        }
    }

    esp_http_client_cleanup(client);
    return err;
}

esp_err_t http_fetch_alarms(alarm_list_t *out_list)
{
    if (!out_list) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(out_list, 0, sizeof(alarm_list_t));

    if (!wifi_wait_connected(5000)) {
        ESP_LOGW(TAG, "Wi-Fi not connected, skip alarm fetch");
        return ESP_ERR_INVALID_STATE;
    }

    char url[128];
    snprintf(url, sizeof(url), "http://%s:%u/api/alarms/list/%s", s_alarm_host, (unsigned)s_alarm_port, s_alarm_user);

    http_resp_buffer_t resp = {
        .cap = 2048,
        .len = 0,
        .data = (char *)calloc(1, 2048)
    };

    if (!resp.data) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = http_fetch_raw(url, &resp);
    if (err != ESP_OK) {
        free(resp.data);
        return err;
    }

    cJSON *root = cJSON_Parse(resp.data);
    if (!root) {
        ESP_LOGE(TAG, "Failed to parse alarm JSON");
        free(resp.data);
        return ESP_FAIL;
    }

    cJSON *data = cJSON_GetObjectItem(root, "data");
    cJSON *alarms = data ? cJSON_GetObjectItem(data, "alarms") : NULL;
    if (!cJSON_IsArray(alarms)) {
        ESP_LOGE(TAG, "Alarms field missing or invalid");
        cJSON_Delete(root);
        free(resp.data);
        return ESP_FAIL;
    }

    time_t now_ts = time(NULL);
    struct tm now_tm = {0};
    localtime_r(&now_ts, &now_tm);

    size_t idx = 0;
    cJSON *item = NULL;
    cJSON_ArrayForEach(item, alarms) {
        if (idx >= ALARM_MAX_COUNT) {
            ESP_LOGW(TAG, "Alarm list truncated to %d", ALARM_MAX_COUNT);
            break;
        }
        alarm_info_t *dst = &out_list->items[idx];
        memset(dst, 0, sizeof(alarm_info_t));

        cJSON *id = cJSON_GetObjectItem(item, "id");
        cJSON *type = cJSON_GetObjectItem(item, "type");
        cJSON *alarm_time = cJSON_GetObjectItem(item, "alarmTime");
        cJSON *target_date = cJSON_GetObjectItem(item, "targetDate");
        cJSON *repeat_days = cJSON_GetObjectItem(item, "repeatDays");
        cJSON *status = cJSON_GetObjectItem(item, "status");

        dst->id = cJSON_IsNumber(id) ? (int)id->valuedouble : 0;
        dst->type = cJSON_IsNumber(type) ? (alarm_type_t)((int)type->valuedouble) : ALARM_TYPE_ONCE;
        dst->status = cJSON_IsNumber(status) ? (int)status->valuedouble : 1;

        if (cJSON_IsString(alarm_time) && alarm_time->valuestring) {
            snprintf(dst->alarm_time, sizeof(dst->alarm_time), "%s", alarm_time->valuestring);
        }

        if (cJSON_IsString(target_date) && target_date->valuestring) {
            snprintf(dst->target_date, sizeof(dst->target_date), "%s", target_date->valuestring);
        }

        dst->repeat_mask = parse_repeat_mask(repeat_days, dst->repeat_days, sizeof(dst->repeat_days));

        if (time_is_valid(now_ts)) {
            dst->next_trigger = alarm_compute_next_trigger(dst, &now_tm);
        } else {
            dst->next_trigger = 0;
        }

        idx++;
    }

    out_list->count = idx;

    cJSON_Delete(root);
    free(resp.data);
    return ESP_OK;
}

esp_err_t http_update_alarm_status(int alarm_id, int status)
{
    if (alarm_id <= 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!wifi_wait_connected(5000)) {
        ESP_LOGW(TAG, "Wi-Fi not connected, skip alarm status update");
        return ESP_ERR_INVALID_STATE;
    }

    char url[160];
    snprintf(url, sizeof(url), "http://%s:%u/api/alarms/%d/status?userId=%s&status=%d",
             s_alarm_host, (unsigned)s_alarm_port, alarm_id, s_alarm_user, status);

    return http_put_no_body(url);
}

static void alarm_fetch_task_fn(void *arg)
{
    alarm_list_t *latest = (alarm_list_t *)calloc(1, sizeof(alarm_list_t));
    if (!latest) {
        ESP_LOGE(TAG, "alarm fetch alloc failed");
        vTaskDelete(NULL);
        return;
    }
    while (1) {
        if (http_fetch_alarms(latest) == ESP_OK) {
            if (!s_alarm_mutex) {
                s_alarm_mutex = xSemaphoreCreateMutex();
            }
            if (s_alarm_mutex && xSemaphoreTake(s_alarm_mutex, pdMS_TO_TICKS(2000)) == pdTRUE) {
                s_alarm_list = *latest;
                log_alarm_snapshot(&s_alarm_list);
                xSemaphoreGive(s_alarm_mutex);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(s_alarm_fetch_period_ms));
    }
    free(latest);
}

static void alarm_monitor_task_fn(void *arg)
{
    while (1) {
        time_t now_ts = time(NULL);
        if (!time_is_valid(now_ts)) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        struct tm now_tm;
        localtime_r(&now_ts, &now_tm);

        if (s_alarm_mutex && xSemaphoreTake(s_alarm_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
            for (size_t i = 0; i < s_alarm_list.count; ++i) {
                alarm_info_t *alarm = &s_alarm_list.items[i];
                if (alarm->status != 1) {
                    continue;
                }

                if (alarm->next_trigger == 0) {
                    alarm->next_trigger = alarm_compute_next_trigger(alarm, &now_tm);
                }

                if (alarm_is_due(alarm, &now_tm)) {
                    if (s_alarm_cb) {
                        s_alarm_cb(alarm, s_alarm_cb_ctx);
                    } else {
                        ESP_LOGI(TAG, "Alarm %d due at %s %s", alarm->id,
                                 (alarm->target_date[0] != '\0') ? alarm->target_date : "repeat",
                                 alarm->alarm_time);
                    }

                    if (alarm->type == ALARM_TYPE_ONCE) {
                        alarm->next_trigger = 0;
                        if (http_update_alarm_status(alarm->id, 0) == ESP_OK) {
                            alarm->status = 0;
                        } else {
                            ESP_LOGW(TAG, "Failed to update alarm %d status to 0", alarm->id);
                        }
                    } else {
                        /* Skip to next minute to avoid repeated triggers within same minute */
                        time_t next_base_ts = now_ts + 60;
                        struct tm next_base;
                        localtime_r(&next_base_ts, &next_base);
                        alarm->next_trigger = alarm_compute_next_trigger(alarm, &next_base);
                    }
                }
            }
            xSemaphoreGive(s_alarm_mutex);
        }

        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

esp_err_t alarm_service_start(uint32_t fetch_interval_ms, alarm_trigger_cb_t cb, void *cb_ctx)
{
    if (fetch_interval_ms >= 5000) {
        s_alarm_fetch_period_ms = fetch_interval_ms;
    }

    if (!s_alarm_mutex) {
        s_alarm_mutex = xSemaphoreCreateMutex();
        if (!s_alarm_mutex) {
            return ESP_ERR_NO_MEM;
        }
    }

    s_alarm_cb = cb;
    s_alarm_cb_ctx = cb_ctx;

    if (!s_alarm_fetch_task) {
        BaseType_t r = xTaskCreate(alarm_fetch_task_fn, "alarm_fetch", ALARM_TASK_STACK, NULL, ALARM_TASK_PRIO, &s_alarm_fetch_task);
        if (r != pdPASS) {
            s_alarm_fetch_task = NULL;
            return ESP_FAIL;
        }
    }

    if (!s_alarm_monitor_task) {
        BaseType_t r = xTaskCreate(alarm_monitor_task_fn, "alarm_monitor", ALARM_TASK_STACK, NULL, ALARM_TASK_PRIO, &s_alarm_monitor_task);
        if (r != pdPASS) {
            s_alarm_monitor_task = NULL;
            return ESP_FAIL;
        }
    }

    return ESP_OK;
}
