#ifndef I2S_SETUP_H
#define I2S_SETUP_H
#include <stdio.h>
#include <sys/types.h>
#include "esp_system.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mp3_decoder.h"

#include "driver/i2s.h"
#include "esp_err.h"

// I2S Pins für MAX98357A
#define I2S_BCK_PIN   26
#define I2S_WS_PIN    25
#define I2S_DATA_PIN  22

// I2S Konfiguration
#define SAMPLE_RATE     44100
#define I2S_CHANNELS    2
#define I2S_BITS       16

esp_err_t init_i2s(void) {
    // I2S Konfiguration
    i2s_config_t i2s_config = {
        .mode = I2S_MODE_MASTER | I2S_MODE_TX,
        .sample_rate = SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .dma_buf_count = 8,
        .dma_buf_len = 64,
        .use_apll = false,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1
    };

    // I2S Pin Konfiguration
    i2s_pin_config_t pin_config = {
        .bck_io_num = I2S_BCK_PIN,
        .ws_io_num = I2S_WS_PIN,
        .data_out_num = I2S_DATA_PIN,
        .data_in_num = I2S_PIN_NO_CHANGE
    };

    // I2S Treiber installieren
    esp_err_t ret = i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
    if (ret != ESP_OK) {
        return ret;
    }

    // I2S Pins konfigurieren
    ret = i2s_set_pin(I2S_NUM_0, &pin_config);
    if (ret != ESP_OK) {
        i2s_driver_uninstall(I2S_NUM_0);
        return ret;
    }

    return ESP_OK;
}

#endif // I2S_SETUP_H