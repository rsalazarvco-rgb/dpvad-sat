# ThingSpeak

ThingSpeak fue utilizado como plataforma de recepción y visualización remota.

## Mapeo documentado

| Campo | Variable | ONLINE | BACKFILL |
|---|---|---:|---:|
| `field1` | Distancia | Sí | Sí |
| `field2` | Temperatura | Sí | Sí |
| `field3` | Humedad | Sí | Sí |
| `field4` | MQ-9 / `gas_ppm` estimado | Sí | Sí |
| `field5` | Presión | Sí | Sí |
| `field6` | Altitud estimada | Sí | Sí |
| `field7` | Información temporal | Sí | No |
| `field8` | Estado operativo | Sí | No |

## Resultados
- 11.148 publicaciones remotas.
- 7.641 ONLINE.
- 3.507 BACKFILL.
- BACKFILL = 31,46 % de la telemetría remota.

## Fuente original pendiente

La tesis identifica `feeds_thingspeak.csv` como fuente primaria. No se genera una reconstrucción artificial. Cuando se disponga del export original, debe almacenarse en `data/raw/thingspeak/`.
