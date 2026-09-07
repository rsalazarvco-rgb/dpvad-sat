# Modelo de datos y trazabilidad

## Estructura lógica del registro

La tesis V6 documenta una estructura lógica con:
- marca temporal;
- orden/secuencia;
- modo operativo;
- distancia;
- temperatura;
- humedad;
- presión;
- respuesta MQ-9;
- variables inerciales;
- estado de sincronización.

Los nombres físicos y la disponibilidad de campos varían entre microSD, ONLINE y BACKFILL.

## Trazabilidad

| Elemento | Fuente | Disponibilidad analítica |
|---|---|---|
| `boot_id` | Logs | Sí; identifica sesiones |
| Secuencia individual | Firmware | No preservada como columna común en CSV analizados |
| Identificador de lote | BACKFILL | Reconstruible por eventos, tamaño, offset y tiempo |
| `entry_id` | ThingSpeak | Identifica publicación remota |
| Timestamp local | microSD | Sí |
| `created_at` | ThingSpeak | Sí |

La ausencia de una clave individual común en ambas fuentes impide declarar conciliación determinística de todas las publicaciones ONLINE.
