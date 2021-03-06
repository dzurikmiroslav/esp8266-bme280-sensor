#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "rom/ets_sys.h"
#include "bme280.h"

#include "bme.h"

#define LOG_TAG "BME"

#define I2C_SDA 14
#define I2C_SCL 2
#define I2C_MASTER_NUM I2C_NUM_0
#define I2C_MASTER_ACK 0
#define I2C_MASTER_NACK 1
#define I2C_MASTER_FREQ_HZ 100000

#define WRITE_BIT I2C_MASTER_WRITE /*!< I2C master write */
#define READ_BIT I2C_MASTER_READ   /*!< I2C master read */
#define ACK_CHECK_EN 0x1           /*!< I2C master will check ack from slave*/
#define ACK_CHECK_DIS 0x0          /*!< I2C master will not check ack from slave */
#define ACK_VAL 0x0                /*!< I2C ack value */
#define NACK_VAL 0x1               /*!< I2C nack value */

struct bme280_dev bme280;

int8_t user_i2c_write(uint8_t dev_id, uint8_t reg_addr, uint8_t *reg_data, uint16_t len)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (dev_id << 1) | WRITE_BIT, ACK_CHECK_EN);
    i2c_master_write_byte(cmd, reg_addr, ACK_CHECK_EN);
    i2c_master_write(cmd, reg_data, len, ACK_CHECK_EN);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, 100 / portTICK_RATE_MS);
    i2c_cmd_link_delete(cmd);
    return ret;
}

int8_t user_i2c_read(uint8_t dev_id, uint8_t reg_addr, uint8_t *reg_data, uint16_t len)
{
    if (len == 0) {
        return 0;
    }

    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (dev_id << 1) | WRITE_BIT, ACK_CHECK_EN);
    i2c_master_write_byte(cmd, reg_addr, ACK_CHECK_EN);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, 100 / portTICK_RATE_MS);
    if (ret != ESP_OK) {
        return ret;
    }
    i2c_cmd_link_delete(cmd);

    cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (dev_id << 1) | READ_BIT, ACK_CHECK_EN);
    if (len > 1) {
        i2c_master_read(cmd, reg_data, len - 1, ACK_VAL);
    }
    i2c_master_read_byte(cmd, reg_data + len - 1, NACK_VAL);
    i2c_master_stop(cmd);
    ret = i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, 100 / portTICK_RATE_MS);
    i2c_cmd_link_delete(cmd);
    return ret;
}

void user_delay_ms(uint32_t period)
{
	ets_delay_us(period * 1000);
}

void init_i2c()
{
    int i2c_master_port = I2C_MASTER_NUM;
    i2c_config_t conf;
    conf.mode = I2C_MODE_MASTER;
    conf.sda_io_num = I2C_SDA;
    conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
    conf.scl_io_num = I2C_SCL;
    conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
    ESP_ERROR_CHECK(i2c_driver_install(i2c_master_port, conf.mode));
    ESP_ERROR_CHECK(i2c_param_config(i2c_master_port, &conf));
}

void bme_init()
{
    init_i2c();

    bme280.write = user_i2c_write;
    bme280.read = user_i2c_read;
    bme280.delay_ms = user_delay_ms;
    bme280.intf = BME280_I2C_INTF;
    bme280.dev_id = BME280_I2C_ADDR_PRIM;
    ESP_ERROR_CHECK(bme280_init(&bme280));

    bme280.settings.osr_h = BME280_OVERSAMPLING_1X;
    bme280.settings.osr_p = BME280_OVERSAMPLING_16X;
    bme280.settings.osr_t = BME280_OVERSAMPLING_2X;
    bme280.settings.filter = BME280_FILTER_COEFF_16;
    uint8_t settings_sel = BME280_OSR_PRESS_SEL | BME280_OSR_TEMP_SEL | BME280_OSR_HUM_SEL | BME280_FILTER_SEL;
    ESP_ERROR_CHECK(bme280_set_sensor_settings(settings_sel, &bme280));
}

bme_data_t bme_read()
{
    bme_data_t ret;
    struct bme280_data data;

    ESP_ERROR_CHECK(bme280_set_sensor_mode(BME280_FORCED_MODE, &bme280));
    vTaskDelay(70 / portTICK_PERIOD_MS);
    ESP_ERROR_CHECK(bme280_get_sensor_data(BME280_ALL, &data, &bme280));

    ret.humidity = (data.humidity / 1024.0f) * 100;
    ret.pressure = data.pressure;
    ret.temperature = data.temperature;

    ESP_LOGD(LOG_TAG, "Pressure %d *0.01 Pa", data.pressure);
    ESP_LOGD(LOG_TAG, "Temperature %d *0.01 °C", data.temperature);
    ESP_LOGD(LOG_TAG, "Humidity %d *0.01 %%", data.humidity);

    return ret;
}
