# Descripción académica del proyecto

## Nombre

**DPVAD_SAT — Plataforma de Adquisición de Datos para Sistemas de Alerta Temprana en Contextos con Baja Conectividad**

## Problema abordado

El proyecto estudia la continuidad de adquisición y disponibilidad de información ambiental cuando la conectividad remota es intermitente o inexistente. En este contexto, la pérdida temporal de acceso a internet puede producir retrasos de telemetría y discontinuidades en la disponibilidad remota de información.

## Propuesta

DPVAD_SAT adopta una arquitectura de borde basada en ESP32 con:

- adquisición multisensorial;
- procesamiento local;
- visualización LCD 20 × 4;
- persistencia en microSD;
- Wi‑Fi;
- publicación remota en ThingSpeak;
- servicios HTTP locales;
- registro de eventos;
- modos ONLINE, OFFLINE y BACKFILL;
- recuperación diferida mediante patrón store-and-forward.

## Alcance dentro de un SAT

La plataforma corresponde al componente de **monitoreo/adquisición** asociado a un Sistema de Alerta Temprana. La versión evaluada no implementa un SAT institucional completo: no incorpora modelos predictivos validados, umbrales oficiales, difusión institucional de alertas ni protocolos de respuesta.

## Enfoque metodológico

La investigación se desarrolló como investigación aplicada y desarrollo tecnológico, con diseño experimental de ingeniería en entorno controlado y enfoque CDIO: Concebir, Diseñar, Implementar y Operar.
