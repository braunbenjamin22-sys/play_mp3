# Milestone v1: Stabiler MP3-Playback (ESP32, ESP-IDF v5.5.1)

Dieser Meilenstein liefert zuverlässige MP3-Wiedergabe von SD (SDSPI) über I2S mit korrekter Abspielgeschwindigkeit und stabilen Tasks.

## Highlights
- Präzise I2S-Taktung für 44.1-kHz-Familie: APLL + 384×Fs; 48-kHz-Familie: APLL + 256×Fs
- I2S Slot-Breite 16 Bit; neue I2S-API (i2s_std)
- Streaming-Decoder (minimp3) frame-weise, Ringbuffer → Writer-Task (keine gleichzeitigen FATFS-Zugriffe)
- Robuster SD-Mount (SDSPI 2 MHz, Retry + Settle), sauberes Aufräumen
- Decoder-Task Stack auf 32 KB erhöht → keine Stackoverflows

## Pinbelegung
- SD (SDSPI): MISO=19, MOSI=23, CLK=18, CS=5
- I2S (TX): BCLK=26, LRC/WS=25, DOUT=22 (MCLK unbenutzt)

## Bauen & Flashen (Windows, VS Code Tasks)
- Build: "ESP-IDF: Activate + Build"
- Flash: "ESP-IDF: Activate + Flash (COM3)"
- Monitor: "ESP-IDF: Activate + Monitor (COM3)" (Beenden: Ctrl + ])

Optional per Terminal (ESP-IDF-Terminal zuvor aktivieren):

```powershell
idf.py build
idf.py -p COM3 flash
idf.py -p COM3 monitor
```

## Bekannte Punkte
- DC-Blocker (1. Ordnung) ist aktiv; weitere Klangprofile wurden verworfen zugunsten maximaler Stabilität.
- 44.1-kHz-Streams werden beim ersten Frame per Clock-only-Reconfig korrekt gesetzt (kein Kanal-Reinit).
- SD-Frequenz aktuell konservativ (2 MHz); kann bei Bedarf erhöht werden.

## Quelle
- Tag: `milestone-v1`
- Hauptänderungen in `main/main.c` (I2S-Init, dynamische Clock-Reconfig, Decoder/Writer-Tasks, SD-Mount)