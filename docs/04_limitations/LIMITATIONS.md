# Limitaciones y condiciones de interpretación

## Datos
- Ventana experimental limitada a ocho días.
- `field7` y `field8` no fueron transmitidos en BACKFILL.
- La identidad individual no se preservó como clave común entre microSD y ThingSpeak.
- `field4` presentó ausencias ONLINE que no pueden explicarse con una única causa.

## Metrología
No se contó con instrumentos patrón certificados para todas las variables. Por tanto:
- no se declara exactitud metrológica absoluta;
- la dispersión estadística no equivale a precisión instrumental certificada;
- `gas_ppm` debe interpretarse como estimación funcional del MQ-9.

## Conectividad
La red Wi‑Fi disponible durante periodos ONLINE fue favorable y controlada. Los resultados de RSSI y publicación no deben generalizarse a cobertura rural real.

## Temporalidad
No existen timestamps comparables de adquisición, inicio de envío y confirmación para la misma observación. Por ello no se calcula latencia extremo a extremo.

## Madurez
El prototipo no fue validado para:
- operación territorial permanente;
- encapsulado industrial;
- autonomía energética prolongada;
- alertamiento institucional;
- seguridad operacional crítica.
