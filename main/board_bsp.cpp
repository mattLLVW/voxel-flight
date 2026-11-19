#include "board_bsp.h"

#include <cstdint>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/spi_master.h"
#include "esp_attr.h"
#include "esp_lcd_panel_io.h"
#include "esp_log.h"

namespace {

constexpr const char *TAG = "voxel-flight";
constexpr spi_host_device_t LCD_HOST = SPI2_HOST;
constexpr int LCD_WIDTH = 320;
constexpr int LCD_HEIGHT = 170;
constexpr int STRIP_HEIGHT = 10;
constexpr int PANEL_Y_OFFSET = 35;
constexpr int LCD_MOSI = 4;
constexpr int LCD_CLK = 5;
constexpr int LCD_CS = 7;
constexpr int LCD_DC = 6;
constexpr int LCD_RST = 14;
constexpr int BOOT_BUTTON = 9;
constexpr int TOUCH_SDA = 18;
constexpr int TOUCH_SCL = 8;
constexpr i2c_port_t TOUCH_PORT = I2C_NUM_0;
constexpr uint8_t TOUCH_ADDRESS = 0x15;

struct LcdCommand {
    uint8_t command;
    const uint8_t *data;
    size_t length;
    uint32_t delay_ms;
};

constexpr uint8_t CMD_36[] = {0x70};
constexpr uint8_t CMD_3A[] = {0x55};
constexpr uint8_t CMD_B2[] = {0x0c, 0x0c, 0x00, 0x33, 0x33};
constexpr uint8_t CMD_B7[] = {0x35};
constexpr uint8_t CMD_BB[] = {0x13};
constexpr uint8_t CMD_C0[] = {0x2c};
constexpr uint8_t CMD_C2[] = {0x01};
constexpr uint8_t CMD_C3[] = {0x0b};
constexpr uint8_t CMD_C4[] = {0x20};
constexpr uint8_t CMD_C6[] = {0x0f};
constexpr uint8_t CMD_D0[] = {0xa4, 0xa1};
constexpr uint8_t CMD_D6[] = {0xa1};
constexpr uint8_t CMD_E0[] = {
    0x00, 0x03, 0x07, 0x08, 0x07, 0x15, 0x2a,
    0x44, 0x42, 0x0a, 0x17, 0x18, 0x25, 0x27
};
constexpr uint8_t CMD_E1[] = {
    0x00, 0x03, 0x08, 0x07, 0x07, 0x23, 0x2a,
    0x43, 0x42, 0x09, 0x18, 0x17, 0x25, 0x27
};

constexpr LcdCommand LCD_INIT_COMMANDS[] = {
    {0x36, CMD_36, sizeof(CMD_36), 0},
    {0x3a, CMD_3A, sizeof(CMD_3A), 0},
    {0xb2, CMD_B2, sizeof(CMD_B2), 0},
    {0xb7, CMD_B7, sizeof(CMD_B7), 0},
    {0xbb, CMD_BB, sizeof(CMD_BB), 0},
    {0xc0, CMD_C0, sizeof(CMD_C0), 0},
    {0xc2, CMD_C2, sizeof(CMD_C2), 0},
    {0xc3, CMD_C3, sizeof(CMD_C3), 0},
    {0xc4, CMD_C4, sizeof(CMD_C4), 0},
    {0xc6, CMD_C6, sizeof(CMD_C6), 0},
    {0xd0, CMD_D0, sizeof(CMD_D0), 0},
    {0xd6, CMD_D6, sizeof(CMD_D6), 0},
    {0xe0, CMD_E0, sizeof(CMD_E0), 0},
    {0xe1, CMD_E1, sizeof(CMD_E1), 0},
    {0x21, nullptr, 0, 0},
    {0x11, nullptr, 0, 120},
    {0x29, nullptr, 0, 0},
};

esp_lcd_panel_io_handle_t display_io = nullptr;
DMA_ATTR uint16_t display_pixels[2][LCD_WIDTH * STRIP_HEIGHT];
SemaphoreHandle_t transfer_done = nullptr;
i2c_master_bus_handle_t touch_bus = nullptr;
i2c_master_dev_handle_t touch_device = nullptr;

bool display_transfer_done(esp_lcd_panel_io_handle_t,
                           esp_lcd_panel_io_event_data_t *,
                           void *)
{
    BaseType_t higher_priority_task_woken = pdFALSE;
    xSemaphoreGiveFromISR(transfer_done, &higher_priority_task_woken);
    return higher_priority_task_woken == pdTRUE;
}

void init_display()
{
    spi_bus_config_t bus = {};
    bus.mosi_io_num = LCD_MOSI;
    bus.miso_io_num = -1;
    bus.sclk_io_num = LCD_CLK;
    bus.quadwp_io_num = -1;
    bus.quadhd_io_num = -1;
    bus.max_transfer_sz =
        LCD_WIDTH * STRIP_HEIGHT * sizeof(uint16_t);
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &bus, SPI_DMA_CH_AUTO));

    esp_lcd_panel_io_spi_config_t io = {};
    io.cs_gpio_num = static_cast<gpio_num_t>(LCD_CS);
    io.dc_gpio_num = static_cast<gpio_num_t>(LCD_DC);
    io.spi_mode = 0;
    io.pclk_hz = 40 * 1000 * 1000;
    io.trans_queue_depth = 2;
    io.on_color_trans_done = display_transfer_done;
    io.lcd_cmd_bits = 8;
    io.lcd_param_bits = 8;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(
        static_cast<esp_lcd_spi_bus_handle_t>(LCD_HOST), &io, &display_io));

    gpio_reset_pin(static_cast<gpio_num_t>(LCD_RST));
    gpio_set_direction(static_cast<gpio_num_t>(LCD_RST), GPIO_MODE_OUTPUT);
    gpio_set_level(static_cast<gpio_num_t>(LCD_RST), 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(static_cast<gpio_num_t>(LCD_RST), 1);
    vTaskDelay(pdMS_TO_TICKS(120));

    for (const LcdCommand &item : LCD_INIT_COMMANDS) {
        ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(
            display_io, item.command, item.data, item.length));
        if (item.delay_ms) vTaskDelay(pdMS_TO_TICKS(item.delay_ms));
    }

}

void init_touch()
{
    i2c_master_bus_config_t bus = {};
    bus.i2c_port = TOUCH_PORT;
    bus.sda_io_num = static_cast<gpio_num_t>(TOUCH_SDA);
    bus.scl_io_num = static_cast<gpio_num_t>(TOUCH_SCL);
    bus.clk_source = I2C_CLK_SRC_DEFAULT;
    bus.glitch_ignore_cnt = 7;
    bus.flags.enable_internal_pullup = true;
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus, &touch_bus));

    i2c_device_config_t device = {};
    device.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    device.device_address = TOUCH_ADDRESS;
    device.scl_speed_hz = 200000;
    ESP_ERROR_CHECK(i2c_master_bus_add_device(
        touch_bus, &device, &touch_device));

    constexpr uint8_t normal_mode[] = {0x00, 0x00};
    const esp_err_t error = i2c_master_transmit(
        touch_device, normal_mode, sizeof(normal_mode), 50);
    if (error != ESP_OK) {
        ESP_LOGW(TAG, "Touch controller did not acknowledge: %s",
                 esp_err_to_name(error));
    }
}

bool touch_pressed()
{
    constexpr uint8_t status_register = 0x00;
    uint8_t report[7] = {};
    const esp_err_t error = i2c_master_transmit_receive(
        touch_device, &status_register, 1, report, sizeof(report), 10);
    return error == ESP_OK && report[2] != 0;
}

}  // namespace

void board_init()
{
    transfer_done = xSemaphoreCreateCounting(2, 0);
    ESP_ERROR_CHECK(transfer_done ? ESP_OK : ESP_ERR_NO_MEM);
    init_display();
    init_touch();

    gpio_config_t button = {
        .pin_bit_mask = 1ULL << BOOT_BUTTON,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&button));
    ESP_LOGI(TAG, "Waveshare ESP32-C6 Touch LCD initialized");
}

void board_blit(const uint16_t *frame, int width, int height)
{
    int pending = 0;
    int buffer = 0;
    for (int strip_y = 0; strip_y < LCD_HEIGHT; strip_y += STRIP_HEIGHT) {
        if (pending == 2) {
            xSemaphoreTake(transfer_done, portMAX_DELAY);
            --pending;
        }
        const int lines = min(STRIP_HEIGHT, LCD_HEIGHT - strip_y);
        uint16_t *target_buffer = display_pixels[buffer];
        for (int line = 0; line < lines; ++line) {
            const int y = strip_y + line;
            const int source_y = y * height / LCD_HEIGHT;
            uint16_t *target = target_buffer + line * LCD_WIDTH;
            for (int x = 0; x < LCD_WIDTH; ++x) {
                const int source_x = x * width / LCD_WIDTH;
                target[x] = __builtin_bswap16(
                    frame[source_y * width + source_x]);
            }
        }

        const uint8_t columns[] = {
            0, 0, static_cast<uint8_t>((LCD_WIDTH - 1) >> 8),
            static_cast<uint8_t>(LCD_WIDTH - 1)
        };
        const int row_start = PANEL_Y_OFFSET + strip_y;
        const int row_end = row_start + lines - 1;
        const uint8_t rows[] = {
            static_cast<uint8_t>(row_start >> 8),
            static_cast<uint8_t>(row_start),
            static_cast<uint8_t>(row_end >> 8),
            static_cast<uint8_t>(row_end)
        };
        ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(
            display_io, 0x2a, columns, sizeof(columns)));
        ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(
            display_io, 0x2b, rows, sizeof(rows)));
        ESP_ERROR_CHECK(esp_lcd_panel_io_tx_color(
            display_io, 0x2c, target_buffer,
            LCD_WIDTH * lines * sizeof(uint16_t)));
        ++pending;
        buffer ^= 1;
    }

    while (pending) {
        xSemaphoreTake(transfer_done, portMAX_DELAY);
        --pending;
    }
}

bool board_selection_pressed()
{
    return gpio_get_level(static_cast<gpio_num_t>(BOOT_BUTTON)) == 0 ||
           touch_pressed();
}
