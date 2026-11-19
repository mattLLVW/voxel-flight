#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

void setup();
void loop();

extern "C" void app_main(void)
{
    setup();
    for (;;) {
        loop();
        vTaskDelay(1);
    }
}
