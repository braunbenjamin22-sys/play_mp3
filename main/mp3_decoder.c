#include "mp3_decoder.h"
#include "minimp3_ex.h"
#include "driver/i2s_std.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"

extern i2s_chan_handle_t i2s_tx_handle;

#define TAG "MP3_DECODER"

void play_mp3_file(const char *path) {
    ESP_LOGI(TAG, "MP3_DECODER: Spiele MP3-Datei: %s", path);

    mp3dec_ex_t mp3;
    if (mp3dec_ex_open(&mp3, path, MP3D_SEEK_TO_SAMPLE)) {
        ESP_LOGE(TAG, "MP3_DECODER: Fehler beim Öffnen der MP3-Datei: %s", path);
        return;
    }

    int16_t pcm_buf[1152*2]; // Stereo, 1152 Samples pro Frame
    size_t samples_read = 0;

    while ((samples_read = mp3dec_ex_read(&mp3, pcm_buf, sizeof(pcm_buf)/sizeof(pcm_buf[0]))) > 0) {
        size_t bytes_to_write = samples_read * sizeof(int16_t);
        size_t bytes_written = 0;
        while (bytes_written < bytes_to_write) {
            size_t ret = 0;
            i2s_channel_write(i2s_tx_handle, (char*)pcm_buf + bytes_written,
                              bytes_to_write - bytes_written, &ret, portMAX_DELAY);
            bytes_written += ret;
        }
    }

    mp3dec_ex_close(&mp3);
    ESP_LOGI(TAG, "MP3_DECODER: Wiedergabe beendet: %s", path);
}
