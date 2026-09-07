# ThingSpeak

ThingSpeak fue utilizado como plataforma remota de telemetría durante la validación experimental del DPVAD_SAT.

En condiciones de conectividad disponible, el dispositivo publicó información mediante el modo ONLINE. Después de periodos de indisponibilidad, los registros pendientes fueron transmitidos mediante BACKFILL.

En la versión evaluada:

- ONLINE utilizó ocho campos;
- BACKFILL transmitió seis campos;
- `field7` y `field8` no fueron transmitidos en BACKFILL por diseño.

ThingSpeak se utilizó como plataforma de prototipado, recepción y visualización remota. Al ser un servicio externo, no se considera un repositorio completamente controlado por el DPVAD_SAT.
