#pragma once

#include <algorithm>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

using std::max;
using std::min;

#ifndef PI
#define PI 3.14159265358979323846f
#endif
#ifndef TWO_PI
#define TWO_PI 6.28318530717958647692f
#endif

static inline uint32_t millis()
{
    return static_cast<uint32_t>(esp_timer_get_time() / 1000);
}

static inline uint32_t micros()
{
    return static_cast<uint32_t>(esp_timer_get_time());
}

static inline void delay(uint32_t milliseconds)
{
    vTaskDelay(pdMS_TO_TICKS(milliseconds));
}

inline uint32_t voxel_random_state = 1;

static inline void randomSeed(uint32_t seed)
{
    voxel_random_state = seed ?: 1;
}

static inline long random(long upper)
{
    if (upper <= 0) return 0;
    voxel_random_state ^= voxel_random_state << 13;
    voxel_random_state ^= voxel_random_state >> 17;
    voxel_random_state ^= voxel_random_state << 5;
    return static_cast<long>(voxel_random_state % static_cast<uint32_t>(upper));
}

static inline long random(long lower, long upper)
{
    return lower + random(upper - lower);
}

class IdCompatibleSerial {
public:
    void printf(const char *format, ...)
    {
        va_list args;
        va_start(args, format);
        vprintf(format, args);
        va_end(args);
    }

    void println(const char *text = "")
    {
        std::printf("%s\n", text);
    }

    size_t write(const uint8_t *data, size_t length)
    {
        size_t written = fwrite(data, 1, length, stdout);
        fflush(stdout);
        return written;
    }
};

inline IdCompatibleSerial Serial;

void board_init();
void board_blit(const uint16_t *frame, int width, int height);
bool board_selection_pressed();
