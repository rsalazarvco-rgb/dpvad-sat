# Resultados experimentales consolidados

Ventana formal: **21 de julio de 2026 00:00 – 28 de julio de 2026 18:00 (UTC−5)**.

## Adquisición y publicación

- Registros locales: **11.135**
- Conteo nominal: **11.160**
- Completitud temporal local: **99,776 %**
- Publicaciones remotas: **11.148**
- ONLINE: **7.641**
- BACKFILL: **3.507**
- BACKFILL sobre telemetría remota: **31,46 %**

## Recuperación

Los **3.507** registros BACKFILL pudieron conciliarse con combinaciones locales únicas para `field1`–`field6`, con error observado igual a cero a la resolución de dos decimales utilizada en la comparación.

Esto demuestra fidelidad para el formato reducido evaluado; no constituye validación metrológica ni equivalencia integral con los ocho campos ONLINE.

## Unicidad

No se observaron duplicados físicos exactos bajo los criterios definidos en microSD o ThingSpeak. La unicidad lógica extremo a extremo de ONLINE no pudo demostrarse por ausencia de una identidad individual común exportada en ambas fuentes.

## Operación de lotes

- Lotes BACKFILL exitosos: **249**
- Intentos fallidos: **139**
- Cola pendiente máxima observada: **1.479**
- Cola al final: **0**
- Mediana lote exitoso: **1,187 s**
- P95 lote exitoso: **16,304 s**

## Regularidad temporal

- Intervalos intradía evaluados: **11.127**
- Exactamente 60 s: **10.736 (96,486 %)**
- Intervalos > 90 s: **18**
- Máximo: **157 s**

Los resultados corresponden a un entorno controlado y no se extrapolan directamente a condiciones rurales reales.
