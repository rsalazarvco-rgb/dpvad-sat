# Lista de verificación antes de publicar

## Configuración general
- [ ] Repositorio creado como **Public**.
- [ ] No seleccionar una licencia open source desde GitHub.
- [ ] Mantener `RIGHTS.md` como aviso explícito de derechos reservados.
- [ ] Confirmar URL pública: `https://github.com/rsalazarvco-rgb/dpvad-sat`.
- [ ] Confirmar correo institucional: `rsalazarv@unadvirtual.edu.co`.

## Datos
- [ ] Subir registros originales de microSD a `data/raw/local/`.
- [ ] Subir exportación original de ThingSpeak a `data/raw/thingspeak/`.
- [ ] Subir logs operativos a `data/raw/system_logs/`.
- [ ] Verificar que no existan datos personales o sensibles.
- [ ] Mantener originales sin modificación.

## Firmware
- [ ] Subir la versión preliminar del firmware.
- [ ] Confirmar correspondencia con la versión descrita en la tesis.
- [ ] Retirar claves API, SSID, contraseñas, tokens y endpoints privados.
- [ ] Revisar que `config.h`, `secrets.h` y `.env` no se publiquen.

## ThingSpeak
- [ ] Completar nombres reales de `field1` a `field8`.
- [ ] Verificar la descripción ONLINE/BACKFILL.
- [ ] No publicar claves API privadas.

## MATLAB
- [ ] Subir únicamente scripts utilizados o apropiados para publicación.
- [ ] Documentar entradas y salidas de cada script.
- [ ] Añadir figuras reproducibles cuando corresponda.

## Documento académico
- [ ] Añadir la URL pública del repositorio en la sección de anexos.
- [ ] Indicar que ThingSpeak y MATLAB fueron utilizados como parte del entorno experimental y analítico.
- [ ] Indicar que cualquier solicitud adicional debe dirigirse a `rsalazarv@unadvirtual.edu.co`.
- [ ] Verificar coherencia entre README, anexos y versión final del documento.
