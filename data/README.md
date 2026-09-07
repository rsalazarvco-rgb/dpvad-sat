# Datos experimentales

Este directorio organiza los conjuntos de datos utilizados en la validación del DPVAD_SAT.

## Principio de conservación

Los datos originales deben almacenarse en `raw/` y **no deben modificarse**.

Cualquier limpieza, conciliación, transformación, agregación o cálculo derivado debe almacenarse en `processed/`.

## Ventana experimental

Periodo principal documentado:

**21 al 28 de julio de 2026 (UTC−5).**

## Fuentes principales

- `raw/local/`: registros locales persistidos en microSD.
- `raw/thingspeak/`: exportaciones de ThingSpeak.
- `raw/system_logs/`: logs operativos del dispositivo.

## Consideración de trazabilidad

No debe suponerse una correspondencia uno a uno entre todas las filas locales y remotas cuando las fuentes exportadas no preservan una clave común que permita demostrarla de forma determinística.

## Derechos de uso

Los conjuntos de datos se publican para consulta y reproducibilidad académica. No se concede autorización general para copiarlos, redistribuirlos, modificarlos o reutilizarlos en otros trabajos sin autorización previa del autor.

Contacto: rsalazarv@unadvirtual.edu.co
