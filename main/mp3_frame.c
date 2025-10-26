#include <string.h>
#include "esp_log.h"
#include "mp3_frame.h"

// MP3 Frame-Konstanten
static const int mp3_bitrates[16] = {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0}; // in kbps
static const int mp3_samplerates[4] = {44100, 48000, 32000, 0}; // in Hz

int calculate_mp3_frame_size(const uint8_t* header) {
    if (!header) return -1;
    
    // Frame-Header validieren
    if (header[0] != 0xFF || (header[1] & 0xE0) != 0xE0) {
        return -1;
    }

    int bitrate_index = (header[2] >> 4) & 0x0F;
    int sampling_rate_index = (header[2] >> 2) & 0x03;
    int padding = (header[2] >> 1) & 0x01;
    
    // Ungültige Indizes prüfen
    if (bitrate_index >= 16 || sampling_rate_index >= 4) {
        return -1;
    }
    
    int bitrate = mp3_bitrates[bitrate_index];
    int samplerate = mp3_samplerates[sampling_rate_index];
    
    // Ungültige Werte prüfen
    if (bitrate == 0 || samplerate == 0) {
        return -1;
    }
    
    // Formel für MPEG 1 Layer III:
    // FrameSize = (144 * BitRate * 1000) / SampleRate + Padding
    return (144 * bitrate * 1000) / samplerate + padding;
}

bool verify_mp3_frame(const uint8_t* data, size_t length, size_t* frame_size) {
    if (!data || length < 4 || !frame_size) {
        return false;
    }
    
    // Erste 11 Bits müssen 1 sein (Frame-Sync)
    if (data[0] != 0xFF || (data[1] & 0xE0) != 0xE0) {
        return false;
    }

    // MPEG Version überprüfen (bits 4,3 von Byte 2)
    uint8_t version = (data[1] >> 3) & 0x03;
    if (version == 0x01) { // Reserved value
        return false;
    }

    // Layer überprüfen (bits 2,1 von Byte 2)
    uint8_t layer = (data[1] >> 1) & 0x03;
    if (layer == 0x00) { // Reserved value
        return false;
    }

    // Frame-Größe berechnen
    int size = calculate_mp3_frame_size(data);
    if (size <= 0) {
        return false;
    }
    
    // Wenn genügend Daten vorhanden sind, den nächsten Frame überprüfen
    if (length >= (size_t)(size + 4)) {
        const uint8_t* next_frame = data + size;
        if (next_frame[0] == 0xFF && (next_frame[1] & 0xE0) == 0xE0) {
            *frame_size = size;
            return true;
        }
    }
    
    // Wenn wir den nächsten Frame nicht überprüfen können,
    // akzeptieren wir diesen Frame trotzdem als gültig
    *frame_size = size;
    return true;
}

size_t find_next_mp3_frame(const uint8_t* data, size_t length, size_t* frame_size) {
    if (!data || length < 4 || !frame_size) {
        return SIZE_MAX;
    }
    
    for (size_t i = 0; i < length - 4; i++) {
        if (verify_mp3_frame(data + i, length - i, frame_size)) {
            return i;
        }
    }
    
    return SIZE_MAX;
}