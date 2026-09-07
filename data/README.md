# Datos del experimento

## `raw/local/`
Registros originales recuperados desde microSD.

## `raw/system_logs/`
Logs operativos originales utilizados para analizar conectividad, fallas, reintentos, lotes BACKFILL y estado del dispositivo.

## `raw/thingspeak/`
Reservado para la exportación original `feeds_thingspeak.csv`.

## `derived/`
Tablas estructuradas a partir de los resultados reportados en la tesis V6.

## Integridad
Los archivos de `raw/` deben mantenerse sin modificación. Se incluyen hashes SHA-256 de los archivos ZIP originales en `raw/archives/SHA256SUMS.csv`.
