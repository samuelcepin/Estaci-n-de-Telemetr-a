# Programación para mecatrónicos sábados

## Code Challenge: Estación de Telemetría ITLA

* **Estudiante:** Samuel R. Cepin
* **Matrícula:** 2024-0566
* **Reto Asignado:** Estación de Telemetría Industrial en C (libcurl + SQLite)

## Descripción

El programa implementa una estación de telemetría en lenguaje C (estándar C11) diseñada para comunicarse con el servidor de planta del ITLA mediante HTTP y MQTT utilizando la biblioteca libcurl, gestionando el almacenamiento persistente de forma local con SQLite. Al iniciar, la aplicación lee la dirección del servidor y las credenciales desde variables de entorno para descargar los parámetros de operación y registrarlos en su base de datos.

Posteriormente, la estación descarga de manera paginada el historial de lecturas de las máquinas evitando registros duplicados mediante restricciones de clave primaria y transacciones SQL. Con los datos almacenados, el sistema filtra las filas corruptas o incompletas y calcula las métricas estadísticas por cada equipo directamente mediante consultas de agregación en SQLite.

Finalmente, el sistema opera con dos procesos simultáneos sobre la misma base de datos: un bucle continuo de envío que genera mediciones, las resguarda en una cola local para sobrevivir a caídas temporales del servidor sin perder secuencias y publica latidos periódicos por MQTT; y un suscriptor independiente que recibe alertas en tiempo real, descarta mensajes retenidos ya procesados y envía su confirmación HTTP al servidor.