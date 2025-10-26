#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <math.h>
#include <errno.h>
#include "freertos/ringbuf.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_vfs_fat.h"
#include "driver/spi_common.h"
#include "driver/i2s_std.h"
#include "sdmmc_cmd.h"
#include "mp3_decoder.h"
#include "minimp3.h"

#define TAG "MP3_PLAYER"

// MP3 aktiv (Sinus deaktiviert)
#ifndef ENABLE_MP3
#define ENABLE_MP3 1
#endif

// Optionale Signalaufbereitung
#ifndef ENABLE_DC_BLOCK
#define ENABLE_DC_BLOCK 1
#endif

#define MOUNT_POINT "/sdcard"
#define SD_MOSI     23
#define SD_MISO     19
#define SD_CLK      18
#define SD_CS       5

#define I2S_BCLK_PIN 26
#define I2S_LRC_PIN  25
#define I2S_DOUT_PIN 22

// Hinweis: Wir vermeiden paralleles Scannen/Abspielen, um FATFS-Kollisionen zu verhindern.

i2s_chan_handle_t i2s_tx_handle = NULL;
static sdmmc_card_t *card = NULL;

typedef struct {
    RingbufHandle_t rb;
    volatile bool done;
    volatile bool writer_done;
    TaskHandle_t h_writer;
    TaskHandle_t h_decoder;
    char path[512];
} play_ctx_t;

static void decoder_task(void *arg) {
    play_ctx_t *ctx = (play_ctx_t *)arg;
    // Streaming-Decoder mit minimp3 (rahmenweise), geringe RAM-Nutzung
    // Datei öffnen mit Retry
    FILE *f = NULL;
    for (int attempt = 1; attempt <= 5; ++attempt) {
        f = fopen(ctx->path, "rb");
        if (f) break;
        ESP_LOGW(TAG, "Decoder: fopen fehlgeschlagen (Versuch %d/5)", attempt);
        vTaskDelay(pdMS_TO_TICKS(100 * attempt));
    }
    if (!f) {
        ESP_LOGE(TAG, "Decoder: Datei nicht öffnbar: %s", ctx->path);
        ctx->done = true;
        vTaskDelete(NULL);
        return;
    }

    mp3dec_t dec;
    mp3dec_init(&dec);

    // Eingabepuffer (klein halten), Ausgabepuffer für max. Frame
    enum { IN_BUF_SIZE = 16 * 1024 };
    uint8_t *in_buf = (uint8_t *)malloc(IN_BUF_SIZE);
    if (!in_buf) {
        ESP_LOGE(TAG, "Decoder: Kein RAM für Eingabepuffer");
        fclose(f);
        ctx->done = true;
        vTaskDelete(NULL);
        return;
    }
    int16_t *pcm_buf = (int16_t *)malloc(sizeof(int16_t) * MINIMP3_MAX_SAMPLES_PER_FRAME);
    if (!pcm_buf) {
        ESP_LOGE(TAG, "Decoder: Kein RAM für PCM-Puffer");
        free(in_buf);
        fclose(f);
        ctx->done = true;
        vTaskDelete(NULL);
        return;
    }

    size_t buf_bytes = 0; // aktuell verfügbare Bytes in in_buf
    bool i2s_reconfigured = false;
    int stream_channels = 2; // Standard auf Stereo

    while (1) {
        // Nachladen, wenn Puffer klein ist
        if (buf_bytes < (size_t)(IN_BUF_SIZE / 2)) {
            size_t free_space = IN_BUF_SIZE - buf_bytes;
            size_t n = fread(in_buf + buf_bytes, 1, free_space, f);
            buf_bytes += n;
        }

        if (buf_bytes == 0) {
            break; // EOF
        }

        mp3dec_frame_info_t info;
        int samples_per_ch = mp3dec_decode_frame(&dec, in_buf, (int)buf_bytes, pcm_buf, &info);

        // Immer verbrauchte Bytes verwerfen (auch wenn 0 Samples)
        int consumed = info.frame_bytes;
        if (consumed <= 0) {
            // Kein vollständiger Frame gefunden, versuche mehr Daten zu laden
            // Schiebe minimal voran, um Endlosschleifen zu vermeiden
            consumed = info.frame_bytes; // kann 0 sein
            if (consumed == 0) {
                // Drop kleiner Teil und lade nach
                if (buf_bytes > 4) {
                    consumed = 1; // vorsichtig weiter
                } else {
                    // zu wenig Daten – nachladen in nächster Iteration
                    continue;
                }
            }
        }

        // Eingabepuffer nach Konsum verschieben
        if (consumed > 0) {
            memmove(in_buf, in_buf + consumed, buf_bytes - consumed);
            buf_bytes -= consumed;
        }

        if (samples_per_ch <= 0) {
            // Noch keine nutzbaren Samples – weiter lesen
            continue;
        }

        // I2S erst nach erster gelungenen Dekodierung auf die Stream-Rate umstellen
        if (!i2s_reconfigured) {
            int new_hz = info.hz > 0 ? info.hz : 44100;
            stream_channels = info.channels > 0 ? info.channels : 2;
            ESP_LOGI(TAG, "MP3-Stream: %d Hz, %d Kanäle", new_hz, stream_channels);
            // Nur die Clock neu setzen (Kanal wurde bereits initialisiert)
            ESP_ERROR_CHECK(i2s_channel_disable(i2s_tx_handle));
            i2s_std_clk_config_t clk_cfg_dyn = I2S_STD_CLK_DEFAULT_CONFIG(new_hz);
            clk_cfg_dyn.clk_src = I2S_CLK_SRC_APLL;
            clk_cfg_dyn.mclk_multiple = I2S_MCLK_MULTIPLE_256;
            ESP_ERROR_CHECK(i2s_channel_reconfig_std_clock(i2s_tx_handle, &clk_cfg_dyn));
            ESP_ERROR_CHECK(i2s_channel_enable(i2s_tx_handle));
            i2s_reconfigured = true;
        }

        // Bei Mono auf Stereo duplizieren
        int out_channels = stream_channels;
        if (stream_channels == 1) {
            // samples_per_ch Samples -> 2*samples interleaved
            for (int i = samples_per_ch - 1; i >= 0; --i) {
                int16_t s = pcm_buf[i];
                pcm_buf[2*i + 0] = s;
                pcm_buf[2*i + 1] = s;
            }
            out_channels = 2;
        }

#if ENABLE_DC_BLOCK
        // Optionaler DC-Blocker (1. Ordnung), reduziert leichten DC-Offset/Rumpeln
        {
            static float x_prev_l = 0.f, y_prev_l = 0.f;
            static float x_prev_r = 0.f, y_prev_r = 0.f;
            const float R = 0.995f; // ~20 Hz bei 44.1 kHz
            if (out_channels == 2) {
                for (int n = 0; n < samples_per_ch; ++n) {
                    float xl = (float)pcm_buf[2*n + 0];
                    float yl = xl - x_prev_l + R * y_prev_l;
                    x_prev_l = xl; y_prev_l = yl;
                    float xr = (float)pcm_buf[2*n + 1];
                    float yr = xr - x_prev_r + R * y_prev_r;
                    x_prev_r = xr; y_prev_r = yr;
                    if (yl > 32767.f) yl = 32767.f; else if (yl < -32768.f) yl = -32768.f;
                    if (yr > 32767.f) yr = 32767.f; else if (yr < -32768.f) yr = -32768.f;
                    pcm_buf[2*n + 0] = (int16_t)yl;
                    pcm_buf[2*n + 1] = (int16_t)yr;
                }
            } else {
                for (int n = 0; n < samples_per_ch; ++n) {
                    float x = (float)pcm_buf[n];
                    float y = x - x_prev_l + R * y_prev_l;
                    x_prev_l = x; y_prev_l = y;
                    if (y > 32767.f) y = 32767.f; else if (y < -32768.f) y = -32768.f;
                    pcm_buf[n] = (int16_t)y;
                }
            }
        }
#endif

        size_t bytes = (size_t)samples_per_ch * out_channels * sizeof(int16_t);
        if (bytes > 0) {
            if (xRingbufferSend(ctx->rb, (void *)pcm_buf, bytes, pdMS_TO_TICKS(1000)) != pdTRUE) {
                ESP_LOGW(TAG, "Decoder: Ringbuffer voll – drop/warte");
            }
        }

        // Stack-Wasserstand gelegentlich loggen
        static int log_cnt = 0;
        if ((++log_cnt % 20) == 0) {
            UBaseType_t hw = uxTaskGetStackHighWaterMark(NULL);
            ESP_LOGD(TAG, "Decoder stack HWMark: %u", (unsigned)hw);
        }
    }

    free(pcm_buf);
    free(in_buf);
    fclose(f);
    ctx->done = true;
    vTaskDelete(NULL);
}

static void i2s_writer_task(void *arg) {
    play_ctx_t *ctx = (play_ctx_t *)arg;
    while (1) {
        size_t item_size = 0;
        uint8_t *item = (uint8_t *)xRingbufferReceive(ctx->rb, &item_size, pdMS_TO_TICKS(100));
        if (item) {
            size_t offset = 0;
            while (offset < item_size) {
                size_t bw = 0;
                esp_err_t wret = i2s_channel_write(i2s_tx_handle, item + offset, item_size - offset, &bw, portMAX_DELAY);
                offset += bw;
                if (wret != ESP_OK) {
                    ESP_LOGW(TAG, "I2S write: %s", esp_err_to_name(wret));
                    break;
                }
            }
            vRingbufferReturnItem(ctx->rb, item);
        } else if (ctx->done) {
            break;
        }
        // Stack-Wasserstand gelegentlich loggen
        static int log_cnt2 = 0;
        if ((++log_cnt2 % 20) == 0) {
            UBaseType_t hw = uxTaskGetStackHighWaterMark(NULL);
            ESP_LOGD(TAG, "I2S writer stack HWMark: %u", (unsigned)hw);
        }
    }
    ctx->writer_done = true;
    vTaskDelete(NULL);
}

/* ====================================================== */
/* I2S Initialisierung (neue API) */
static esp_err_t init_i2s(void) {
    ESP_LOGI(TAG, "Initialisiere I2S...");

    // Kanal erstellen
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    // Größere DMA-Puffer für gleichmäßigeren Datenfluss
    chan_cfg.dma_desc_num = 6;
    chan_cfg.dma_frame_num = 512;
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &i2s_tx_handle, NULL));

    // Standardmodus konfigurieren inkl. GPIO-Pins (neue I2S-API)
    // Für den Testsinus starten wir mit 48 kHz (auf ESP32 besonders stabil)
    i2s_std_clk_config_t clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(48000);
    clk_cfg.clk_src = I2S_CLK_SRC_APLL;            // APLL für geringen Jitter
    clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256; // 256xFs
    i2s_std_config_t std_cfg = {
        .clk_cfg = clk_cfg,
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(16, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = I2S_BCLK_PIN,
            .ws   = I2S_LRC_PIN,
            .dout = I2S_DOUT_PIN,
            .din  = I2S_GPIO_UNUSED
        }
    };
    // 32-bit Slot (Padding), Daten bleiben 16-bit -> stabilere Taktung bei vielen DACs
    std_cfg.slot_cfg.slot_bit_width = I2S_SLOT_BIT_WIDTH_32BIT;
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(i2s_tx_handle, &std_cfg));

    // Kanal aktivieren
    ESP_ERROR_CHECK(i2s_channel_enable(i2s_tx_handle));

    ESP_LOGI(TAG, "I2S erfolgreich initialisiert.");
    return ESP_OK;
}

/* ====================================================== */
/* Test-Sinus über I2S ausgeben */
static void play_test_sine(int freq_hz, int duration_ms) {
    const int sample_rate = 48000; // entspricht init_i2s()
    const int channels = 2;        // Stereo
    const int16_t amplitude = 5000; // moderate Lautstärke
    const int chunk_frames = 512;  // pro Schreibvorgang
    int total_frames = (sample_rate * duration_ms) / 1000;
    int frames_written = 0;
    static float phase = 0.0f;
    const float two_pi = 6.283185307179586f;
    const float phase_inc = two_pi * (float)freq_hz / (float)sample_rate;

    int16_t buffer[chunk_frames * channels];
    ESP_LOGI(TAG, "Test-Sinus: %d Hz für %d ms", freq_hz, duration_ms);

    while (frames_written < total_frames) {
        int frames_this_time = (total_frames - frames_written) > chunk_frames ? chunk_frames : (total_frames - frames_written);
        for (int i = 0; i < frames_this_time; ++i) {
            int16_t sample = (int16_t)(sinf(phase) * amplitude);
            buffer[i * 2 + 0] = sample; // Left
            buffer[i * 2 + 1] = sample; // Right
            phase += phase_inc;
            if (phase > two_pi) phase -= two_pi;
        }
        size_t bytes_to_write = frames_this_time * channels * sizeof(int16_t);
        size_t bytes_written = 0;
        esp_err_t wret = i2s_channel_write(i2s_tx_handle, buffer, bytes_to_write, &bytes_written, portMAX_DELAY);
        if (wret != ESP_OK || bytes_written != bytes_to_write) {
            ESP_LOGW(TAG, "I2S write (sine): ret=%s, %u/%u bytes", esp_err_to_name(wret), (unsigned)bytes_written, (unsigned)bytes_to_write);
        }
        frames_written += frames_this_time;
    }
}

/* ====================================================== */
/* SD-Karteninitialisierung */
static esp_err_t init_sd_card(void) {
    ESP_LOGI(TAG, "Initialisiere SD-Karte über SPI...");

    spi_bus_config_t bus_cfg = {
        .mosi_io_num = SD_MOSI,
        .miso_io_num = SD_MISO,
        .sclk_io_num = SD_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4096,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus_cfg, SPI_DMA_CH_AUTO));

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = SD_CS;
    slot_config.host_id = SPI2_HOST;

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    // Konservativer Start mit 2 MHz für maximale Stabilität
    host.max_freq_khz = 2000;

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };

    // Mount mit einfachen Retries/Backoff
    const int max_tries = 3;
    esp_err_t ret = ESP_FAIL;
    for (int attempt = 1; attempt <= max_tries; ++attempt) {
        ret = esp_vfs_fat_sdspi_mount(MOUNT_POINT, &host, &slot_config, &mount_config, &card);
        if (ret == ESP_OK) break;
        ESP_LOGE(TAG, "SD-Mount fehlgeschlagen (Versuch %d/%d): %s", attempt, max_tries, esp_err_to_name(ret));
        vTaskDelay(pdMS_TO_TICKS(500 * attempt));
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Fehler beim Mounten der SD-Karte: %s", esp_err_to_name(ret));
        spi_bus_free(SPI2_HOST);
        return ret;
    }

    ESP_LOGI(TAG, "SD-Karte erfolgreich gemountet.");
    sdmmc_card_print_info(stdout, card);
    // Kurzes Settle-Delay, damit FATFS/SDSPI nach dem Mount zur Ruhe kommt
    vTaskDelay(pdMS_TO_TICKS(200));
    return ESP_OK;
}

/* ====================================================== */
/* MP3-Dateien abspielen mit neuer I2S API */
static bool list_directory_and_find_first_mp3(const char *path, char *out, size_t outlen) {
    ESP_LOGI(TAG, "Liste Dateien in %s auf:", path);
    DIR *dir = opendir(path);
    if (!dir) {
        ESP_LOGE(TAG, "Kann Verzeichnis %s nicht öffnen", path);
        return false;
    }
    bool found = false;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        ESP_LOGI(TAG, " - %s", entry->d_name);
        if (!found && (strstr(entry->d_name, ".mp3") || strstr(entry->d_name, ".MP3"))) {
            if (snprintf(out, outlen, "%s/%s", path, entry->d_name) >= outlen) {
                ESP_LOGW(TAG, "Dateiname zu lang: %s", entry->d_name);
            } else {
                found = true;
            }
        }
    }
    closedir(dir);
    // Kurzes Settle-Delay nach Verzeichnis-Scan
    vTaskDelay(pdMS_TO_TICKS(100));
    return found;
}

static void start_playback_from_file(const char *fullpath) {
    ESP_LOGI(TAG, "Spiele MP3-Datei: %s", fullpath);
    // Kurze Pause nach Directory-Operationen, um SDSPI/FS zu entlasten
    vTaskDelay(pdMS_TO_TICKS(200));

    // Ringbuffer und Tasks starten
    play_ctx_t *ctx = calloc(1, sizeof(play_ctx_t));
    if (!ctx) { ESP_LOGE(TAG, "Kein RAM für Playback-Kontext"); return; }
    // Kleinerer Ringbuffer, um RAM-Druck zu reduzieren
    ctx->rb = xRingbufferCreate(24 * 1024, RINGBUF_TYPE_BYTEBUF);
    if (!ctx->rb) { ESP_LOGE(TAG, "Ringbuffer konnte nicht erstellt werden"); free(ctx); return; }
    strlcpy(ctx->path, fullpath, sizeof(ctx->path));
    ctx->done = false;
    ctx->writer_done = false;
    // Decoder benötigt viel Stack (minimp3 Scratch groß) -> großzügig dimensionieren (~32 KB)
    xTaskCreate(decoder_task, "dec_task", 32768, ctx, 5, &ctx->h_decoder);
    xTaskCreate(i2s_writer_task, "i2s_task", 6144, ctx, 6, &ctx->h_writer);

    while (!ctx->done) { vTaskDelay(pdMS_TO_TICKS(100)); }
    // Warten, bis Writer sicher beendet ist (max. 2s)
    int wait_ms = 0;
    while (!ctx->writer_done && wait_ms < 2000) {
        vTaskDelay(pdMS_TO_TICKS(50));
        wait_ms += 50;
    }
    vRingbufferDelete(ctx->rb);
    free(ctx);
}

/* ====================================================== */
/* Directory-Listing Task */
// entfernt: list_directory_sync – Ausgabe erfolgt in list_directory_and_find_first_mp3

/* ====================================================== */
/* Hauptprogramm */
void app_main(void) {
    ESP_LOGI(TAG, "Starte MP3 Player...");

    // Verlagerung der Hauptlogik in einen eigenen Task mit größerem Stack,
    // um mögliche Stacküberläufe der main_task zu vermeiden (siehe Crash bei SDSPI).
    extern void player_task(void *arg);
    xTaskCreate(player_task, "player_task", 8192, NULL, 5, NULL);
    // app_main Task kann beendet werden
    vTaskDelete(NULL);
}

// Hauptlogik in separatem Task (größerer Stack)
void player_task(void *arg) {
    ESP_ERROR_CHECK(init_i2s());
//#if ENABLE_MP3
    if (init_sd_card() != ESP_OK) {
        ESP_LOGE(TAG, "SD-Karteninitialisierung fehlgeschlagen. Starte neu in 5s...");
        vTaskDelay(pdMS_TO_TICKS(5000));
        esp_restart();
    }

    // Einmaliges Öffnen des Verzeichnisses: Listing und erste MP3 ermitteln
    char first_mp3[512] = {0};
    bool found = list_directory_and_find_first_mp3(MOUNT_POINT, first_mp3, sizeof(first_mp3));
    if (found) {
        ESP_LOGI(TAG, "Gefunden: %s", first_mp3);
        start_playback_from_file(first_mp3);
    } else {
        ESP_LOGW(TAG, "Keine MP3-Datei gefunden");
    }

    ESP_LOGI(TAG, "Wiedergabe abgeschlossen. Gerät bleibt aktiv.");
    // Task in Idle halten
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
//#else
//    ESP_LOGI(TAG, "MP3 deaktiviert – kontinuierlicher 440 Hz Sinuston.");
//    while (1) {
//        // 1s Blöcke schreiben, nahtlos hintereinander
//        play_test_sine(440, 1000);
//    }
//#endif
}
