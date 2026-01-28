#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h" 
#include "esp_http_client.h"
#include <esp_http_server.h>

#include "mqtt_client.h"
#include "esp_crt_bundle.h" 
#include "esp_timer.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "cJSON.h" 
#include "rom/ets_sys.h"

#include "bme68x.h"
#include "ssd1306.h"

#include "esp_https_ota.h"
#include "esp_ota_ops.h"




#define TAG "MUSHROOM_IOT"

#define BOTON_PULSADO_ES 1 

#define PIN_VENTILADOR     26   
#define PIN_HUMIDIFICADOR  27

#define TB_BROKER_URI      "mqtt://demo.thingsboard.io"
#define TB_ACCESS_TOKEN    "a08e1dncysa8fky6xive" 
#define TELEGRAM_TOKEN     "8531142504:AAHamh-FsSlT65B9_0uMU9LtF4492xxAj3s" 
#define TELEGRAM_CHAT_ID   "476420106"        

#define OTA_URL "http://192.168.1.129:9000/invernaderoSBC.bin"



#define APP_VERSION "v4.0.5" // pa test


#define I2C_PORT            I2C_NUM_0
#define PIN_SDA             21
#define PIN_SCL             22
#define PIN_BOTON           4

#define I2C_FREQ_HZ         100000
#define BME_ADDR_LOW        0x76
#define BME_ADDR_HIGH       0x77
#define OLED_ADDR           0x3C


char current_ssid[32] = {0};
char current_pass[64] = {0};

void start_ap_mode(void);
esp_err_t load_wifi_credentials(void);


typedef struct {
    char nombre[16];
    float temp_min;
    float temp_max;
    float hum_min;
    float hum_max;
} FaseCultivo;

FaseCultivo fase_germinacion = {"Germinacion", 24.0, 28.0, 60.0, 70.0};
FaseCultivo fase_fructificacion = {"Fructificacion", 18.0, 23.0, 90.0, 95.0};

FaseCultivo *fase_actual = &fase_germinacion; 
bool modo_automatico = true; 

int8_t guardado_fan_state = 0; 
int8_t guardado_hum_state = 0; 

i2c_master_bus_handle_t bus_handle;
i2c_master_dev_handle_t bme_dev_handle;
i2c_master_dev_handle_t oled_dev_handle = NULL;
struct bme68x_dev bme;
SSD1306_t oled;
bool oled_detectada = false;

static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0
esp_mqtt_client_handle_t mqtt_client = NULL;
bool mqtt_connected = false;
static int last_update_id = 0; 

int8_t bme_i2c_read(uint8_t reg_addr, uint8_t *reg_data, uint32_t len, void *intf_ptr);
int8_t bme_i2c_write(uint8_t reg_addr, const uint8_t *reg_data, uint32_t len, void *intf_ptr);
void bme_delay_us(uint32_t period, void *intf_ptr);
void telegram_send_message_to(const char *chat_id, const char *text);

void guardar_estado_nvs() {
    nvs_handle_t my_handle;
    esp_err_t err = nvs_open("storage", NVS_READWRITE, &my_handle);
    if (err == ESP_OK) {
        int8_t id_fase = (fase_actual == &fase_germinacion) ? 0 : 1;
        int8_t id_modo = modo_automatico ? 1 : 0;
        int8_t st_fan = gpio_get_level(PIN_VENTILADOR);
        int8_t st_hum = gpio_get_level(PIN_HUMIDIFICADOR);

        nvs_set_i8(my_handle, "fase_id", id_fase);
        nvs_set_i8(my_handle, "modo_id", id_modo);
        nvs_set_i8(my_handle, "fan_st", st_fan);
        nvs_set_i8(my_handle, "hum_st", st_hum);
        
        nvs_commit(my_handle);
        nvs_close(my_handle);
        ESP_LOGI(TAG, "💾 Guardado NVS: Fase %d, Modo %d", id_fase, id_modo);
    }
}

void cargar_estado_nvs() {
    nvs_handle_t my_handle;
    esp_err_t err = nvs_open("storage", NVS_READONLY, &my_handle);
    if (err == ESP_OK) {
        int8_t id_fase = 0;
        int8_t id_modo = 1;
        int8_t st_fan = 0;
        int8_t st_hum = 0;

        nvs_get_i8(my_handle, "fase_id", &id_fase);
        nvs_get_i8(my_handle, "modo_id", &id_modo);
        nvs_get_i8(my_handle, "fan_st", &st_fan);
        nvs_get_i8(my_handle, "hum_st", &st_hum);

        if (id_fase == 1) fase_actual = &fase_fructificacion;
        else fase_actual = &fase_germinacion;

        modo_automatico = (id_modo == 1);
        guardado_fan_state = st_fan;
        guardado_hum_state = st_hum;

        nvs_close(my_handle);
        ESP_LOGI(TAG, "📂 Cargado NVS: Fase %s, Modo %s", 
                 fase_actual->nombre, modo_automatico ? "AUTO" : "MANUAL");
    } else {
        ESP_LOGW(TAG, "Primer inicio (Sin datos NVS)");
    }
}

void oled_set_power(bool on) {
    if (!oled_detectada || oled_dev_handle == NULL) return;
    uint8_t cmd = on ? 0xAF : 0xAE;
    uint8_t data[] = {0x00, cmd}; 
    i2c_master_transmit(oled_dev_handle, data, sizeof(data), -1);
}

static void init_i2c_bus(void) {
    i2c_master_bus_config_t i2c_bus_conf = {
        .clk_source = I2C_CLK_SRC_DEFAULT, .i2c_port = I2C_PORT,
        .scl_io_num = PIN_SCL, .sda_io_num = PIN_SDA,
        .glitch_ignore_cnt = 7, .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_conf, &bus_handle));
}

static void init_oled_device(void) {
    if (i2c_master_probe(bus_handle, OLED_ADDR, 50) != ESP_OK) {
        oled_detectada = false; return;
    }
    i2c_device_config_t oled_conf = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = OLED_ADDR, .scl_speed_hz = I2C_FREQ_HZ,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus_handle, &oled_conf, &oled_dev_handle));
    oled._i2c_dev_handle = oled_dev_handle;
    oled._address = OLED_ADDR; oled._flip = false;
    ssd1306_init(&oled, 128, 64);
    oled_set_power(true); 
    ssd1306_clear_screen(&oled, false);
    ssd1306_display_text(&oled, 0, "Iniciando...", 12, false);
    oled_detectada = true;
}

static bool init_bme_device(void) {
    uint8_t addr_found = 0;
    if (i2c_master_probe(bus_handle, BME_ADDR_LOW, 50) == ESP_OK) addr_found = BME_ADDR_LOW;
    else if (i2c_master_probe(bus_handle, BME_ADDR_HIGH, 50) == ESP_OK) addr_found = BME_ADDR_HIGH;
    if (addr_found == 0) return false;
    i2c_device_config_t bme_dev_conf = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = addr_found, .scl_speed_hz = I2C_FREQ_HZ,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus_handle, &bme_dev_conf, &bme_dev_handle));
    return true;
}

static void init_gpio(void) {
    gpio_reset_pin(PIN_VENTILADOR);
    gpio_set_direction(PIN_VENTILADOR, GPIO_MODE_INPUT_OUTPUT); 
    gpio_set_level(PIN_VENTILADOR, 0); 

    gpio_reset_pin(PIN_HUMIDIFICADOR);
    gpio_set_direction(PIN_HUMIDIFICADOR, GPIO_MODE_INPUT_OUTPUT);
    gpio_set_level(PIN_HUMIDIFICADOR, 0); 

    gpio_config_t btn_conf = {
        .pin_bit_mask = (1ULL << PIN_BOTON),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = 0,      
        .pull_down_en = 1,    
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&btn_conf);
}

static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) esp_wifi_connect();
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        esp_wifi_connect(); xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
}

esp_err_t save_wifi_credentials(const char *ssid, const char *pass) {
    nvs_handle_t my_handle;
    esp_err_t err = nvs_open("storage", NVS_READWRITE, &my_handle);
    if (err == ESP_OK) {
        nvs_set_str(my_handle, "w_ssid", ssid);
        nvs_set_str(my_handle, "w_pass", pass);
        nvs_commit(my_handle);
        nvs_close(my_handle);
        return ESP_OK;
    }
    return err;
}

esp_err_t load_wifi_credentials(void) {
    nvs_handle_t my_handle;
    size_t ssid_len = sizeof(current_ssid);
    size_t pass_len = sizeof(current_pass);
    esp_err_t err = nvs_open("storage", NVS_READONLY, &my_handle);
    if (err == ESP_OK) {
        if(nvs_get_str(my_handle, "w_ssid", current_ssid, &ssid_len) == ESP_OK &&
           nvs_get_str(my_handle, "w_pass", current_pass, &pass_len) == ESP_OK) {
            nvs_close(my_handle);
            return ESP_OK;
        }
        nvs_close(my_handle);
    }
    return ESP_FAIL;
}

static esp_err_t save_post_handler(httpd_req_t *req) {
    char buf[150];
    int ret = httpd_req_recv(req, buf, req->content_len);
    if (ret <= 0) return ESP_FAIL;
    buf[ret] = '\0';

    char ssid[32] = {0}, pass[64] = {0};
    if (httpd_query_key_value(buf, "ssid", ssid, sizeof(ssid)) == ESP_OK &&
        httpd_query_key_value(buf, "pwd", pass, sizeof(pass)) == ESP_OK) {
        
        save_wifi_credentials(ssid, pass);
        httpd_resp_send(req, "<h1>Guardado! Reiniciando...</h1>", -1);
        vTaskDelay(pdMS_TO_TICKS(2000));
        esp_restart(); 
    }
    return ESP_OK;
}

static esp_err_t config_get_handler(httpd_req_t *req) {
    char html[1024] = "<html><body><h1>Configurar WiFi SBC</h1>"
                      "<form action='/save' method='post'>"
                      "SSID: <input type='text' name='ssid'><br>"
                      "Pass: <input type='password' name='pwd'><br>"
                      "<input type='submit' value='Guardar'></form></body></html>";
    httpd_resp_send(req, html, -1);
    return ESP_OK;
}

void start_ap_mode(void) {
    ESP_LOGI(TAG, "Iniciando Modo Punto de Acceso (AP)");
    
    if(oled_detectada) {
        ssd1306_clear_screen(&oled, false);
        ssd1306_display_text(&oled, 0, "MODO CONFIG", 11, false);
        ssd1306_display_text(&oled, 2, "WIFI: ESP32-SBC", 15, false);
        ssd1306_display_text(&oled, 4, "IP: 192.168.4.1", 15, false);
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg); 

    esp_netif_create_default_wifi_ap();
    
    wifi_config_t wifi_config = {
        .ap = { .ssid = "ESP32-SBC-Config", .password = "", .max_connection = 4, .authmode = WIFI_AUTH_OPEN }
    };
    
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_uri_t uri_get = { .uri = "/", .method = HTTP_GET, .handler = config_get_handler };
        httpd_register_uri_handler(server, &uri_get);
        httpd_uri_t uri_post = { .uri = "/save", .method = HTTP_POST, .handler = save_post_handler };
        httpd_register_uri_handler(server, &uri_post);
    }
    
    while(1) { vTaskDelay(pdMS_TO_TICKS(1000)); }
}

void wifi_init_sta(void) {
    s_wifi_event_group = xEventGroupCreate();

    esp_netif_create_default_wifi_sta();
    
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, &instance_got_ip));

    wifi_config_t wifi_config = { .sta = { .threshold.authmode = WIFI_AUTH_WPA2_PSK } };
    
    strlcpy((char *)wifi_config.sta.ssid, current_ssid, sizeof(wifi_config.sta.ssid));
    strlcpy((char *)wifi_config.sta.password, current_pass, sizeof(wifi_config.sta.password));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    esp_mqtt_event_handle_t event = event_data;
    if (event->event_id == MQTT_EVENT_CONNECTED) mqtt_connected = true;
    else if (event->event_id == MQTT_EVENT_DISCONNECTED) mqtt_connected = false;
}

static void mqtt_app_start(void) {
    esp_mqtt_client_config_t mqtt_cfg = { .broker.address.uri = TB_BROKER_URI, .credentials.username = TB_ACCESS_TOKEN };
    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(mqtt_client);
}

void send_telemetry_thingsboard(float temp, float hum, float press, float gas) {
    if (!mqtt_connected) return;
    int fase_id = (fase_actual == &fase_germinacion) ? 0 : 1; 
	
	// MODO PRUEBA: FORZAR VALORES PERFECTOS o MALOOOOS
    // Si estás en Germinación (24-28), enviamos 26.
    // Si estás en Fructificación (18-23), enviamos 20.
    //float temp_fake = (fase_id == 0) ? 26.0 : 20.0;
    //float hum_fake = (fase_id == 0) ? 65.0 : 92.0;
	
	//float temp_fake = 50.0; // ¡Hoguera!
    //float hum_fake = 10.0;  // Muy seco
	
    // ---------------------------------------------
	
    char telemetry_json[256]; 
    snprintf(telemetry_json, sizeof(telemetry_json), //cambiar si quieres temp y hum a temp_fake o hum_fake para simular thingsboard
        "{\"temperature\":%.2f,\"humidity\":%.2f,\"pressure\":%.2f,\"gas\":%.0f,\"auto\":%d,\"fan\":%d,\"humid\":%d,\"phase_id\":%d}", 
        temp, hum, press, gas, modo_automatico, gpio_get_level(PIN_VENTILADOR), gpio_get_level(PIN_HUMIDIFICADOR), fase_id);
    esp_mqtt_client_publish(mqtt_client, "v1/devices/me/telemetry", telemetry_json, 0, 1, 0);
}

void telegram_send_message_to(const char *chat_id, const char *text) {
    if (!(xEventGroupGetBits(s_wifi_event_group) & WIFI_CONNECTED_BIT)) return;
    char url[256];
    snprintf(url, sizeof(url), "https://api.telegram.org/bot%s/sendMessage", TELEGRAM_TOKEN);
    char *post_data = malloc(512);
    snprintf(post_data, 512, "{\"chat_id\":\"%s\",\"text\":\"%s\"}", chat_id, text);
    esp_http_client_config_t config = { .url = url, .crt_bundle_attach = esp_crt_bundle_attach };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, post_data, strlen(post_data));
    esp_http_client_perform(client);
    free(post_data);
    esp_http_client_cleanup(client);
}

void ota_task(void *pvParameter) {
    ESP_LOGI(TAG, "Iniciando OTA desde: %s", OTA_URL);
    
    if (oled_detectada) {
        ssd1306_clear_screen(&oled, false);
        ssd1306_display_text(&oled, 0, "ACTUALIZANDO...", 13, false);
    }

    esp_http_client_config_t config = {
        .url = OTA_URL,
        .crt_bundle_attach = NULL, 
        .keep_alive_enable = true,
        .timeout_ms = 30000,    
        .keep_alive_interval = 10, 
    };

    esp_https_ota_config_t ota_config = {
        .http_config = &config,
    };

    esp_err_t ret = esp_https_ota(&ota_config);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "OTA Exitosa. Reiniciando...");
        esp_restart();
    } else {
        ESP_LOGE(TAG, "Fallo OTA");
        if (oled_detectada) {
            ssd1306_display_text(&oled, 2, "Error OTA", 9, false);
        }
    }
    vTaskDelete(NULL);
}

static void telegram_check_updates(void) {
    EventBits_t bits = xEventGroupGetBits(s_wifi_event_group);
    if (!(bits & WIFI_CONNECTED_BIT)) return;

    char url[256];
    snprintf(url, sizeof(url), "https://api.telegram.org/bot%s/getUpdates?timeout=5&offset=%d", TELEGRAM_TOKEN, last_update_id + 1);
    esp_http_client_config_t config = { .url = url, .crt_bundle_attach = esp_crt_bundle_attach, .timeout_ms = 10000 };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_http_client_set_method(client, HTTP_METHOD_GET);

    if (esp_http_client_open(client, 0) == ESP_OK) {
        esp_http_client_fetch_headers(client); 
        int status_code = esp_http_client_get_status_code(client);
        if (status_code == 200) {
            int max_len = 4096;
            char *buffer = malloc(max_len);
            if (buffer) {
                int read_len = esp_http_client_read_response(client, buffer, max_len - 1);
                if (read_len > 0) {
                    buffer[read_len] = 0;
                    cJSON *root = cJSON_Parse(buffer);
                    if (root) {
                        cJSON *result = cJSON_GetObjectItem(root, "result");
                        if (cJSON_IsArray(result)) {
                            int items = cJSON_GetArraySize(result);
                            for (int i = 0; i < items; i++) {
                                cJSON *update = cJSON_GetArrayItem(result, i);
                                cJSON *u_id = cJSON_GetObjectItem(update, "update_id");
                                if (u_id) last_update_id = u_id->valueint;

                                cJSON *msg = cJSON_GetObjectItem(update, "message");
                                if (msg) {
                                    cJSON *text = cJSON_GetObjectItem(msg, "text");
                                    cJSON *chat = cJSON_GetObjectItem(msg, "chat");
                                    
                                    if (text && chat) {
                                        char chat_id_str[32];
                                        snprintf(chat_id_str, 32, "%.0f", cJSON_GetObjectItem(chat, "id")->valuedouble);
                                        ESP_LOGI(TAG, "CMD Telegram: %s", text->valuestring);
                                        bool guardar_cambios = false; 

                                        if (strcmp(text->valuestring, "/status") == 0) {
                                            char resp[256];
                                            snprintf(resp, 256, 
                                                "🍄 ESTADO\nModo: %s\nFase: %s\nLímites T: %.1f-%.1f C\nLímites H: %.1f-%.1f %%\n💨 Vent: %s\n💧 Hum: %s",
                                                modo_automatico ? "AUTO" : "MANUAL",
                                                fase_actual->nombre,
                                                fase_actual->temp_min, fase_actual->temp_max,
                                                fase_actual->hum_min, fase_actual->hum_max,
                                                gpio_get_level(PIN_VENTILADOR) ? "ON" : "OFF",
                                                gpio_get_level(PIN_HUMIDIFICADOR) ? "ON" : "OFF");
                                            telegram_send_message_to(chat_id_str, resp);
                                        }
                                        else if (strcmp(text->valuestring, "/germinacion") == 0) {
                                            fase_actual = &fase_germinacion;
                                            modo_automatico = true;
                                            guardar_cambios = true; 
                                            telegram_send_message_to(chat_id_str, "✅ Fase: GERMINACION (Auto).");
                                        }
                                        else if (strcmp(text->valuestring, "/fructificacion") == 0) {
                                            fase_actual = &fase_fructificacion;
                                            modo_automatico = true;
                                            guardar_cambios = true; 
                                            telegram_send_message_to(chat_id_str, "✅ Fase: FRUCTIFICACION (Auto).");
                                        }
                                        else if (strcmp(text->valuestring, "/auto") == 0) {
                                            modo_automatico = true;
                                            guardar_cambios = true; 
                                            telegram_send_message_to(chat_id_str, "🤖 Modo AUTOMÁTICO.");
                                        }
                                        else if (strcmp(text->valuestring, "/manual") == 0) {
                                            modo_automatico = false;
                                            guardar_cambios = true; 
                                            telegram_send_message_to(chat_id_str, "🛠 Modo MANUAL.");
                                        }
                                        else if (strcmp(text->valuestring, "/encender_ventilador") == 0) {
                                            modo_automatico = false;
                                            gpio_set_level(PIN_VENTILADOR, 1);
                                            guardar_cambios = true; 
                                            telegram_send_message_to(chat_id_str, "Ventilador ON (Manual).");
                                        } 
                                        else if (strcmp(text->valuestring, "/apagar_ventilador") == 0) {
                                            modo_automatico = false;
                                            gpio_set_level(PIN_VENTILADOR, 0);
                                            guardar_cambios = true; 
                                            telegram_send_message_to(chat_id_str, "Ventilador OFF (Manual).");
                                        }
                                        else if (strcmp(text->valuestring, "/encender_humidificador") == 0) {
                                            modo_automatico = false;
                                            gpio_set_level(PIN_HUMIDIFICADOR, 1);
                                            guardar_cambios = true; 
                                            telegram_send_message_to(chat_id_str, "Humidificador ON (Manual).");
                                        } 
                                        else if (strcmp(text->valuestring, "/apagar_humidificador") == 0) {
                                            modo_automatico = false;
                                            gpio_set_level(PIN_HUMIDIFICADOR, 0);
                                            guardar_cambios = true; 
                                            telegram_send_message_to(chat_id_str, "Humidificador OFF (Manual).");
                                        }
										else if (strcmp(text->valuestring, "/actualizar") == 0) {
											telegram_send_message_to(chat_id_str, "Descargando actualización...");
											xTaskCreate(ota_task, "ota_task", 8192, NULL, 5, NULL);
										}
                                        else {
                                            telegram_send_message_to(chat_id_str, "Comando desconocido.");
                                        }

                                        if (guardar_cambios) guardar_estado_nvs();
                                    }
                                }
                            }
                        }
                        cJSON_Delete(root);
                    }
                }
                free(buffer);
            }
        }
    }
    esp_http_client_cleanup(client);
}

static void telegram_task(void *pvParameters) {
    while (1) {
        telegram_check_updates();
        vTaskDelay(pdMS_TO_TICKS(4000)); 
    }
}

void check_auto_control(float temp, float hum) {
    if (!modo_automatico) return; 

    if (temp > fase_actual->temp_max) {
        gpio_set_level(PIN_VENTILADOR, 1);
    }
    else if (temp < (fase_actual->temp_max - 0.5)) {
        gpio_set_level(PIN_VENTILADOR, 0);
    }

    if (hum < fase_actual->hum_min) {
        gpio_set_level(PIN_HUMIDIFICADOR, 1);
    }
    else if (hum > (fase_actual->hum_min + 3.0)) {
        gpio_set_level(PIN_HUMIDIFICADOR, 0);
    }
}



void app_main(void) {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
	
    init_gpio();
    init_i2c_bus();      
    init_oled_device();  

    bool boton_pulsado = (gpio_get_level(PIN_BOTON) == BOTON_PULSADO_ES);
    bool wifi_guardado = (load_wifi_credentials() == ESP_OK);

    if (boton_pulsado || !wifi_guardado) {
        ESP_LOGW(TAG, "Entrando en MODO CONFIGURACION (AP)");
        start_ap_mode(); 
    }

    esp_ota_mark_app_valid_cancel_rollback(); 
    cargar_estado_nvs(); 

    if (!init_bme_device()) {
        if(oled_detectada) ssd1306_display_text(&oled, 0, "Error Sensor", 12, false);
        ESP_LOGE(TAG, "Error BME680 no encontrado");
    }
    
    bme.intf = BME68X_I2C_INTF; bme.read = bme_i2c_read; bme.write = bme_i2c_write;
    bme.delay_us = bme_delay_us; bme.intf_ptr = &bme_dev_handle; bme.amb_temp = 25;
    bme68x_init(&bme);
    struct bme68x_conf conf = { .filter = BME68X_FILTER_OFF, .odr = BME68X_ODR_NONE, .os_hum = BME68X_OS_16X, .os_pres = BME68X_OS_1X, .os_temp = BME68X_OS_2X };
    bme68x_set_conf(&conf, &bme);
    struct bme68x_heatr_conf heatr_conf = { .enable = BME68X_ENABLE, .heatr_temp = 300, .heatr_dur = 100 };
    bme68x_set_heatr_conf(BME68X_FORCED_MODE, &heatr_conf, &bme);

    if (oled_detectada) {
        ssd1306_clear_screen(&oled, false);
        ssd1306_display_text(&oled, 0, "Conectando...", 13, false);
    }
    
    wifi_init_sta(); 

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT, pdFALSE, pdFALSE, pdMS_TO_TICKS(15000));
    
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "✅ WiFi Conectado.");
        mqtt_app_start();
        
        char msg_inicio[128];
		snprintf(msg_inicio, 128, "Sistema Online %s.\n%s | %s", 
			 APP_VERSION, fase_actual->nombre, modo_automatico ? "AUTO" : "MANUAL");
        telegram_send_message_to(TELEGRAM_CHAT_ID, msg_inicio);
        
        xTaskCreate(telegram_task, "telegram_task", 8192, NULL, 5, NULL);
    } else {
        ESP_LOGW(TAG, "⚠️ Offline (Timeout).");
        if (oled_detectada) ssd1306_display_text(&oled, 0, "Modo Offline", 12, false);
        vTaskDelay(pdMS_TO_TICKS(2000));
    }

    struct bme68x_data data;
    uint32_t del_period;
    uint8_t n_fields;
    
    int tick_counter = 0;       
    int timer_pantalla = 0;     
    bool pantalla_fisica_encendida = true; 
    float last_temp = 0.0;
    float last_hum = 0.0;

    if (oled_detectada) {
        ssd1306_clear_screen(&oled, false);
        oled_set_power(false);
        pantalla_fisica_encendida = false;
    }

    while (1) {
        bool despertar_y_leer = false;
        bool refresco_segundo = false;
        bool enviar_nube = false;

        if (gpio_get_level(PIN_BOTON) == BOTON_PULSADO_ES) {
            if (timer_pantalla == 0) despertar_y_leer = true;
            timer_pantalla = 100; 
        }

        tick_counter++;
        if (tick_counter >= 50) { 
            enviar_nube = true;
            if (timer_pantalla == 0) despertar_y_leer = true; 
            tick_counter = 0;
        }
        else if (timer_pantalla > 0 && (tick_counter % 10 == 0)) {
            refresco_segundo = true;
        }

        if (despertar_y_leer || refresco_segundo || (enviar_nube && timer_pantalla > 0)) {
            bme68x_set_op_mode(BME68X_FORCED_MODE, &bme);
            del_period = bme68x_get_meas_dur(BME68X_FORCED_MODE, &conf, &bme) + (heatr_conf.heatr_dur * 1000);
            bme.delay_us(del_period, bme.intf_ptr);
            bme68x_get_data(BME68X_FORCED_MODE, &data, &n_fields, &bme);
            
            if (n_fields) {
                last_temp = data.temperature;
                last_hum = data.humidity;
                
                check_auto_control(last_temp, last_hum);
                
                ESP_LOGI(TAG, "T: %.2f | H: %.2f | Mode: %s | F: %d | H: %d", 
                         last_temp, last_hum, 
                         modo_automatico ? "A" : "M", 
                         gpio_get_level(PIN_VENTILADOR), 
                         gpio_get_level(PIN_HUMIDIFICADOR));
            }
        }

        if (timer_pantalla > 0) {
            timer_pantalla--;
            if (!pantalla_fisica_encendida) {
                oled_set_power(true); 
                vTaskDelay(pdMS_TO_TICKS(10)); 
                pantalla_fisica_encendida = true;
                despertar_y_leer = true; 
            }

            if (oled_detectada && (despertar_y_leer || refresco_segundo)) {
                char linea[20];
                ssd1306_clear_screen(&oled, false);
                sprintf(linea, "%s %s", modo_automatico ? "AUTO" : "MAN", mqtt_connected ? "*" : ".");
                ssd1306_display_text(&oled, 0, linea, strlen(linea), false);
                sprintf(linea, "T: %.1fC H: %.0f%%", last_temp, last_hum);
                ssd1306_display_text(&oled, 2, linea, strlen(linea), false);
                ssd1306_display_text(&oled, 4, fase_actual->nombre, strlen(fase_actual->nombre), false);
                sprintf(linea, "V:%d H:%d", gpio_get_level(PIN_VENTILADOR), gpio_get_level(PIN_HUMIDIFICADOR));
                ssd1306_display_text(&oled, 6, linea, strlen(linea), false);
            }
        } 
        else {
            if (pantalla_fisica_encendida) {
                if (oled_detectada) ssd1306_clear_screen(&oled, false);
                oled_set_power(false);
                pantalla_fisica_encendida = false;
            }
        }

        if (enviar_nube && mqtt_connected && n_fields) {
            send_telemetry_thingsboard(last_temp, last_hum, data.pressure/100.0, data.gas_resistance);
        }

        vTaskDelay(pdMS_TO_TICKS(100)); 
    }
}

int8_t bme_i2c_read(uint8_t reg_addr, uint8_t *reg_data, uint32_t len, void *intf_ptr) {
    i2c_master_dev_handle_t handle = *(i2c_master_dev_handle_t*)intf_ptr;
    esp_err_t err = i2c_master_transmit_receive(handle, &reg_addr, 1, reg_data, len, -1);
    return (err == ESP_OK) ? BME68X_OK : BME68X_E_COM_FAIL;
}
int8_t bme_i2c_write(uint8_t reg_addr, const uint8_t *reg_data, uint32_t len, void *intf_ptr) {
    i2c_master_dev_handle_t handle = *(i2c_master_dev_handle_t*)intf_ptr;
    uint8_t *buffer = malloc(len + 1);
    if (!buffer) return BME68X_E_NULL_PTR;
    buffer[0] = reg_addr; memcpy(buffer + 1, reg_data, len);
    esp_err_t err = i2c_master_transmit(handle, buffer, len + 1, -1);
    free(buffer);
    return (err == ESP_OK) ? BME68X_OK : BME68X_E_COM_FAIL;
}
void bme_delay_us(uint32_t period, void *intf_ptr) {
    if (period >= 10000) vTaskDelay(pdMS_TO_TICKS(period / 1000));
    else ets_delay_us(period); 
}