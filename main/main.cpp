#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "../voxel-flight.ino"

extern "C" void app_main(void)
{
    setup();
    for (;;) {
        loop();
        vTaskDelay(1);
    }
}
