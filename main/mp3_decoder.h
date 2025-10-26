#ifndef MP3_DECODER_H
#define MP3_DECODER_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>   // ✅ Wichtig für bool
#include "minimp3_ex.h"
#include "driver/i2s_std.h"

extern i2s_chan_handle_t i2s_tx_handle;

void play_mp3_file(const char *path);
bool is_valid_mp3_header(const uint8_t *data, size_t length);

#endif // MP3_DECODER_H
