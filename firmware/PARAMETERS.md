# Parámetros operativos documentados

| Parámetro | Valor/estrategia |
|---|---|
| Adquisición multisensorial | ciclo interno de 3 s |
| Persistencia nominal analizada | 60 s |
| NTP | actualización periódica cuando existe red |
| Publicación ONLINE | periódica y desacoplada de la adquisición |
| Rotación de archivos | diaria |
| MQ-9 | filtrado EMA |
| HC-SR04 | `pulseIn()` con timeout |
| Portal de datos | puerto 80 |
| Portal de trazabilidad | puerto 8080 |
| Modos | ONLINE / OFFLINE / BACKFILL |

**Nota:** el documento V6 diferencia el ciclo interno de adquisición (3 s) del periodo nominal de registros persistidos analizado en resultados (60 s).
