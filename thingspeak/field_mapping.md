# Mapeo ThingSpeak

El flujo operativo documentado para la publicación ONLINE utiliza ocho campos:

| Field | Variable | Unidad/tipo | ONLINE | BACKFILL |
|---|---|---|---:|---:|
| field1 | Distancia | cm | Sí | Sí |
| field2 | Temperatura | °C | Sí | Sí |
| field3 | Humedad | % | Sí | Sí |
| field4 | Gas MQ-9 | ppm estimado | Sí | Sí |
| field5 | Presión | hPa | Sí | Sí |
| field6 | Altitud estimada | m | Sí | Sí |
| field7 | Fecha/hora local | temporal | Sí | No |
| field8 | Estado operativo | categórico | Sí | No |

BACKFILL utiliza el formato reducido `field1`–`field6`; por tanto:
- completitud operativa: 6/6 = **100 %**;
- completitud integral frente a ONLINE: 6/8 = **75 %**.

`field4` corresponde a una estimación funcional del MQ-9 y no a una medición metrológicamente certificada.
