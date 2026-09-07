# DPVAD_SAT

**Plataforma de Adquisición de Datos para Sistemas de Alerta Temprana en Contextos con Baja Conectividad**

## Descripción

DPVAD_SAT es un prototipo académico basado en ESP32 orientado a la adquisición, persistencia, visualización y transmisión de variables ambientales en escenarios con disponibilidad limitada o intermitente de conectividad.

La arquitectura implementa persistencia local en microSD y modos de operación **ONLINE, OFFLINE y BACKFILL**, bajo un enfoque *offline-first* y *store-and-forward*.

## Alcance

El prototipo corresponde al componente de adquisición y monitoreo de información ambiental asociado a un Sistema de Alerta Temprana (SAT).

La versión evaluada **no constituye un SAT completo** y no incorpora modelos predictivos, umbrales institucionales certificados, difusión formal de alertas ni protocolos institucionales de respuesta.

## Tecnologías utilizadas

- ESP32
- Arduino Framework
- Sensores ambientales integrados
- microSD
- LCD 20 × 4
- Wi-Fi
- HTTP
- ThingSpeak
- MATLAB
- Servicios web locales
- Procesamiento y análisis de datos

## ThingSpeak

ThingSpeak fue utilizado como plataforma remota de recepción, almacenamiento y visualización de telemetría durante la validación experimental.

El sistema implementó publicaciones en modo ONLINE y recuperación diferida mediante BACKFILL.

## MATLAB

MATLAB y las capacidades de análisis asociadas al ecosistema ThingSpeak se utilizaron como apoyo para la exploración, procesamiento y representación de los datos experimentales.

MATLAB no forma parte de la lógica de adquisición ejecutada por el ESP32 ni de la continuidad offline del dispositivo.

## Datos experimentales

El repositorio está preparado para incluir los conjuntos de datos correspondientes a la ventana experimental documentada en el proyecto de grado.

Fuentes principales:

- registros persistidos localmente en microSD;
- exportación de telemetría desde ThingSpeak;
- logs operativos del sistema.

Los datos originales deben conservarse sin modificación dentro de `data/raw/`. Toda transformación, depuración o conciliación debe almacenarse en `data/processed/`.

## Código fuente

El directorio `firmware/` está destinado a una versión académica preliminar del firmware utilizado durante el desarrollo y validación del DPVAD_SAT.

Antes de publicar el código deben retirarse credenciales, claves API, contraseñas, endpoints privados y cualquier otro secreto.

## Servicios locales

- **Puerto 80:** consulta y descarga de registros de datos almacenados en microSD.
- **Puerto 8080:** interfaz de administración y trazabilidad operativa del dispositivo.

## Reproducibilidad académica

El repositorio complementa el documento académico con datos, código, documentación técnica y recursos de análisis que permiten revisar la validación experimental dentro del alcance descrito en el proyecto.

## Derechos de uso

**Copyright © 2026 Rodrigo Salazar Valencia. Todos los derechos reservados.**

Este repositorio es público con fines de **consulta, evaluación y reproducibilidad académica**. Su publicación **no constituye una licencia de uso libre**.

Salvo los derechos mínimos necesarios para visualizar y utilizar las funciones propias de GitHub conforme a sus términos de servicio, **no se concede autorización para copiar, reproducir, modificar, redistribuir, incorporar en otros proyectos, explotar comercialmente ni crear trabajos derivados** a partir del código fuente, datos, documentación, figuras u otros materiales contenidos en este repositorio sin autorización previa y expresa del autor.

La citación académica del proyecto no implica autorización para reutilizar sus materiales.

Para solicitar autorización de uso, información complementaria o realizar consultas académicas:

**Rodrigo Salazar Valencia**  
Universidad Nacional Abierta y a Distancia — UNAD  
**Correo institucional:** rsalazarv@unadvirtual.edu.co

## Repositorio

https://github.com/rsalazarvco-rgb/dpvad-sat
