# Arquitectura técnica

## Nodo de borde

El **ESP32 DevKit V1** actúa como unidad central de adquisición, procesamiento, persistencia, conectividad y servicios locales.

## Componentes

| Componente | Función | Interfaz |
|---|---|---|
| ESP32 DevKit V1 | Procesamiento central y Wi‑Fi | ADC, I²C, SPI, GPIO |
| DHT22 | Temperatura y humedad | Digital |
| HC-SR04 | Distancia / representación experimental de nivel | TRIG–ECHO |
| MQ-9 | Respuesta relativa a gases combustibles | ADC |
| MPU-9250 | Aceleración, velocidad angular e inclinación | I²C |
| BMP/BME280 | Presión, temperatura y altitud estimada | I²C |
| LCD 20 × 4 | Visualización local | I²C |
| microSD | Persistencia local CSV | SPI |
| RAM | Contingencia temporal | Memoria interna |
| Wi‑Fi | Sincronización, telemetría y servicios | IEEE 802.11 |
| ThingSpeak | Recepción y visualización remota | HTTP/API |

## Separación adquisición/transmisión

El diseño desacopla la generación del dato del estado del servicio remoto:

- **ONLINE:** el sistema adquiere, persiste y publica.
- **OFFLINE:** mantiene adquisición y persistencia local.
- **BACKFILL:** procesa y envía registros pendientes cuando se restablece el servicio remoto.

Esta separación implementa los principios offline-first y store-and-forward.

## Servicios locales

- **Puerto 80:** consulta de archivos de datos, información estructurada y descarga de CSV almacenados en microSD.
- **Puerto 8080:** estado operativo, firmware, conectividad, pendientes, lotes, eventos y logs de sistema.

Consulte las Figuras 16, 17, 18, 31, 33 y 34 en `docs/figures/`.
