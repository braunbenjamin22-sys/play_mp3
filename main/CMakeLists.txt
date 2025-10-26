idf_component_register(SRCS "mp3_decoder.c" "main.c" "mp3_frame.c"
                      INCLUDE_DIRS "."
                      REQUIRES "minimp3" "driver" "fatfs" "sdmmc" "esp_ringbuf")


