#pragma once

#include "esp_err.h"

/* 启动业务控制任务（上传、睡眠分析、UART解析） */
esp_err_t app_controller_start(void);
