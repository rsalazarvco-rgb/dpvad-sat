# Matriz sintética de validación

| Dimensión | Resultado | Limitación |
|---|---|---|
| Adquisición local | 11.135/11.160; 99,776 % | Discontinuidades puntuales |
| Unicidad física | 0 duplicados exactos | No equivale a unicidad lógica extremo a extremo |
| Trazabilidad ONLINE | `entry_id`, `created_at` | Sin identidad común local/remota |
| BACKFILL | 3.507/3.507 conciliados | Solo `field1`–`field6` |
| Completitud BACKFILL | 100 % operativa; 75 % integral | `field7` y `field8` omitidos |
| Latencia extremo a extremo | No calculable directamente | Faltan timestamps comparables por observación |
| Tiempo de lotes | mediana 1,187 s; P95 16,304 s | 139 intentos fallidos |
| Series temporales | 96,486 % a 60 s | 18 intervalos >90 s; máximo 157 s |
