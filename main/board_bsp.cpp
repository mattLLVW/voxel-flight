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
constexpr uint8_t IMU_ADDRESSES[] = {0x6b, 0x6a};
constexpr uint8_t IMU_WHO_AM_I = 0x00;
constexpr uint8_t IMU_CTRL1 = 0x02;
constexpr uint8_t IMU_CTRL2 = 0x03;
constexpr uint8_t IMU_CTRL3 = 0x04;
constexpr uint8_t IMU_CTRL7 = 0x08;
constexpr uint8_t IMU_RESET = 0x60;
constexpr uint8_t IMU_RESET_RESULT = 0x4d;
constexpr uint8_t IMU_ACCEL_X_L = 0x35;          // accel X/Y/Z then gyro X/Y/Z, 16-bit little endian
constexpr float IMU_ACCEL_SCALE = 4096.0f;       // LSB per g at +-8 g
constexpr float IMU_GYRO_SCALE = 32.0f;          // LSB per deg/s at +-1024 deg/s
constexpr float DEG_TO_RAD_F = PI / 180.0f;
constexpr float TILT_RANGE_RAD = 25.0f * DEG_TO_RAD_F;
constexpr float TILT_DEAD_ZONE = 0.08f;
constexpr float FUSION_TIME_CONSTANT = 0.6f;     // seconds the accelerometer takes to correct the gyro
constexpr int MANUAL_HOLD_FRAMES = 90;           // ~3 s of neutral before the autopilot returns
// Board axes are not marked on the case: flip these if a tilt steers the wrong way.
constexpr bool INVERT_STEER = false;
constexpr bool INVERT_CLIMB = false;
constexpr bool DEBUG_TILT = false;

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
i2c_master_dev_handle_t imu_device = nullptr;
struct ImuState {
    bool ready = false;
    bool have_angles = false;
    float roll = 0;                // rotation about the board's short axis: tilting left/right in landscape
    float pitch = 0;               // rotation about the board's long axis: tilting top up/down in landscape
    float gyro_bias[2] = {0, 0};   // rad/s
    float neutral_roll = 0;
    float neutral_pitch = 0;
    float steer = 0;
    float climb = 0;
    int manual_hold = 0;
    int64_t last_us = 0;
    bool button_was_down = false;
};

ImuState imu;

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

esp_err_t imu_write_register(uint8_t reg, uint8_t value)
{
    const uint8_t command[] = {reg, value};
    return i2c_master_transmit(
        imu_device, command, sizeof(command), 50);
}

esp_err_t imu_read_registers(
    uint8_t reg, uint8_t *data, size_t length)
{
    return i2c_master_transmit_receive(
        imu_device, &reg, 1, data, length, 20);
}

struct ImuSample {
    float accel_roll;
    float accel_pitch;
    float accel_g;
    float gyro_roll;   // rad/s, bias not removed
    float gyro_pitch;
};

bool read_imu_sample(ImuSample &sample)
{
    uint8_t data[12];
    if (imu_read_registers(IMU_ACCEL_X_L, data, sizeof(data)) != ESP_OK) {
        return false;
    }

    auto word = [&](int index) {
        return static_cast<float>(static_cast<int16_t>(
            data[index * 2] | (data[index * 2 + 1] << 8)));
    };
    const float ax = word(0) / IMU_ACCEL_SCALE;
    const float ay = word(1) / IMU_ACCEL_SCALE;
    const float az = word(2) / IMU_ACCEL_SCALE;
    const float gx = word(3) / IMU_GYRO_SCALE * DEG_TO_RAD_F;
    const float gy = word(4) / IMU_GYRO_SCALE * DEG_TO_RAD_F;

    sample.accel_g = sqrtf(ax * ax + ay * ay + az * az);
    if (sample.accel_g < 0.3f) return false;
    // A rotation by +theta about X moves gravity to +Y; about Y it moves gravity to -X.
    sample.accel_roll = atan2f(ay, az);
    sample.accel_pitch = atan2f(-ax, az);
    sample.gyro_roll = gx;
    sample.gyro_pitch = gy;
    return true;
}

// Complementary filter: the gyroscope gives smooth, fast angle changes, and the accelerometer
// slowly pulls the result back to gravity so the gyro's drift cannot accumulate.
bool update_imu()
{
    if (!imu.ready) return false;

    ImuSample sample;
    if (!read_imu_sample(sample)) return false;

    const int64_t now = esp_timer_get_time();
    if (!imu.have_angles) {
        imu.roll = sample.accel_roll;
        imu.pitch = sample.accel_pitch;
        imu.have_angles = true;
        imu.last_us = now;
        return true;
    }

    const float dt = std::clamp((now - imu.last_us) * 1e-6f, 0.001f, 0.1f);
    imu.last_us = now;

    float gain = dt / (FUSION_TIME_CONSTANT + dt);
    // While the board is being shaken the accelerometer is not measuring gravity alone
    if (fabsf(sample.accel_g - 1.0f) > 0.25f) gain = 0;

    const float roll_rate = sample.gyro_roll - imu.gyro_bias[0];
    const float pitch_rate = sample.gyro_pitch - imu.gyro_bias[1];
    imu.roll += roll_rate * dt;
    imu.pitch += pitch_rate * dt;
    imu.roll += (sample.accel_roll - imu.roll) * gain;
    imu.pitch += (sample.accel_pitch - imu.pitch) * gain;
    return true;
}

float wrapped_angle(float angle)
{
    while (angle > PI) angle -= TWO_PI;
    while (angle < -PI) angle += TWO_PI;
    return angle;
}

float control_axis(float delta)
{
    float value = wrapped_angle(delta) / TILT_RANGE_RAD;
    value = std::clamp(value, -1.0f, 1.0f);
    if (fabsf(value) < TILT_DEAD_ZONE) return 0;
    return (value - copysignf(TILT_DEAD_ZONE, value)) /
           (1.0f - TILT_DEAD_ZONE);
}

void init_imu()
{
    for (uint8_t address : IMU_ADDRESSES) {
        i2c_device_config_t device = {};
        device.dev_addr_length = I2C_ADDR_BIT_LEN_7;
        device.device_address = address;
        device.scl_speed_hz = 200000;
        i2c_master_dev_handle_t candidate = nullptr;
        if (i2c_master_bus_add_device(
                touch_bus, &device, &candidate) != ESP_OK) {
            continue;
        }
        imu_device = candidate;
        uint8_t who_am_i = 0;
        if (imu_read_registers(IMU_WHO_AM_I, &who_am_i, 1) == ESP_OK &&
            who_am_i == 0x05) {
            imu.ready = true;
            break;
        }
        i2c_master_bus_rm_device(candidate);
        imu_device = nullptr;
    }

    if (!imu.ready) {
        ESP_LOGW(TAG, "QMI8658 not found; flight remains on autopilot");
        return;
    }

    // Soft reset, then: address auto-increment (needed to read all axes in one burst) with
    // little-endian data, accel +-8 g and gyro +-1024 deg/s both at ~224 Hz, both sensors on.
    imu_write_register(IMU_RESET, 0xb0);
    vTaskDelay(pdMS_TO_TICKS(30));
    uint8_t reset_result = 0;
    imu_read_registers(IMU_RESET_RESULT, &reset_result, 1);
    if (reset_result != 0x80) {
        ESP_LOGW(TAG, "QMI8658 reset result 0x%02x", reset_result);
    }
    ESP_ERROR_CHECK(imu_write_register(IMU_CTRL1, 0x40));
    ESP_ERROR_CHECK(imu_write_register(IMU_CTRL2, 0x25));
    ESP_ERROR_CHECK(imu_write_register(IMU_CTRL3, 0x65));
    ESP_ERROR_CHECK(imu_write_register(IMU_CTRL7, 0x03));
    vTaskDelay(pdMS_TO_TICKS(50));
    ESP_LOGI(TAG, "QMI8658 tilt controls initialized");
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
    init_imu();

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

bool board_calibrate_flight_controls()
{
    if (!imu.ready) return false;

    // Wait (up to 4 s) for a window in which the board is held still, and take the neutral
    // orientation and the gyroscope's zero-rate offset from it
    constexpr int WINDOW = 40;
    constexpr float STILL_RATE = 0.08f;   // rad/s
    imu.have_angles = false;
    imu.gyro_bias[0] = imu.gyro_bias[1] = 0;

    float rate_sum[2] = {0, 0};
    float roll_sum = 0, pitch_sum = 0;
    int count = 0;
    bool settled = false;
    for (int attempt = 0; attempt < 400 && !settled; ++attempt) {
        ImuSample sample;
        if (!read_imu_sample(sample) || !update_imu()) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        if (fabsf(sample.gyro_roll - rate_sum[0] / max(count, 1)) > STILL_RATE ||
            fabsf(sample.gyro_pitch - rate_sum[1] / max(count, 1)) > STILL_RATE ||
            fabsf(sample.gyro_roll) > 4 * STILL_RATE ||
            fabsf(sample.gyro_pitch) > 4 * STILL_RATE) {
            count = 0;
            rate_sum[0] = rate_sum[1] = roll_sum = pitch_sum = 0;
            imu.have_angles = false;
        } else {
            rate_sum[0] += sample.gyro_roll;
            rate_sum[1] += sample.gyro_pitch;
            roll_sum += sample.accel_roll;
            pitch_sum += sample.accel_pitch;
            ++count;
            settled = count >= WINDOW;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (count == 0) return false;

    imu.gyro_bias[0] = rate_sum[0] / count;
    imu.gyro_bias[1] = rate_sum[1] / count;
    imu.neutral_roll = roll_sum / count;
    imu.neutral_pitch = pitch_sum / count;
    imu.roll = imu.neutral_roll;
    imu.pitch = imu.neutral_pitch;
    imu.have_angles = true;
    imu.last_us = esp_timer_get_time();
    imu.steer = imu.climb = 0;
    imu.manual_hold = 0;
    ESP_LOGI(TAG, "Flight controls calibrated%s (gyro bias %.3f/%.3f rad/s)",
             settled ? "" : " (board was not still)",
             imu.gyro_bias[0], imu.gyro_bias[1]);
    return true;
}

// steer: -1 (left) .. 1 (right); climb: -1 (descend) .. 1 (climb).
// Returns true while the player is flying; false hands the plane back to the autopilot.
bool board_flight_controls(float &steer, float &climb)
{
    steer = 0;
    climb = 0;
    if (!imu.ready) return false;

    // BOOT re-centres the controls on however the board is held right now
    const bool button_down =
        gpio_get_level(static_cast<gpio_num_t>(BOOT_BUTTON)) == 0;
    if (button_down && !imu.button_was_down && imu.have_angles) {
        imu.neutral_roll = imu.roll;
        imu.neutral_pitch = imu.pitch;
        imu.steer = imu.climb = 0;
    }
    imu.button_was_down = button_down;

    if (update_imu()) {
        const float raw_steer =
            control_axis(imu.roll - imu.neutral_roll) * (INVERT_STEER ? -1 : 1);
        const float raw_climb =
            control_axis(imu.pitch - imu.neutral_pitch) * (INVERT_CLIMB ? -1 : 1);
        imu.steer += (raw_steer - imu.steer) * 0.3f;
        imu.climb += (raw_climb - imu.climb) * 0.3f;
        if (fabsf(imu.steer) > 0.05f || fabsf(imu.climb) > 0.05f) {
            imu.manual_hold = MANUAL_HOLD_FRAMES;
        } else if (imu.manual_hold > 0) {
            --imu.manual_hold;
        }
        if (DEBUG_TILT) {
            ESP_LOGI(TAG, "roll %.1f pitch %.1f deg  steer %.2f climb %.2f",
                     (imu.roll - imu.neutral_roll) / DEG_TO_RAD_F,
                     (imu.pitch - imu.neutral_pitch) / DEG_TO_RAD_F,
                     imu.steer, imu.climb);
        }
    } else if (imu.manual_hold > 0) {
        --imu.manual_hold;
    }

    steer = imu.steer;
    climb = imu.climb;
    return imu.manual_hold > 0;
}
