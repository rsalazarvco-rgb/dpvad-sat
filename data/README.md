# Datos experimentales

Este repositorio incluye los registros locales y logs operativos recuperados de los materiales del proyecto.

## Fuentes

### `raw/local/`
Archivos `data_station101_YYYYMMDD.csv` extraídos de la microSD.

La ventana formal de análisis fue del **21 al 28 de julio de 2026, hasta las 18:00 UTC−5**. El archivo del 18 de julio y `pending_timestamp.csv` forman parte del material original, pero fueron excluidos de la ventana formal según el documento V6.

### `raw/system_logs/`
Logs diarios `system_YYYYMMDD.csv` utilizados para analizar estados, conectividad, reintentos y BACKFILL.

### `raw/thingspeak/`
Debe contener la exportación original `feeds_thingspeak.csv`. No se incluye si no está disponible como archivo fuente verificable.

### `derived/`
Tablas derivadas de los resultados reportados en la tesis V6. No sustituyen las fuentes originales.

## Regla de integridad

Los archivos de `raw/` deben conservarse sin modificación. Todo procesamiento debe producir nuevos archivos en `derived/` o en un directorio de análisis.
