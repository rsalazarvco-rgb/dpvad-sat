# Componentes físicos y lógicos

Configuración consolidada documentada en la versión V6:

| Componente | Función principal | Interfaz |
|---|---|---|
| ESP32 DevKit V1 | Procesamiento central, conectividad Wi‑Fi y administración de buses | ADC, I²C, SPI, GPIO |
| DHT22 | Temperatura y humedad relativa | Digital |
| HC-SR04 | Distancia y representación experimental de nivel | TRIG–ECHO |
| MQ-9 | Respuesta relativa a gases combustibles | ADC |
| MPU-9250 | Acelerómetro, giroscopio y magnetómetro | I²C |
| BMP/BME280 | Presión atmosférica, temperatura y altitud estimada | I²C |
| LCD 20 × 4 | Visualización local de variables y estado | I²C |
| Módulo microSD | Persistencia en archivos CSV diarios | SPI |
| RAM | Búfer temporal de contingencia | Memoria interna |
| Wi‑Fi | Sincronización temporal, telemetría y servicios locales | IEEE 802.11 |
| ThingSpeak | Recepción y visualización remota | HTTP/API |

La arquitectura final utiliza el ESP32 como nodo de borde para adquisición, persistencia, servicios locales y telemetría.
