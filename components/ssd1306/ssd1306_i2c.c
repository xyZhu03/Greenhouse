#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h" // Se usa la nueva cabecera del driver I2C
#include "ssd1306.h"

#define tag "SSD1306"

// Define el puerto I2C que se va a utilizar. I2C_NUM_0 es el más común.
#define I2C_MASTER_PORT I2C_NUM_0

/*
 * Función interna para enviar un buffer de comandos al OLED.
 * Encapsula la nueva API de i2c_master.
 */
static esp_err_t ssd1306_i2c_send_cmds(SSD1306_t *dev, const uint8_t *cmds, int len) {
    // Se crea un buffer temporal que incluye el byte de control I2C.
    uint8_t *buffer = malloc(len + 1);
    if (buffer == NULL) {
        ESP_LOGE(tag, "Failed to allocate memory for command buffer");
        return ESP_ERR_NO_MEM;
    }
    
    buffer[0] = OLED_CONTROL_BYTE_CMD_STREAM; // Byte de control para un stream de comandos
    memcpy(buffer + 1, cmds, len);
    
    // Se transmite el buffer completo en una sola transacción.
    esp_err_t ret = i2c_master_transmit(dev->_i2c_dev_handle, buffer, len + 1, -1); // -1 para timeout infinito
    
    free(buffer);
    return ret;
}

/*
 * Función interna para enviar un buffer de datos (píxeles) al OLED.
 */
static esp_err_t ssd1306_i2c_send_data(SSD1306_t *dev, const uint8_t *data, int len) {
    uint8_t *buffer = malloc(len + 1);
     if (buffer == NULL) {
        ESP_LOGE(tag, "Failed to allocate memory for data buffer");
        return ESP_ERR_NO_MEM;
    }

    buffer[0] = OLED_CONTROL_BYTE_DATA_STREAM; // Byte de control para un stream de datos
    memcpy(buffer + 1, data, len);

    esp_err_t ret = i2c_master_transmit(dev->_i2c_dev_handle, buffer, len + 1, -1);

    free(buffer);
    return ret;
}


// --- FUNCIONES PÚBLICAS REESCRITAS CON LA NUEVA API ---

/*
 * Inicialización del bus y del dispositivo I2C (NUEVA VERSIÓN)
 */
void i2c_master_init(SSD1306_t * dev, int16_t sda, int16_t scl, int16_t reset) {
    // 1. Configuración del bus I2C
    i2c_master_bus_config_t i2c_bus_config = {
        .i2c_port = I2C_MASTER_PORT,
        .sda_io_num = sda,
        .scl_io_num = scl,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7, // Filtro de glitches
        .flags.enable_internal_pullup = true,
    };
    i2c_master_bus_handle_t bus_handle;
    ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_config, &bus_handle));

    // 2. Configuración del dispositivo OLED en el bus
    i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = I2CAddress, // Dirección I2C del OLED (definida en ssd1306.h)
        .scl_speed_hz = 400000,        // Frecuencia de reloj a 400 kHz
    };
    // Se añade el dispositivo al bus y se guarda el handle en la estructura dev
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus_handle, &dev_config, &dev->_i2c_dev_handle));

    // 3. Lógica de reset del pin (sin cambios)
    if (reset >= 0) {
        gpio_reset_pin(reset);
        gpio_set_direction(reset, GPIO_MODE_OUTPUT);
        gpio_set_level(reset, 0);
        vTaskDelay(50 / portTICK_PERIOD_MS);
        gpio_set_level(reset, 1);
    }
    dev->_address = I2CAddress;
    dev->_flip = false;
}

/*
 * Envía la secuencia de inicialización al display (NUEVA VERSIÓN)
 */
void i2c_init(SSD1306_t * dev, int width, int height) {
    dev->_width = width;
    dev->_height = height;
    dev->_pages = (height == 32) ? 4 : 8;

    // Array con todos los comandos de inicialización
    uint8_t init_cmds[] = {
        OLED_CMD_DISPLAY_OFF,
        OLED_CMD_SET_MUX_RATIO, (height == 32) ? (uint8_t)0x1F : (uint8_t)0x3F,
        OLED_CMD_SET_DISPLAY_OFFSET, 0x00,
        OLED_CMD_SET_DISPLAY_START_LINE,
        dev->_flip ? OLED_CMD_SET_SEGMENT_REMAP_0 : OLED_CMD_SET_SEGMENT_REMAP_1,
        OLED_CMD_SET_COM_SCAN_MODE,
        OLED_CMD_SET_DISPLAY_CLK_DIV, 0x80,
        OLED_CMD_SET_COM_PIN_MAP, (height == 32) ? (uint8_t)0x02 : (uint8_t)0x12,
        OLED_CMD_SET_CONTRAST, 0xFF,
        OLED_CMD_DISPLAY_RAM,
        OLED_CMD_SET_VCOMH_DESELCT, 0x40,
        OLED_CMD_SET_MEMORY_ADDR_MODE, OLED_CMD_SET_PAGE_ADDR_MODE,
        OLED_CMD_SET_CHARGE_PUMP, 0x14,
        OLED_CMD_DEACTIVE_SCROLL,
        OLED_CMD_DISPLAY_NORMAL,
        OLED_CMD_DISPLAY_ON
    };

    esp_err_t espRc = ssd1306_i2c_send_cmds(dev, init_cmds, sizeof(init_cmds));

    if (espRc == ESP_OK) {
        ESP_LOGI(tag, "OLED configured successfully");
    } else {
        ESP_LOGE(tag, "OLED configuration failed. code: 0x%.2X", espRc);
    }
}

/*
 * Muestra un buffer de píxeles en una posición (NUEVA VERSIÓN)
 */
void i2c_display_image(SSD1306_t * dev, int page, int seg, uint8_t * images, int width) {
    if (page >= dev->_pages || seg >= dev->_width) return;

    // Se ha eliminado CONFIG_OFFSETX para que sea más genérico
    uint8_t columLow = seg & 0x0F;
    uint8_t columHigh = (seg >> 4) & 0x0F;
    int _page = dev->_flip ? (dev->_pages - 1 - page) : page;

    // Comandos para posicionar el cursor de escritura
    uint8_t cmds[] = {
        (uint8_t)(0x00 | columLow),      // Set Lower Column Start Address
        (uint8_t)(0x10 | columHigh),     // Set Higher Column Start Address
        (uint8_t)(0xB0 | _page)         // Set Page Start Address
    };
    
    // Se envían los comandos de posicionamiento y luego los datos de la imagen
    ssd1306_i2c_send_cmds(dev, cmds, sizeof(cmds));
    ssd1306_i2c_send_data(dev, images, width);
}

/*
 * Establece el contraste de la pantalla (NUEVA VERSIÓN)
 */
void i2c_contrast(SSD1306_t * dev, int contrast) {
    int _contrast = (contrast < 0) ? 0 : ((contrast > 0xFF) ? 0xFF : contrast);
    uint8_t cmds[] = { OLED_CMD_SET_CONTRAST, (uint8_t)_contrast };
    ssd1306_i2c_send_cmds(dev, cmds, sizeof(cmds));
}


/*
 * Configura el scroll por hardware (NUEVA VERSIÓN)
 */
void i2c_hardware_scroll(SSD1306_t * dev, ssd1306_scroll_type_t scroll) {
    uint8_t cmd_buf[8]; // Buffer para los comandos de scroll
    int cmd_len = 0;

    switch(scroll) {
        case SCROLL_RIGHT:
            cmd_buf[0] = OLED_CMD_HORIZONTAL_RIGHT; cmd_buf[1] = 0x00; cmd_buf[2] = 0x00;
            cmd_buf[3] = 0x07; cmd_buf[4] = 0x07; cmd_buf[5] = 0x00; cmd_buf[6] = 0xFF;
            cmd_buf[7] = OLED_CMD_ACTIVE_SCROLL;
            cmd_len = 8;
            break;
        case SCROLL_LEFT:
            cmd_buf[0] = OLED_CMD_HORIZONTAL_LEFT; cmd_buf[1] = 0x00; cmd_buf[2] = 0x00;
            cmd_buf[3] = 0x07; cmd_buf[4] = 0x07; cmd_buf[5] = 0x00; cmd_buf[6] = 0xFF;
            cmd_buf[7] = OLED_CMD_ACTIVE_SCROLL;
            cmd_len = 8;
            break;
        case SCROLL_STOP:
            cmd_buf[0] = OLED_CMD_DEACTIVE_SCROLL;
            cmd_len = 1;
            break;
        // Los scrolls verticales son más complejos, se dejan como estaban para simplificar
        // y se pueden refactorizar de la misma manera si se necesitan.
        case SCROLL_DOWN:
        case SCROLL_UP:
            ESP_LOGW(tag, "Vertical scroll not fully refactored in this version.");
            return; // Evita ejecutar código no refactorizado
    }

    if (cmd_len > 0) {
        esp_err_t espRc = ssd1306_i2c_send_cmds(dev, cmd_buf, cmd_len);
        if (espRc == ESP_OK) {
            ESP_LOGD(tag, "Scroll command succeeded");
        } else {
            ESP_LOGE(tag, "Scroll command failed. code: 0x%.2X", espRc);
        }
    }
}