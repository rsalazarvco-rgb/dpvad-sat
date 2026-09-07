# Parámetros operativos documentados

| Parámetro | Valor / estrategia |
|---|---|
| Ciclo interno de adquisición multisensorial | 3 s |
| Persistencia nominal analizada | 60 s |
| NTP | Periódico cuando existe red |
| Publicación remota | Desacoplada de adquisición |
| Rotación de archivos | Diaria |
| MQ-9 | EMA y relación Rs/R0 |
| HC-SR04 | `pulseIn()` con timeout |
| Portal de datos | Puerto 80 |
| Portal de trazabilidad | Puerto 8080 |
| Modos | ONLINE / OFFLINE / BACKFILL |

El documento diferencia el ciclo interno de adquisición del periodo nominal de los registros persistidos utilizados en el análisis temporal.
