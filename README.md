# Sistema IoT para Cultivo de Setas

Controlador automatizado basado en ESP32 para la gestión de temperatura y humedad en entornos de cultivo micológico. El sistema regula actuadores externos basándose en fases de cultivo predefinidas y permite el control remoto.

## Funcionalidades

- **Control Automático:** Regulación de temperatura y humedad mediante ventilador y humidificador con lógica de histéresis.
- **Perfiles de Cultivo:**
  - Germinación (24-28 C, 60-70% Humedad).
  - Fructificación (18-23 C, 90-95% Humedad).
- **Control Remoto vía Telegram:** Recepción de estado y comandos (/status, /auto, /manual, cambios de fase).
- **Telemetría:** Envío de datos a ThingsBoard mediante MQTT.
- **Interfaz Local:** Pantalla OLED SSD1306 con temporizador de apagado automático y activación por botón táctil.
- **Persistencia:** Guardado de estado (fase y modo) en memoria NVS para recuperación tras cortes de luz.

## Hardware Requerido

- ESP32.
- Sensor BME680 (Temperatura, Humedad, Presión, Gas).
- Pantalla OLED I2C (SSD1306).
- Módulo de botón táctil o pulsador (GPIO 4).
- Relés o MOSFETs para Ventilador (GPIO 26) y Humidificador (GPIO 27).

## Configuración

Antes de compilar, edita las definiciones al inicio del archivo principal con tus credenciales:

- `ESP_WIFI_SSID` / `ESP_WIFI_PASS`: Credenciales WiFi.
- `TB_ACCESS_TOKEN`: Token del dispositivo en ThingsBoard.
- `TELEGRAM_TOKEN`: Token del Bot de Telegram.
- `TELEGRAM_CHAT_ID`: ID de chat autorizado.

## Comandos de Telegram

El bot acepta los siguientes comandos:

- `/status`: Muestra lecturas actuales, modo y estado de actuadores.
- `/germinacion`: Cambia el perfil a Germinación y activa modo Auto.
- `/fructificacion`: Cambia el perfil a Fructificación y activa modo Auto.
- `/auto`: Activa el control automático.
- `/manual`: Pasa a control manual.
- `/actualizar`: Actualizar el sistema vía OTA.
- `/encender_ventilador` / `/apagar_ventilador`: Control manual del ventilador.
- `/encender_humidificador` / `/apagar_humidificador`: Control manual del humidificador.

## Compilación e Instalación

Este proyecto utiliza el framework ESP-IDF.

1. Clonar el repositorio.
2. Configurar el entorno de ESP-IDF.
3. Compilar y flashear:

```bash
idf.py build
idf.py -p (PUERTO) flash monitor
```
