#include <Preferences.h>
#include <stdint.h>
#include <stddef.h>

const char *preferencesName = "test";

Preferences preferences;

// Save an array of times to flash. Each entry is 3 bytes: {hour, minute, second}.
// - `times` is a pointer to an array of `count` elements, each element is uint8_t[3].
// - `count` is the number of time entries.
void saveSettings(const uint8_t times[][3], size_t count)
{
    preferences.begin(preferencesName, false);

    uint16_t c = (uint16_t)count;
    preferences.putBytes("count", &c, sizeof(c));

    if (count > 0)
    {
        preferences.putBytes("times", times, (size_t)c * 3);
    }

    preferences.end();
}

// Load settings from flash into provided buffer.
// - `times` must point to a buffer able to hold up to `maxCount` entries.
// - On success, `count` will be set to the number of entries actually read.
// Returns true on success, false on failure or if no data present.
bool loadSettings(uint8_t times[][3], size_t &count, size_t maxCount)
{
    preferences.begin(preferencesName, true);

    uint16_t c = 0;
    size_t got = preferences.getBytes("count", &c, sizeof(c));
    if (got != sizeof(c))
    {
        preferences.end();
        count = 0;
        return false;
    }

    if (c == 0)
    {
        preferences.end();
        count = 0;
        return true;
    }

    if ((size_t)c > maxCount)
    {
        c = (uint16_t)maxCount;
    }

    size_t expected = (size_t)c * 3;
    size_t read = preferences.getBytes("times", times, expected);
    preferences.end();

    if (read != expected)
    {
        count = 0;
        return false;
    }

    count = c;
    return true;
}