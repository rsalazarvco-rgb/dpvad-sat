# Metodología experimental

## Diseño

- Investigación aplicada y desarrollo tecnológico.
- Diseño experimental de ingeniería en entorno controlado.
- Seguimiento longitudinal.
- Unidad experimental: prototipo DPVAD_SAT.
- Escenarios: ONLINE, OFFLINE y BACKFILL.

## Ventana formal

**21 de julio de 2026 00:00 – 28 de julio de 2026 18:00 (UTC−5).**

## Fuentes primarias

1. registros persistidos en microSD;
2. exportación del canal ThingSpeak;
3. logs operativos del firmware.

## Preparación

El procesamiento incluyó:
- normalización temporal;
- conversión numérica;
- clasificación ONLINE/BACKFILL;
- análisis de valores ausentes;
- revisión de duplicados físicos;
- conciliación del subconjunto BACKFILL;
- análisis de regularidad temporal;
- análisis de cola, reintentos y lotes.

No se imputaron valores ausentes cuando la ausencia representaba un comportamiento relevante del sistema.

## Criterio de interpretación

Los indicadores se evalúan por dimensiones independientes: completitud, consistencia, unicidad, trazabilidad, temporalidad y fidelidad de transmisión. Un 100 % en una dimensión no se interpreta como desempeño perfecto del sistema completo.
