# Interfaces e integraciones

| Interfaz | Asignación general | Elementos | Consideración |
|---|---|---|---|
| I²C | GPIO 21/22 o equivalente | MPU-9250, BMP/BME280, LCD 20 × 4 | Bus compartido |
| SPI | SCK, MISO, MOSI, CS | microSD | Bus dedicado |
| ADC | Entrada analógica | MQ-9 | Filtrado EMA y relación Rs/R0 |
| TRIG–ECHO | Dos GPIO | HC-SR04 | Lectura con timeout |
| Wi‑Fi/HTTP | Inalámbrica | ThingSpeak y servicios locales | Telemetría y consulta local |
| JSON | Endpoint embebido | Último registro y estado | Intercambio estructurado local |

Servicios web:
- Puerto **80**: consulta y descarga de registros almacenados en microSD.
- Puerto **8080**: administración y trazabilidad operativa de solo lectura.
