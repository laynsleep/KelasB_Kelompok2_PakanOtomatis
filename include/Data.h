/* Header for data.cpp: save/load time entries to Preferences */
#ifndef DATA_H
#define DATA_H

#include <stdint.h>
#include <stddef.h>

// Save an array of times to flash. Each entry is 3 bytes: {hour, minute, second}.
void saveSettings(const uint8_t times[][3], size_t count);

// Load settings from flash into provided buffer.
// - `times` must point to a buffer able to hold up to `maxCount` entries.
// - On success, `count` will be set to the number of entries read.
// Returns true on success, false on failure.
bool loadSettings(uint8_t times[][3], size_t &count, size_t maxCount);

#endif // DATA_H
