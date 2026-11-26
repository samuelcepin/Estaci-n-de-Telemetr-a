# Pruebas del proyecto

Estas pruebas separan las comprobaciones locales de las comprobaciones de red.

## 1. Pruebas locales (sin token ni internet)

Desde la raiz del proyecto:

```sh
python3 -m unittest discover -s tests -p 'test_local.py' -v
```

Comprueban la estructura, el Makefile, que no haya un token incrustado en el codigo,
la compilacion C11 sin advertencias y una prueba de integracion de SQLite. La prueba
SQLite crea una base temporal y verifica WAL, inserciones preparadas, duplicados,
estadisticas SQL, cola de telemetria, confirmacion local de alarmas y rangos del sensor simulado. No toca
`estacion.db`.

## 2. Pruebas de consulta al servidor (solo lectura)

Configura las variables en la terminal. No escribas el token dentro de los archivos:

```sh
export ITLA_HOST=itla-meca.duckdns.org
export ITLA_MATRICULA=TU_MATRICULA
export ITLA_TOKEN=TU_TOKEN
python3 -m unittest discover -s tests -p 'test_server.py' -v
```

En el laboratorio se puede usar `ITLA_HOST=10.0.0.200`. Si necesitas especificar
una URL completa, usa `ITLA_API_BASE=http://host:4000`. El puerto MQTT se puede
cambiar con `ITLA_MQTT_PORT` y el timeout HTTP/TCP con `ITLA_TEST_TIMEOUT`.

Las pruebas consultan DNS, `/api/v1/ping`, `/tablero`, `/api/v1/config`,
`/api/v1/mi-progreso` y verifican que el puerto TCP 1883 esté accesible. No hacen
POST, no descargan las 2,000 lecturas, no publican latidos, no confirman alarmas y
no se suscriben a tópicos; por tanto, no deben modificar la evaluación.

## 3. Todas las pruebas

```sh
python3 -m unittest discover -s tests -v
```

Si no defines `ITLA_HOST`, las pruebas de servidor terminan omitidas con una
explicación; las pruebas locales siguen ejecutándose.

## 4. Interpretación

- `OK` / `ok`: la comprobación se completó correctamente.
- `FAIL`: revisa el mensaje de error y la conectividad/configuración indicada.
- `skipped`: faltan variables de entorno o datos necesarios para esa prueba.

Un resultado satisfactorio en estas pruebas no demuestra que el servidor haya
aceptado la descarga, el análisis, la telemetría o las alarmas. Esos niveles se
verifican durante la sesión real de evaluación y con las operaciones específicas
del reto.
