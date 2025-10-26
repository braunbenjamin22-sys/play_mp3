# Copilot Instructions for play_mp3 ESP-IDF Project
This is an ESP32 project that interfaces with an SD card over SPI to play MP3 files. Here's what you need to know to work effectively with this codebase:
## Project Architecture
# Copilot Instructions for play_mp3 ESP-IDF Project

This is an ESP32 project that interfaces with an SD card over SPI to play MP3 files. Here's what you need to know to work effectively with this codebase:

## Project Architecture

- **Main Application (`main/main.c`)**: Handles SD card initialization and filesystem mounting using ESP-IDF's FAT filesystem
- **Audio Decoder**: Uses `minimp3` component for MP3 decoding (lightweight, single-header library)
- **Hardware Interface**: 
   - SD card via SPI (SDSPI): MISO=19, MOSI=23, CLK=18, CS=5 (configurable in `main.c`)
   - I2S (audio out, new API): BCLK=26, LRC=25, DIN=22
   - Pinbelegung per Defines in `main.c`
## Key Patterns & Conventions
1. **SD Card Access Pattern**:
2. **Error Handling**:
3. **Resource Management**:
## Development Workflow
0. **Activate ESP-IDF environment (Windows)**
   - In VS Code: use "ESP-IDF: Open ESP-IDF Terminal" (recommended)
   - Or PowerShell: `. C:/Espressif/frameworks/esp-idf-v5.5.1/export.ps1`

## Key Patterns & Conventions

1. **SD Card Access Pattern**:
   ```c
   // Initialize → Mount → Access Files → Unmount → Free Bus
   spi_bus_initialize() → esp_vfs_fat_sdspi_mount() → file_ops → esp_vfs_fat_sdcard_unmount() → spi_bus_free()
   ```

2. **Error Handling**:
   - All ESP-IDF API calls return `esp_err_t`
   - Check return values and use `ESP_ERROR_CHECK()` for critical paths
   - Use `ESP_LOGI()/ESP_LOGE()` for logging with appropriate tags

3. **Resource Management**:
   - Mount configuration limits max open files to 5
   - Allocation unit size set to 16KB for FAT filesystem
   - Always unmount before freeing SPI bus

## Development Workflow

1. **Build & Flash**:
   ```powershell
   idf.py build
   idf.py -p COM3 flash
   idf.py -p COM3 monitor  # exit: Ctrl+]
## Key Files
## Cross-Component Integration

2. **Debugging**:
   - Monitor serial output for filesystem and SD card status
   - Set log levels with `esp_log_level_set()`
   - Check mounting errors and card info in serial output

## Key Files

- `main/main.c`: Application entry point and SD card handling
- `components/minimp3/`: MP3 decoder library
- `sdkconfig`: Project configuration (generated)
- `sdkconfig.defaults`: Default project configuration

## Cross-Component Integration

1. **Component Dependencies**:
1. **Component Dependencies**:
   - Main component requires: `driver`, `fatfs`, `sdmmc`, `minimp3` (see `main/CMakeLists.txt`)
   - Do not hardcode example component paths in `main/idf_component.yml`. The previous example `sd_card` path was removed; built-in IDF components are used instead.
   - Each ESP-IDF component follows the standard structure
   - Each ESP-IDF component follows standard component structure

2. **Memory Considerations**:
## Common Patterns
   - Be mindful of stack usage in interrupt handlers
   - Buffer sizes affect performance - current SPI transfer size is 4KB

## Common Patterns

1. **ESP-IDF Initialization**:
   ```c
   void app_main(void) {
       // Initialize NVS (if needed)
       // Initialize peripherals
       // Mount filesystem
       // Main application logic
   }
   ```

2. **Filesystem Operations**:
   - Use standard POSIX file operations (`open`, `read`, `write`, etc.)
   - Remember to include `<sys/unistd.h>` and related headers
   - All paths should be prefixed with `MOUNT_POINT` ("/sdcard")

   3. **I2S (new API)**
       - Use `driver/i2s_std.h` and configure pins via `i2s_std_config_t.gpio_cfg` when calling `i2s_channel_init_std_mode()`.
       - Avoid legacy `driver/i2s.h` APIs in new code.
       - Example snippet:
          ```c
          i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
          i2s_chan_handle_t i2s_tx_handle;
          ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &i2s_tx_handle, NULL));

          i2s_std_config_t std_cfg = {
             .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(44100),
             .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(16, I2S_SLOT_MODE_STEREO),
             .gpio_cfg = { .mclk = I2S_GPIO_UNUSED, .bclk = 26, .ws = 25, .dout = 22, .din = I2S_GPIO_UNUSED }
          };
          ESP_ERROR_CHECK(i2s_channel_init_std_mode(i2s_tx_handle, &std_cfg));
          ESP_ERROR_CHECK(i2s_channel_enable(i2s_tx_handle));
## Troubleshooting Guides
1. **Mount Failures**:
2. **Performance Issues**:

## Troubleshooting Guides

1. **Mount Failures**:
   - Check SPI pin configurations
   - Verify card format (FAT32)
   - Check `esp_err_t` return codes

2. **Performance Issues**:
   - Adjust `SD_SPI_FREQ_KHZ` (currently 4MHz)
   - Monitor buffer sizes and transfer sizes
   - Check for proper error handling and resource cleanup

3. **idf.py not found (Windows)**
   - Open an ESP-IDF Terminal in VS Code or run `export.ps1` in PowerShell.
   - Ensure tools are installed: `C:/Espressif/frameworks/esp-idf-v5.5.1/install.ps1`.

4. **Tool paths in VS Code**
   - In `settings.json` prefer forward slashes in Windows paths. Make sure:
     - `idf.espIdfPathWin` = `C:/Espressif/frameworks/esp-idf-v5.5.1/`
     - `idf.toolsPathWin` = `C:/Espressif` (nicht `C:/Espressif/tools`!)
     - Executables: `idf.pythonInstallPath`, `idf.gitPathWin`, `idf.cmakePathWin`, `idf.ninjaPathWin`

5. **Known warnings**
   - `minimp3.c` self-test may emit use-after-free warnings; they are confined to test code paths and don't affect playback.
   - A warning about unknown Kconfig symbol in `sdkconfig.defaults` (e.g. `FATFS_VFS_FSTAT_BLKSIZE`) can be removed or updated when migrating IDF versions.

Remember to unmount filesystems and free resources before major reconfigurations or power events.