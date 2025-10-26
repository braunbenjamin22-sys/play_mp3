#ifndef MP3_FRAME_H
#define MP3_FRAME_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/**
 * @brief Berechnet die Größe eines MP3-Frames basierend auf seinem Header
 * 
 * @param header Pointer auf die ersten 4 Bytes des Frame-Headers
 * @return int Frame-Größe in Bytes oder -1 bei ungültigem Header
 */
int calculate_mp3_frame_size(const uint8_t* header);

/**
 * @brief Überprüft, ob ein MP3-Frame gültig ist
 * 
 * @param data Pointer auf die Daten
 * @param length Länge der verfügbaren Daten
 * @param frame_size Pointer für die zurückgegebene Frame-Größe
 * @return true Frame ist gültig
 * @return false Frame ist ungültig
 */
bool verify_mp3_frame(const uint8_t* data, size_t length, size_t* frame_size);

/**
 * @brief Sucht den nächsten gültigen MP3-Frame in den Daten
 * 
 * @param data Pointer auf die Daten
 * @param length Länge der Daten
 * @param frame_size Pointer für die zurückgegebene Frame-Größe
 * @return size_t Offset zum nächsten Frame oder SIZE_MAX wenn keiner gefunden
 */
size_t find_next_mp3_frame(const uint8_t* data, size_t length, size_t* frame_size);

#endif // MP3_FRAME_H