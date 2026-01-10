idf_component_register(SRCS "alarm_music.c"
                       INCLUDE_DIRS "."
                       REQUIRES audio_player audio_hw xl9555_keys http_request)
