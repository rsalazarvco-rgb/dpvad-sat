# Resultados consolidados de la validación

## 1. Adquisición y persistencia

Durante la ventana formal se identificaron **11.135 registros locales** frente a **11.160 observaciones nominales**, equivalente a una completitud temporal local de **99,776 %**.

El déficit total de 25 observaciones se concentró los días 25, 26 y 27 de julio.

## 2. Telemetría remota

ThingSpeak registró **11.148 publicaciones**:

- **7.641 ONLINE (68,54 %)**;
- **3.507 BACKFILL (31,46 %)**.

El conteo remoto fue 13 publicaciones superior al local. Esta diferencia no se interpreta por sí sola como pérdida o duplicación porque las fuentes no conservan una identidad individual común para cada observación ONLINE.

## 3. BACKFILL

Los **3.507 registros BACKFILL** coincidieron con combinaciones locales únicas en `field1`–`field6`.

A la resolución de **dos decimales** utilizada en la comparación:
- error absoluto observado: 0;
- error porcentual observado: 0 %.

Este resultado demuestra fidelidad del subconjunto conciliado, no exactitud metrológica.

### Completitud

- Operativa respecto del formato BACKFILL: **100 % (6/6 campos)**.
- Integral frente al formato ONLINE: **75 % (6/8 campos)**.

`field7` y `field8` fueron omitidos por diseño durante BACKFILL.

## 4. field4 / MQ-9

En ONLINE, `field4` presentó **1.125 ausencias de 7.641 publicaciones (14,72 %)**.

- 855 casos (76 %) coincidieron temporalmente con `gas_ppm = 0`.
- 270 casos (24 %) se asociaron con valores locales positivos.

Por tanto, la ausencia de `field4` no puede atribuirse a una causa única.

## 5. Unicidad

No se observaron duplicados físicos exactos bajo los criterios definidos:
- timestamps locales;
- filas sensoriales locales;
- `entry_id`;
- `created_at`;
- filas remotas completas.

Las asociaciones múltiples detectadas por tolerancia temporal no se clasifican como duplicados lógicos confirmados.

## 6. Conectividad y BACKFILL

- RSSI válido: n=423.
- Mediana RSSI: **−69 dBm**.
- Lotes BACKFILL exitosos: **249**.
- Intentos BACKFILL fallidos: **139**.
- Mediana lote exitoso: **1,187 s**.
- P95 lote exitoso: **16,304 s**.
- Cola pendiente máxima observada: **1.479**.
- Cola al final: **0**.

Los tiempos de lote incluyen procesamiento y respuesta del servicio; no equivalen a latencia Wi‑Fi pura.

## 7. Regularidad temporal

De **11.127 intervalos intradía**, **10.736 (96,486 %)** fueron exactamente de 60 s.

- 18 intervalos fueron superiores a 90 s.
- máximo observado: 157 s.

La serie fue predominantemente regular, pero no completamente uniforme.

## 8. Alcance de los resultados

La evidencia respalda capacidad funcional para adquirir, persistir y recuperar información ante indisponibilidad del servicio remoto dentro del entorno controlado evaluado. No demuestra operación territorial prolongada ni exactitud metrológica certificada.
