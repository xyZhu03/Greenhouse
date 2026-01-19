#include <stdio.h>
#include "bme68x.h"
#include "bme68x_defs.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/i2c.h"

// Definición de pines para la comunicación I2C
#define SDA_PIN 21
#define SCL_PIN 22

// Configuración de la interfaz I2C
#define I2C_MASTER_NUM I2C_NUM_0
#define I2C_SPEED 100000  // Velocidad en Hz (100kHz)

static struct bme68x_dev bme_dev;
struct bme68x_data sensor_data;

// Función para inicializar I2C
esp_err_t i2c_master_init(void) {
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = SDA_PIN,
        .scl_io_num = SCL_PIN,
        .master.clk_speed = I2C_SPEED  // Usamos clk_speed
    };
    esp_err_t ret = i2c_param_config(I2C_MASTER_NUM, &conf);
    if (ret != ESP_OK) {
        return ret;
    }
    return i2c_driver_install(I2C_MASTER_NUM, conf.mode, 0, 0, 0);
}

// Función de retardo compatible con bme68x_delay_us_fptr_t
void bme68x_delay_us(uint32_t period, void *intf_ptr) {
    vTaskDelay(period / portTICK_PERIOD_MS);  // Convertir microsegundos a milisegundos
}

// Función de lectura adaptada
int8_t bme68x_i2c_read(uint8_t reg_addr, uint8_t *reg_data, long unsigned int len, void *dev) {
    esp_err_t ret;
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    
    // Iniciar una transacción I2C para leer los datos
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (BME68X_I2C_INTF<<1) | I2C_MASTER_WRITE, true); // Dirección de I2C con la operación de escritura
    i2c_master_write_byte(cmd, reg_addr, true);  // Dirección del registro
    i2c_master_start(cmd);  // Repetir la condición de inicio
    i2c_master_write_byte(cmd, (BME68X_I2C_INTF<<1) | I2C_MASTER_READ, true); // Dirección de I2C con la operación de lectura
    i2c_master_read(cmd, reg_data, len, I2C_MASTER_LAST_NACK);
    i2c_master_stop(cmd);  // Detener la transacción I2C
    
    ret = i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, pdMS_TO_TICKS(1000));  // Ejecutar el comando
    i2c_cmd_link_delete(cmd);  // Liberar el comando

    if (ret != ESP_OK) {
        ESP_LOGE("BME680", "Error al leer datos de I2C");
        return -1;  // Indicar error
    }
    return 0;  // Éxito
}

// Función de escritura adaptada
int8_t bme68x_i2c_write(uint8_t reg_addr, const uint8_t *reg_data, long unsigned int len, void *dev) {
    esp_err_t ret;
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();

    // Iniciar una transacción I2C para escribir los datos
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (BME68X_I2C_INTF<<1) | I2C_MASTER_WRITE, true); // Dirección de I2C con la operación de escritura
    i2c_master_write_byte(cmd, reg_addr, true);  // Dirección del registro
    i2c_master_write(cmd, reg_data, len, true); // Escribir los datos
    i2c_master_stop(cmd);  // Detener la transacción I2C

    ret = i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, pdMS_TO_TICKS(1000));  // Ejecutar el comando
    i2c_cmd_link_delete(cmd);  // Liberar el comando

    if (ret != ESP_OK) {
        ESP_LOGE("BME680", "Error al escribir datos en I2C");
        return -1;  // Indicar error
    }
    return 0;  // Éxito
}

// Inicialización del sensor BME680
int8_t init_bme680(void) {
    int8_t res;

    // Configuración del dispositivo BME680
    bme_dev.intf = BME68X_I2C_INTF;  // Asignamos la interfaz I2C del sensor
    bme_dev.read = bme68x_i2c_read;  // Asignamos la función de lectura de I2C
    bme_dev.write = bme68x_i2c_write; // Asignamos la función de escritura de I2C
    bme_dev.delay_us = bme68x_delay_us;  // Usamos la función de retardo personalizada

    res = bme68x_init(&bme_dev);
    if (res != BME68X_OK) {
        ESP_LOGE("BME680", "Error al inicializar el sensor BME680");
        return res;
    }

    // Establecer configuración de medición del sensor
    struct bme68x_conf conf;
    res = bme68x_get_conf(&conf, &bme_dev);
    if (res != BME68X_OK) {
        ESP_LOGE("BME680", "Error al obtener configuración del sensor");
        return res;
    }

    conf.os_hum = BME68X_OS_2X;
    conf.os_pres = BME68X_OS_4X;
    conf.os_temp = BME68X_OS_8X;
    conf.filter = BME68X_FILTER_SIZE_3;

    res = bme68x_set_conf(&conf, &bme_dev);
    if (res != BME68X_OK) {
        ESP_LOGE("BME680", "Error al configurar el sensor");
        return res;
    }

    return BME68X_OK;
}

// Función para leer datos del sensor
int8_t read_bme680_data(void) {
    uint8_t n_data = 1;  // Número de datos a leer
    int8_t res = bme68x_get_data(BME68X_FORCED_MODE, &sensor_data, &n_data, &bme_dev);
    if (res != BME68X_OK) {
        ESP_LOGE("BME680", "Error al leer los datos del sensor");
        return res;
    }

    ESP_LOGI("BME680", "Temperatura: %.2f°C", sensor_data.temperature / 100.0);
    ESP_LOGI("BME680", "Humedad: %.2f%%", sensor_data.humidity / 1000.0);
    ESP_LOGI("BME680", "Presión: %.2f hPa", sensor_data.pressure / 100.0);

    return BME68X_OK;
}


void app_main(void) {
    // Inicializar I2C
    esp_err_t ret = i2c_master_init();
    if (ret != ESP_OK) {
        ESP_LOGE("I2C", "Error al inicializar I2C: %d", ret);
        return;
    }
    ESP_LOGI("I2C", "I2C inicializado correctamente");

    // Inicializar el BME680
    if (init_bme680() == BME68X_OK) {
        ESP_LOGI("BME680", "Sensor BME680 inicializado correctamente");
    }

    // Leer datos del sensor en un bucle
    while (1) {
        if (read_bme680_data() == BME68X_OK) {
            vTaskDelay(2000 / portTICK_PERIOD_MS);  // Leer cada 2 segundos
        } else {
            ESP_LOGE("BME680", "Error al leer datos");
        }
    }
}
