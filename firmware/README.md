# Firmware

El firmware es el componente técnico principal del DPVAD_SAT.

## Arquitectura funcional
La V6 documenta módulos para:
- sensores;
- persistencia;
- telemetría;
- servidor web/JSON;
- contingencia RAM/BACKFILL;
- logging y trazabilidad;
- máquina de estados y planificador no bloqueante.

## Fuente publicada
`source/DPVAD_SAT_v12_5_SYNC_RC1_1_PUBLIC.ino` es una versión preliminar sanitizada recuperada de materiales previos del proyecto.

No se afirma que sea una copia bit a bit de la versión exacta utilizada en toda la campaña experimental.

## Seguridad
Antes de publicarse se sustituyeron credenciales y claves sensibles. Aun así, debe realizarse una revisión manual antes de cada commit.
