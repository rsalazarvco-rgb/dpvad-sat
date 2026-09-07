# Firmware preliminar

El directorio `source/` contiene una **versión preliminar sanitizada** del firmware recuperada de los materiales del proyecto.

## Importante

El archivo publicado corresponde a un candidato **V12.5 SYNC RC1** y se incluye como material académico preliminar. No se afirma que sea una copia bit a bit de la versión final utilizada durante toda la ventana experimental.

Antes de incorporarlo al repositorio público se sustituyeron:
- SSID y contraseña Wi‑Fi;
- contraseña del AP;
- credenciales de los portales web;
- clave de escritura de ThingSpeak.

## Arquitectura documentada

La versión V6 describe una máquina de estados no bloqueante con:
- adquisición multisensorial;
- persistencia diaria en microSD;
- ONLINE;
- OFFLINE;
- BACKFILL;
- servidor web;
- JSON;
- logging y trazabilidad;
- contingencia en RAM.

Consulte `docs/figures/figure_31_flujo_operativo_firmware.png`.
