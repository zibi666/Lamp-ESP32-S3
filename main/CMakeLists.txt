# Define source files
set(SOURCES 
            # Voice (语音功能)
            "voice/audio_codec.cc"
            "voice/audio_service.cc"
            "voice/driver/es8388_audio_codec.cc"
            "voice/processors/audio_debugger.cc"
            # Network (网络传输)
            "network/protocol.cc"
            "network/websocket_protocol.cc"
            "network/audio_uploader.c"
            "network/audio_afe_ws_sender.cc"
            # PWM
            "pwm/pwm_test.cc"
            "pwm/backlight.cc"
            # BSP - 睡眠监测相关功能 (来自 other_projects)
            "bsp/UART/uart.c"
            "bsp/radar_protocol/radar_protocol.c"
            "bsp/HTTP/http_request.c"
            "bsp/SleepAnalysis/sleep_analysis.cpp"
            # 智能灯光控制
            "bsp/SmartLight/smart_light_controller.cpp"
            "bsp/SmartLight/smart_light_backlight_bridge.cc"
            # SD卡音频播放功能
            "bsp/sd_audio/audio_hw.c"
            "bsp/sd_audio/audio_player.c"
            "bsp/sd_audio/audio_sdcard.c"
            "bsp/Input/xl9555_keys.c"
            "bsp/App/app_controller.c"
            "bsp/RTC/rtc_service.c"
            "bsp/AlarmMusic/alarm_music.c"
            # Core
            "system_info.cc"
            "application.cc"
            "settings.cc"
            "device_state_event.cc"
            "assets.cc"
            "main.cc"
            )

set(INCLUDE_DIRS "." "voice" "voice/driver" "voice/processors" "network" "pwm"
                 "bsp/UART" "bsp/radar_protocol" "bsp/HTTP" "bsp/SleepAnalysis" "bsp/SmartLight"
                 "bsp/sd_audio" "bsp/Input" "bsp/App" "bsp/RTC" "bsp/AlarmMusic")

# Add board common files
file(GLOB BOARD_COMMON_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/boards/common/*.cc ${CMAKE_CURRENT_SOURCE_DIR}/boards/common/*.c)
list(APPEND SOURCES ${BOARD_COMMON_SOURCES})
list(APPEND INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/boards/common)

idf_build_get_property(build_components BUILD_COMPONENTS)
# Function to find component dynamically by pattern
function(find_component_by_pattern PATTERN COMPONENT_VAR PATH_VAR)
    foreach(COMPONENT ${build_components})
        if(COMPONENT MATCHES "${PATTERN}")
            set(${COMPONENT_VAR} ${COMPONENT} PARENT_SCOPE)
            idf_component_get_property(COMPONENT_PATH ${COMPONENT} COMPONENT_DIR)
            set(${PATH_VAR} "${COMPONENT_PATH}" PARENT_SCOPE)
            break()
        endif()
    endforeach()
endfunction()

# Set default BUILTIN_TEXT_FONT and BUILTIN_ICON_FONT
set(BUILTIN_TEXT_FONT font_puhui_14_1)
set(BUILTIN_ICON_FONT font_awesome_14_1)

# Add board files according to BOARD_TYPE
# Set default assets if the board uses partition table V2
if(CONFIG_BOARD_TYPE_BREAD_COMPACT_WIFI)
    set(BOARD_TYPE "bread-compact-wifi")
    set(BUILTIN_TEXT_FONT font_puhui_basic_14_1)
    set(BUILTIN_ICON_FONT font_awesome_14_1)
elseif(CONFIG_BOARD_TYPE_BREAD_COMPACT_ML307)
    set(BOARD_TYPE "bread-compact-ml307")
    set(BUILTIN_TEXT_FONT font_puhui_basic_14_1)
    set(BUILTIN_ICON_FONT font_awesome_14_1)
elseif(CONFIG_BOARD_TYPE_BREAD_COMPACT_ESP32)
    set(BOARD_TYPE "bread-compact-esp32")
elseif(CONFIG_BOARD_TYPE_BREAD_COMPACT_ESP32_LCD)
    set(BOARD_TYPE "bread-compact-esp32-lcd")
    set(BUILTIN_TEXT_FONT font_puhui_basic_14_1)
    set(BUILTIN_ICON_FONT font_awesome_14_1)
elseif(CONFIG_BOARD_TYPE_DF_K10)
    set(BOARD_TYPE "df-k10")
    set(BUILTIN_TEXT_FONT font_puhui_basic_20_4)
    set(BUILTIN_ICON_FONT font_awesome_20_4)
    set(DEFAULT_EMOJI_COLLECTION twemoji_64)
elseif(CONFIG_BOARD_TYPE_DF_S3_AI_CAM)
    set(BOARD_TYPE "df-s3-ai-cam")
elseif(CONFIG_BOARD_TYPE_ESP_BOX_3)
    set(BOARD_TYPE "esp-box-3")
    set(BUILTIN_TEXT_FONT font_puhui_basic_20_4)
    set(BUILTIN_ICON_FONT font_awesome_20_4)
    set(DEFAULT_EMOJI_COLLECTION twemoji_64)
elseif(CONFIG_BOARD_TYPE_ESP_BOX)
    set(BOARD_TYPE "esp-box")
    set(BUILTIN_TEXT_FONT font_puhui_basic_20_4)
    set(BUILTIN_ICON_FONT font_awesome_20_4)
    set(DEFAULT_EMOJI_COLLECTION twemoji_64)
elseif(CONFIG_BOARD_TYPE_ESP_BOX_LITE)
    set(BOARD_TYPE "esp-box-lite")
    set(BUILTIN_TEXT_FONT font_puhui_basic_20_4)
    set(BUILTIN_ICON_FONT font_awesome_20_4)
    set(DEFAULT_EMOJI_COLLECTION twemoji_64)
elseif(CONFIG_BOARD_TYPE_KEVIN_BOX_1)
    set(BOARD_TYPE "kevin-box-1")
    set(BUILTIN_TEXT_FONT font_puhui_basic_14_1)
    set(BUILTIN_ICON_FONT font_awesome_14_1)
elseif(CONFIG_BOARD_TYPE_KEVIN_BOX_2)
    set(BOARD_TYPE "kevin-box-2")
    set(BUILTIN_TEXT_FONT font_puhui_basic_14_1)
    set(BUILTIN_ICON_FONT font_awesome_14_1)
elseif(CONFIG_BOARD_TYPE_KEVIN_C3)
    set(BOARD_TYPE "kevin-c3")
elseif(CONFIG_BOARD_TYPE_KEVIN_SP_V3_DEV)
    set(BOARD_TYPE "kevin-sp-v3-dev")
    set(BUILTIN_TEXT_FONT font_puhui_basic_20_4)
    set(BUILTIN_ICON_FONT font_awesome_20_4)
    set(DEFAULT_EMOJI_COLLECTION twemoji_64)
elseif(CONFIG_BOARD_TYPE_KEVIN_SP_V4_DEV)
    set(BOARD_TYPE "kevin-sp-v4-dev")
    set(BUILTIN_TEXT_FONT font_puhui_basic_20_4)
    set(BUILTIN_ICON_FONT font_awesome_20_4)
    set(DEFAULT_EMOJI_COLLECTION twemoji_64)
elseif(CONFIG_BOARD_TYPE_KEVIN_YUYING_313LCD)
    set(BOARD_TYPE "kevin-yuying-313lcd")
    set(BUILTIN_TEXT_FONT font_puhui_basic_30_4)
    set(BUILTIN_ICON_FONT font_awesome_30_4)
    set(DEFAULT_EMOJI_COLLECTION twemoji_64)
elseif(CONFIG_BOARD_TYPE_LICHUANG_DEV)
    set(BOARD_TYPE "lichuang-dev")
    set(BUILTIN_TEXT_FONT font_puhui_basic_20_4)
    set(BUILTIN_ICON_FONT font_awesome_20_4)
    set(DEFAULT_EMOJI_COLLECTION twemoji_64)
elseif(CONFIG_BOARD_TYPE_LICHUANG_C3_DEV)
    set(BOARD_TYPE "lichuang-c3-dev")
    set(BUILTIN_TEXT_FONT font_puhui_basic_20_4)
    set(BUILTIN_ICON_FONT font_awesome_20_4)
    set(DEFAULT_EMOJI_COLLECTION twemoji_32)
elseif(CONFIG_BOARD_TYPE_MAGICLICK_2P4)
    set(BOARD_TYPE "magiclick-2p4")
    set(BUILTIN_TEXT_FONT font_puhui_basic_16_4)
    set(BUILTIN_ICON_FONT font_awesome_16_4)
    set(DEFAULT_EMOJI_COLLECTION twemoji_32)
elseif(CONFIG_BOARD_TYPE_MAGICLICK_2P5)
    set(BOARD_TYPE "magiclick-2p5")
    set(BUILTIN_TEXT_FONT font_puhui_basic_16_4)
    set(BUILTIN_ICON_FONT font_awesome_16_4)
    set(DEFAULT_EMOJI_COLLECTION twemoji_32)
elseif(CONFIG_BOARD_TYPE_MAGICLICK_C3)
    set(BOARD_TYPE "magiclick-c3")
    set(BUILTIN_TEXT_FONT font_puhui_basic_16_4)
    set(BUILTIN_ICON_FONT font_awesome_16_4)
    set(DEFAULT_EMOJI_COLLECTION twemoji_32)
elseif(CONFIG_BOARD_TYPE_MAGICLICK_C3_V2)
    set(BOARD_TYPE "magiclick-c3-v2")
    set(BUILTIN_TEXT_FONT font_puhui_basic_16_4)
    set(BUILTIN_ICON_FONT font_awesome_16_4)
    set(DEFAULT_EMOJI_COLLECTION twemoji_32)
elseif(CONFIG_BOARD_TYPE_M5STACK_CORE_S3)
    set(BOARD_TYPE "m5stack-core-s3")
    set(BUILTIN_TEXT_FONT font_puhui_basic_20_4)
    set(BUILTIN_ICON_FONT font_awesome_20_4)
    set(DEFAULT_EMOJI_COLLECTION twemoji_64)
elseif(CONFIG_BOARD_TYPE_M5STACK_CORE_TAB5)
    set(BOARD_TYPE "m5stack-tab5")
    set(BUILTIN_TEXT_FONT font_puhui_basic_30_4)
    set(BUILTIN_ICON_FONT font_awesome_30_4)
    set(DEFAULT_EMOJI_COLLECTION twemoji_64)
elseif(CONFIG_BOARD_TYPE_ATOMS3_ECHO_BASE)
    set(BOARD_TYPE "atoms3-echo-base")
    set(BUILTIN_TEXT_FONT font_puhui_basic_16_4)
    set(BUILTIN_ICON_FONT font_awesome_16_4)
    set(DEFAULT_EMOJI_COLLECTION twemoji_32)
elseif(CONFIG_BOARD_TYPE_ATOMS3R_ECHO_BASE)
    set(BOARD_TYPE "atoms3r-echo-base")
    set(BUILTIN_TEXT_FONT font_puhui_basic_16_4)
    set(BUILTIN_ICON_FONT font_awesome_16_4)
    set(DEFAULT_EMOJI_COLLECTION twemoji_32)
elseif(CONFIG_BOARD_TYPE_ATOMS3R_CAM_M12_ECHO_BASE)
    set(BOARD_TYPE "atoms3r-cam-m12-echo-base")
elseif(CONFIG_BOARD_TYPE_ATOM_ECHOS3R)
    set(BOARD_TYPE "atom-echos3r")
elseif(CONFIG_BOARD_TYPE_ATOMMATRIX_ECHO_BASE)
    set(BOARD_TYPE "atommatrix-echo-base")
elseif(CONFIG_BOARD_TYPE_XMINI_C3_V3)
    set(BOARD_TYPE "xmini-c3-v3")
    set(BUILTIN_TEXT_FONT font_puhui_basic_14_1)
    set(BUILTIN_ICON_FONT font_awesome_14_1)
elseif(CONFIG_BOARD_TYPE_XMINI_C3_4G)
    set(BOARD_TYPE "xmini-c3-4g")
    set(BUILTIN_TEXT_FONT font_puhui_basic_14_1)
    set(BUILTIN_ICON_FONT font_awesome_14_1)
elseif(CONFIG_BOARD_TYPE_XMINI_C3)
    set(BOARD_TYPE "xmini-c3")
    set(BUILTIN_TEXT_FONT font_puhui_basic_14_1)
    set(BUILTIN_ICON_FONT font_awesome_14_1)
elseif(CONFIG_BOARD_TYPE_ESP32S3_KORVO2_V3)
    set(BOARD_TYPE "esp32s3-korvo2-v3")
    set(BUILTIN_TEXT_FONT font_puhui_basic_20_4)
    set(BUILTIN_ICON_FONT font_awesome_20_4)
    set(DEFAULT_EMOJI_COLLECTION twemoji_64)
elseif(CONFIG_BOARD_TYPE_ESP_SPARKBOT)
    set(BOARD_TYPE "esp-sparkbot")
    set(BUILTIN_TEXT_FONT font_puhui_basic_20_4)
    set(BUILTIN_ICON_FONT font_awesome_20_4)
    set(DEFAULT_EMOJI_COLLECTION twemoji_64)
elseif(CONFIG_BOARD_TYPE_ESP_SPOT_S3)
    set(BOARD_TYPE "esp-spot-s3")
elseif(CONFIG_BOARD_TYPE_ESP_HI)
    set(BOARD_TYPE "esp-hi")
    # Set ESP_HI emoji directory for DEFAULT_ASSETS_EXTRA_FILES
    set(DEFAULT_ASSETS_EXTRA_FILES "${CMAKE_BINARY_DIR}/emoji")
elseif(CONFIG_BOARD_TYPE_ECHOEAR)
    set(BOARD_TYPE "echoear")
    set(BUILTIN_TEXT_FONT font_puhui_20_4)
    set(BUILTIN_ICON_FONT font_awesome_20_4)
    set(DEFAULT_EMOJI_COLLECTION twemoji_64)
elseif(CONFIG_BOARD_TYPE_ESP32S3_AUDIO_BOARD)
    set(BOARD_TYPE "waveshare-s3-audio-board")
    set(BUILTIN_TEXT_FONT font_puhui_basic_16_4)
    set(BUILTIN_ICON_FONT font_awesome_16_4)
    set(DEFAULT_EMOJI_COLLECTION twemoji_32)
elseif(CONFIG_BOARD_TYPE_ESP32S3_Touch_AMOLED_1_8)
    set(BOARD_TYPE "esp32-s3-touch-amoled-1.8")
    set(BUILTIN_TEXT_FONT font_puhui_basic_30_4)
    set(BUILTIN_ICON_FONT font_awesome_30_4)
    set(DEFAULT_EMOJI_COLLECTION twemoji_64)
elseif(CONFIG_BOARD_TYPE_ESP32S3_Touch_AMOLED_2_06)
    set(BOARD_TYPE "waveshare-s3-touch-amoled-2.06")
    set(BUILTIN_TEXT_FONT font_puhui_basic_30_4)
    set(BUILTIN_ICON_FONT font_awesome_30_4)
    set(DEFAULT_EMOJI_COLLECTION twemoji_64)
elseif(CONFIG_BOARD_TYPE_ESP32S3_Touch_LCD_4B)
    set(BOARD_TYPE "waveshare-s3-touch-lcd-4b")
    set(BUILTIN_TEXT_FONT font_puhui_basic_30_4)
    set(BUILTIN_ICON_FONT font_awesome_30_4)
    set(DEFAULT_EMOJI_COLLECTION twemoji_64)
elseif(CONFIG_BOARD_TYPE_ESP32S3_Touch_AMOLED_1_75)
    set(BOARD_TYPE "waveshare-s3-touch-amoled-1.75")
    set(BUILTIN_TEXT_FONT font_puhui_basic_30_4)
    set(BUILTIN_ICON_FONT font_awesome_30_4)
    set(DEFAULT_EMOJI_COLLECTION twemoji_64)
elseif(CONFIG_BOARD_TYPE_ESP32S3_Touch_LCD_1_85C)
    set(BOARD_TYPE "esp32-s3-touch-lcd-1.85c")
    set(BUILTIN_TEXT_FONT font_puhui_basic_16_4)
    set(BUILTIN_ICON_FONT font_awesome_16_4)
    set(DEFAULT_EMOJI_COLLECTION twemoji_64)
elseif(CONFIG_BOARD_TYPE_ESP32S3_Touch_LCD_1_85)
    set(BOARD_TYPE "esp32-s3-touch-lcd-1.85")
    set(BUILTIN_TEXT_FONT font_puhui_basic_16_4)
    set(BUILTIN_ICON_FONT font_awesome_16_4)
    set(DEFAULT_EMOJI_COLLECTION twemoji_64)
elseif(CONFIG_BOARD_TYPE_ESP32S3_Touch_LCD_1_46)
    set(BOARD_TYPE "esp32-s3-touch-lcd-1.46")
    set(BUILTIN_TEXT_FONT font_puhui_basic_16_4)
    set(BUILTIN_ICON_FONT font_awesome_16_4)
    set(DEFAULT_EMOJI_COLLECTION twemoji_64)
elseif(CONFIG_BOARD_TYPE_ESP32S3_Touch_LCD_3_5)
    set(BOARD_TYPE "esp32-s3-touch-lcd-3.5")
    set(BUILTIN_TEXT_FONT font_puhui_basic_20_4)
    set(BUILTIN_ICON_FONT font_awesome_20_4)
    set(DEFAULT_EMOJI_COLLECTION twemoji_64)
elseif(CONFIG_BOARD_TYPE_ESP32S3_Touch_LCD_3_5B)
    set(BOARD_TYPE "waveshare-s3-touch-lcd-3.5b")
    set(BUILTIN_TEXT_FONT font_puhui_basic_16_4)
    set(BUILTIN_ICON_FONT font_awesome_16_4)
    set(DEFAULT_EMOJI_COLLECTION twemoji_32)
elseif(CONFIG_BOARD_TYPE_ESP32S3_Touch_LCD_3_49)
    set(BOARD_TYPE "waveshare-s3-touch-lcd-3.49")
    set(LVGL_TEXT_FONT font_puhui_basic_30_4)
    set(LVGL_ICON_FONT font_awesome_30_4)
    set(DEFAULT_EMOJI_COLLECTION twemoji_64)
elseif(CONFIG_BOARD_TYPE_ESP32C6_LCD_1_69)
    set(BOARD_TYPE "waveshare-c6-lcd-1.69")
    set(BUILTIN_TEXT_FONT font_puhui_basic_20_4)
    set(BUILTIN_ICON_FONT font_awesome_20_4)
    set(DEFAULT_EMOJI_COLLECTION twemoji_64)
elseif(CONFIG_BOARD_TYPE_ESP32C6_Touch_AMOLED_1_43)
    set(BOARD_TYPE "waveshare-c6-touch-amoled-1.43")
    set(BUILTIN_TEXT_FONT font_puhui_basic_30_4)
    set(BUILTIN_ICON_FONT font_awesome_30_4)
    set(DEFAULT_EMOJI_COLLECTION twemoji_64)
elseif(CONFIG_BOARD_TYPE_ESP32P4_NANO)
    set(BOARD_TYPE "waveshare-p4-nano")
    set(BUILTIN_TEXT_FONT font_puhui_basic_30_4)
    set(BUILTIN_ICON_FONT font_awesome_30_4)
    set(DEFAULT_EMOJI_COLLECTION twemoji_64)
elseif(CONFIG_BOARD_TYPE_ESP32P4_WIFI6_Touch_LCD_4B)
    set(BOARD_TYPE "waveshare-p4-wifi6-touch-lcd-4b")
    set(BUILTIN_TEXT_FONT font_puhui_basic_30_4)
    set(BUILTIN_ICON_FONT font_awesome_30_4)
    set(DEFAULT_EMOJI_COLLECTION twemoji_64)
elseif(CONFIG_BOARD_TYPE_ESP32P4_WIFI6_Touch_LCD_XC)
    set(BOARD_TYPE "waveshare-p4-wifi6-touch-lcd-xc")
    set(BUILTIN_TEXT_FONT font_puhui_basic_30_4)
    set(BUILTIN_ICON_FONT font_awesome_30_4)
    set(DEFAULT_EMOJI_COLLECTION twemoji_64)
elseif(CONFIG_BOARD_TYPE_BREAD_COMPACT_WIFI_LCD)
    set(BOARD_TYPE "bread-compact-wifi-lcd")
    set(BUILTIN_TEXT_FONT font_puhui_basic_16_4)
    set(BUILTIN_ICON_FONT font_awesome_16_4)
    set(DEFAULT_EMOJI_COLLECTION twemoji_32)
elseif(CONFIG_BOARD_TYPE_TUDOUZI)
    set(BOARD_TYPE "tudouzi")
    set(BUILTIN_TEXT_FONT font_puhui_basic_14_1)
    set(BUILTIN_ICON_FONT font_awesome_14_1)
elseif(CONFIG_BOARD_TYPE_LILYGO_T_CIRCLE_S3)
    set(BOARD_TYPE "lilygo-t-circle-s3")
    set(BUILTIN_TEXT_FONT font_puhui_basic_16_4)
    set(BUILTIN_ICON_FONT font_awesome_16_4)
    set(DEFAULT_EMOJI_COLLECTION twemoji_32)
elseif(CONFIG_BOARD_TYPE_LILYGO_T_CAMERAPLUS_S3_V1_0_V1_1)
    set(BOARD_TYPE "lilygo-t-cameraplus-s3")
    set(BUILTIN_TEXT_FONT font_puhui_basic_20_4)
    set(BUILTIN_ICON_FONT font_awesome_20_4)
    set(DEFAULT_EMOJI_COLLECTION twemoji_64)
elseif(CONFIG_BOARD_TYPE_LILYGO_T_DISPLAY_S3_PRO_MVSRLORA)
    set(BOARD_TYPE "lilygo-t-display-s3-pro-mvsrlora")
    set(BUILTIN_TEXT_FONT font_puhui_basic_20_4)
    set(BUILTIN_ICON_FONT font_awesome_20_4)
    set(DEFAULT_EMOJI_COLLECTION twemoji_64)
elseif(CONFIG_BOARD_TYPE_MOVECALL_MOJI_ESP32S3)
    set(BOARD_TYPE "movecall-moji-esp32s3")
    set(BUILTIN_TEXT_FONT font_puhui_basic_20_4)
    set(BUILTIN_ICON_FONT font_awesome_20_4)
    set(DEFAULT_EMOJI_COLLECTION twemoji_64)
elseif(CONFIG_BOARD_TYPE_MOVECALL_CUICAN_ESP32S3)
    set(BOARD_TYPE "movecall-cuican-esp32s3")
    set(BUILTIN_TEXT_FONT font_puhui_basic_20_4)
    set(BUILTIN_ICON_FONT font_awesome_20_4)
    set(DEFAULT_EMOJI_COLLECTION twemoji_64)
elseif(CONFIG_BOARD_TYPE_ATK_DNESP32S3)
    set(BOARD_TYPE "atk-dnesp32s3")
    set(BUILTIN_TEXT_FONT font_puhui_basic_20_4)
    set(BUILTIN_ICON_FONT font_awesome_20_4)
    set(DEFAULT_EMOJI_COLLECTION twemoji_64)
elseif(CONFIG_BOARD_TYPE_DU_CHATX)
    set(BOARD_TYPE "du-chatx")
    set(BUILTIN_TEXT_FONT font_puhui_basic_16_4)
    set(BUILTIN_ICON_FONT font_awesome_16_4)
    set(DEFAULT_EMOJI_COLLECTION twemoji_32)
elseif(CONFIG_BOARD_TYPE_ESP32_S3_1_54_MUMA)
    set(BOARD_TYPE "sp-esp32-s3-1.54-muma")
    set(BUILTIN_TEXT_FONT font_puhui_basic_16_4)
    set(BUILTIN_ICON_FONT font_awesome_16_4)
    set(DEFAULT_EMOJI_COLLECTION twemoji_32)
elseif(CONFIG_BOARD_TYPE_ESP32_S3_1_28_BOX)
    set(BOARD_TYPE "sp-esp32-s3-1.28-box")
    set(BUILTIN_TEXT_FONT font_puhui_basic_16_4)
    set(BUILTIN_ICON_FONT font_awesome_16_4)
    set(DEFAULT_EMOJI_COLLECTION twemoji_64)
elseif(CONFIG_BOARD_TYPE_OTTO_ROBOT)
    set(BOARD_TYPE "otto-robot")
    set(BUILTIN_TEXT_FONT font_puhui_16_4)
    set(BUILTIN_ICON_FONT font_awesome_16_4)
endif()

file(GLOB BOARD_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/boards/${BOARD_TYPE}/*.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/boards/${BOARD_TYPE}/*.c
)
list(APPEND SOURCES ${BOARD_SOURCES})

# Select audio processor according to Kconfig
if(CONFIG_USE_AUDIO_PROCESSOR)
    list(APPEND SOURCES "voice/processors/afe_audio_processor.cc")
else()
    list(APPEND SOURCES "voice/processors/no_audio_processor.cc")
endif()

# Only zh-CN language is supported now
set(LANG_DIR "zh-CN")

# Define language paths
set(LANG_HEADER "${CMAKE_CURRENT_SOURCE_DIR}/assets/lang_config.h")
file(GLOB LANG_SOUNDS ${CMAKE_CURRENT_SOURCE_DIR}/assets/locales/${LANG_DIR}/*.ogg)
file(GLOB COMMON_SOUNDS ${CMAKE_CURRENT_SOURCE_DIR}/assets/common/*.ogg)

# If target chip is ESP32, exclude specific files to avoid build errors
if(CONFIG_IDF_TARGET_ESP32)
    list(REMOVE_ITEM SOURCES "audio/codecs/box_audio_codec.cc"
                             "audio/codecs/es8388_audio_codec.cc"
                             "audio/codecs/es8389_audio_codec.cc"
                             "led/gpio_led.cc"
                             )
endif()

idf_component_register(SRCS ${SOURCES}
                    EMBED_FILES ${LANG_SOUNDS} ${COMMON_SOUNDS}
                    INCLUDE_DIRS ${INCLUDE_DIRS}
                    WHOLE_ARCHIVE
                    )

# Use target_compile_definitions to define BOARD_TYPE, BOARD_NAME
# If BOARD_NAME is empty, use BOARD_TYPE
if(NOT BOARD_NAME)
    set(BOARD_NAME ${BOARD_TYPE})
endif()
target_compile_definitions(${COMPONENT_LIB}
                    PRIVATE BOARD_TYPE=\"${BOARD_TYPE}\" BOARD_NAME=\"${BOARD_NAME}\"
                    PRIVATE BUILTIN_TEXT_FONT=${BUILTIN_TEXT_FONT} BUILTIN_ICON_FONT=${BUILTIN_ICON_FONT}
                    )

# Language config is now pre-generated (zh-CN only)
# No need to run gen_lang.py script anymore

# Find xiaozhi-fonts component dynamically
find_component_by_pattern("xiaozhi-fonts" XIAOZHI_FONTS_COMPONENT XIAOZHI_FONTS_COMPONENT_PATH)
if(XIAOZHI_FONTS_COMPONENT_PATH)
    set(XIAOZHI_FONTS_PATH "${XIAOZHI_FONTS_COMPONENT_PATH}")
endif()

if(CONFIG_BOARD_TYPE_ESP_HI)
set(URL "https://github.com/espressif2022/image_player/raw/main/test_apps/test_8bit")
set(EMOJI_DIR "${CMAKE_BINARY_DIR}/emoji")
file(MAKE_DIRECTORY ${EMOJI_DIR})

# List all files to download
set(FILES_TO_DOWNLOAD "")
list(APPEND FILES_TO_DOWNLOAD "Anger_enter.aaf" "Anger_loop.aaf" "Anger_return.aaf")
list(APPEND FILES_TO_DOWNLOAD "happy_enter.aaf" "happy_loop.aaf" "happ_return.aaf")
list(APPEND FILES_TO_DOWNLOAD "sad_enter.aaf" "sad_loop.aaf" "sad_return.aaf")
list(APPEND FILES_TO_DOWNLOAD "scorn_enter.aaf" "scorn_loop.aaf" "scorn_return.aaf")
list(APPEND FILES_TO_DOWNLOAD "left_enter.aaf" "left_loop.aaf" "left_return.aaf")
list(APPEND FILES_TO_DOWNLOAD "right_enter.aaf" "right_loop.aaf" "right_return.aaf")
list(APPEND FILES_TO_DOWNLOAD "asking.aaf" "blink_once.aaf" "blink_quick.aaf")
list(APPEND FILES_TO_DOWNLOAD "connecting.aaf" "panic_enter.aaf" "panic_loop.aaf")
list(APPEND FILES_TO_DOWNLOAD "panic_return.aaf" "wake.aaf")

foreach(FILENAME IN LISTS FILES_TO_DOWNLOAD)
    set(REMOTE_FILE "${URL}/${FILENAME}")
    set(LOCAL_FILE "${EMOJI_DIR}/${FILENAME}")
    
    # Check if local file exists
    if(EXISTS ${LOCAL_FILE})
        message(STATUS "File ${FILENAME} already exists, skipping download")
    else()
        message(STATUS "Downloading ${FILENAME}")
        file(DOWNLOAD ${REMOTE_FILE} ${LOCAL_FILE}
             STATUS DOWNLOAD_STATUS)
        list(GET DOWNLOAD_STATUS 0 STATUS_CODE)
        if(NOT STATUS_CODE EQUAL 0)
            message(FATAL_ERROR "Failed to download ${FILENAME} from ${URL}")
        endif()
    endif()
endforeach()

endif()


# Function to build default assets based on configuration
function(build_default_assets_bin)
    # Set output path for generated assets.bin
    set(GENERATED_ASSETS_BIN "${CMAKE_BINARY_DIR}/generated_assets.bin")
    
    # Prepare arguments for build script
    set(BUILD_ARGS
        "--sdkconfig" "${SDKCONFIG}"
        "--output" "${GENERATED_ASSETS_BIN}"
    )
    
    # Add builtin text font if defined
    if(BUILTIN_TEXT_FONT)
        list(APPEND BUILD_ARGS "--builtin_text_font" "${BUILTIN_TEXT_FONT}")
    endif()
    
    # Add default emoji collection if defined
    if(DEFAULT_EMOJI_COLLECTION)
        list(APPEND BUILD_ARGS "--emoji_collection" "${DEFAULT_EMOJI_COLLECTION}")
    endif()
    
    # Add default assets extra files if defined
    if(DEFAULT_ASSETS_EXTRA_FILES)
        list(APPEND BUILD_ARGS "--extra_files" "${DEFAULT_ASSETS_EXTRA_FILES}")
    endif()
    
    if(XIAOZHI_FONTS_PATH)
        list(APPEND BUILD_ARGS "--xiaozhi_fonts_path" "${XIAOZHI_FONTS_PATH}")
    endif()
    
    # Create custom command to build assets
    add_custom_command(
        OUTPUT ${GENERATED_ASSETS_BIN}
        COMMAND python ${PROJECT_DIR}/scripts/build_default_assets.py ${BUILD_ARGS}
        DEPENDS
            ${SDKCONFIG}
            ${PROJECT_DIR}/scripts/build_default_assets.py
        COMMENT "Building default assets.bin based on configuration"
        VERBATIM
    )
    
    # Create target for generated assets
    add_custom_target(generated_default_assets ALL
        DEPENDS ${GENERATED_ASSETS_BIN}
    )
    
    # Set the generated file path in parent scope
    set(GENERATED_ASSETS_LOCAL_FILE ${GENERATED_ASSETS_BIN} PARENT_SCOPE)
    
    message(STATUS "Default assets build configured: ${GENERATED_ASSETS_BIN}")
endfunction()


# Function to get local assets file path (handles both URL and local file)
function(get_assets_local_file assets_source assets_local_file_var)
    # Check if it's a URL (starts with http:// or https://)
    if(assets_source MATCHES "^https?://")
        # It's a URL, download it
        get_filename_component(ASSETS_FILENAME "${assets_source}" NAME)
        set(ASSETS_LOCAL_FILE "${CMAKE_BINARY_DIR}/${ASSETS_FILENAME}")
        set(ASSETS_TEMP_FILE "${CMAKE_BINARY_DIR}/${ASSETS_FILENAME}.tmp")
        
        # Check if local file exists
        if(EXISTS ${ASSETS_LOCAL_FILE})
            message(STATUS "Assets file ${ASSETS_FILENAME} already exists, skipping download")
        else()
            message(STATUS "Downloading ${ASSETS_FILENAME}")
            
            # Clean up any existing temp file
            if(EXISTS ${ASSETS_TEMP_FILE})
                file(REMOVE ${ASSETS_TEMP_FILE})
            endif()
            
            # Download to temporary file first
            file(DOWNLOAD ${assets_source} ${ASSETS_TEMP_FILE}
                 STATUS DOWNLOAD_STATUS)
            list(GET DOWNLOAD_STATUS 0 STATUS_CODE)
            if(NOT STATUS_CODE EQUAL 0)
                # Clean up temp file on failure
                if(EXISTS ${ASSETS_TEMP_FILE})
                    file(REMOVE ${ASSETS_TEMP_FILE})
                endif()
                message(FATAL_ERROR "Failed to download ${ASSETS_FILENAME} from ${assets_source}")
            endif()
            
            # Move temp file to final location (atomic operation)
            file(RENAME ${ASSETS_TEMP_FILE} ${ASSETS_LOCAL_FILE})
            message(STATUS "Successfully downloaded ${ASSETS_FILENAME}")
        endif()
    else()
        # It's a local file path
        if(IS_ABSOLUTE "${assets_source}")
            set(ASSETS_LOCAL_FILE "${assets_source}")
        else()
            set(ASSETS_LOCAL_FILE "${CMAKE_CURRENT_SOURCE_DIR}/${assets_source}")
        endif()
        
        # Check if local file exists
        if(NOT EXISTS ${ASSETS_LOCAL_FILE})
            message(FATAL_ERROR "Assets file not found: ${ASSETS_LOCAL_FILE}")
        endif()
        
        message(STATUS "Using assets file: ${ASSETS_LOCAL_FILE}")
    endif()
    
    set(${assets_local_file_var} ${ASSETS_LOCAL_FILE} PARENT_SCOPE)
endfunction()


partition_table_get_partition_info(size "--partition-name assets" "size")
partition_table_get_partition_info(offset "--partition-name assets" "offset")
if ("${size}" AND "${offset}")
    # Flash assets based on configuration
    if(CONFIG_FLASH_DEFAULT_ASSETS)
        # Build default assets based on configuration
        build_default_assets_bin()
        esptool_py_flash_to_partition(flash "assets" "${GENERATED_ASSETS_LOCAL_FILE}")
        message(STATUS "Generated default assets flash configured: ${GENERATED_ASSETS_LOCAL_FILE} -> assets partition")
    elseif(CONFIG_FLASH_CUSTOM_ASSETS)
        # Flash custom assets
        get_assets_local_file("${CONFIG_CUSTOM_ASSETS_FILE}" ASSETS_LOCAL_FILE)
        esptool_py_flash_to_partition(flash "assets" "${ASSETS_LOCAL_FILE}")
        message(STATUS "Custom assets flash configured: ${ASSETS_LOCAL_FILE} -> assets partition")
    elseif(CONFIG_FLASH_NONE_ASSETS)
        message(STATUS "Assets flashing disabled (FLASH_NONE_ASSETS)")
    endif()
else()
    message(STATUS "Assets partition not found, using v1 partition table")
endif()
