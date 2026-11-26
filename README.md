# Programación para Mecatrónicos — Sábados

## Code Challenge: Estación de Telemetría ITLA

**Estudiante:** Samuel R. Cepin  
**Matrícula:** 2024-0566  
**Reto asignado:** Estación de Telemetría Industrial en C (libcurl + SQLite)

---

# Estación de Telemetría ITLA

## 1. Descripción

El programa implementa una estación de telemetría en lenguaje C (estándar C11), diseñada para comunicarse con el servidor de planta del ITLA mediante los protocolos HTTP y MQTT, utilizando la biblioteca **libcurl**. Para la gestión y el almacenamiento persistente de la información local, utiliza **SQLite**.

Al iniciar, la aplicación obtiene la dirección del servidor y las credenciales de autenticación mediante variables de entorno. Con estos datos, consulta los parámetros de operación del servidor y los almacena en su base de datos local.

Posteriormente, la estación descarga de manera paginada el historial de lecturas de las máquinas, evitando registros duplicados mediante restricciones de clave primaria y transacciones SQL. Una vez almacenados los datos, el sistema filtra las filas corruptas o incompletas y calcula las métricas estadísticas de cada equipo directamente mediante consultas de agregación en SQLite.

Finalmente, el sistema opera con dos procesos simultáneos que comparten la misma base de datos:

- **Proceso de envío de telemetría:** genera mediciones de forma continua, las almacena en una cola local para conservarlas ante interrupciones temporales del servidor y publica latidos periódicos mediante MQTT.
- **Proceso de escucha de alarmas:** recibe alertas en tiempo real mediante MQTT, descarta mensajes retenidos que ya hayan sido procesados y envía las confirmaciones correspondientes al servidor mediante HTTP.

De esta manera, la aplicación integra comunicación con servicios remotos, almacenamiento persistente, procesamiento de datos y ejecución concurrente, siguiendo una arquitectura modular desarrollada en C11.

## 2. Requisitos

### Ubuntu o Raspberry Pi OS

Instala las herramientas de compilación y las dependencias necesarias:

```sh
sudo apt update
sudo apt install build-essential libcurl4-openssl-dev
```

### Windows (MSYS2 UCRT64)

Instala GCC, libcurl y Make mediante el terminal UCRT64:

```sh
pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-curl make
```

El proyecto incluye la amalgamación de SQLite, por lo que no necesita enlazar la biblioteca SQLite del sistema.

## 3. Archivos de SQLite incluidos

El proyecto incluye los siguientes archivos de SQLite dentro del directorio `lib/sqlite/`:

```text
lib/sqlite/sqlite3.c
lib/sqlite/sqlite3.h
```

El `Makefile` compila `sqlite3.c` por separado y genera `build/sqlite3.o`, permitiendo integrar SQLite directamente en el proyecto.

## 4. Compilación

Desde la carpeta raíz del proyecto, ejecuta:

```sh
make clean
make
```

La compilación utiliza el estándar C11 y las opciones de advertencia:

```text
gcc -std=c11 -Wall -Wextra
```

El ejecutable generado se llama `estacion` en Linux y `estacion.exe` en Windows.

## 5. Configuración de la conexión

Las variables de entorno deben definirse en la misma terminal desde la cual se ejecutará el programa.

### Conexión en el laboratorio

```sh
export ITLA_HOST=10.0.0.200
```

### Conexión desde fuera del laboratorio

Utiliza el nombre DDNS vigente confirmado por el profesor:

```sh
export ITLA_HOST=itla-meca.duckdns.org
```

### Credenciales de autenticación

Define también la matrícula y el token asignados:

```sh
export ITLA_MATRICULA=TU_MATRICULA
export ITLA_TOKEN=TU_TOKEN
```

No escribas el token dentro del código fuente, del README ni de archivos que vayas a subir al repositorio. Tampoco reutilices contraseñas personales como token del curso.

## 6. Utilización del programa

La estación dispone de diferentes modos de ejecución que permiten configurar el sistema, descargar lecturas, analizar los datos, enviar telemetría y recibir alarmas.

### 6.1 Configuración inicial

Ejecuta el siguiente comando para consultar la configuración del servidor y almacenarla localmente:

```sh
./estacion config
```

Este proceso consulta la configuración autenticada del servidor y guarda los pares `clave=valor` en la tabla `config` de la base de datos SQLite.

### 6.2 Descarga de lecturas

Para descargar el historial de lecturas de las máquinas:

```sh
./estacion descargar
```

Este proceso obtiene las lecturas mediante solicitudes paginadas, conserva los campos vacíos, evita registros duplicados mediante `INSERT OR IGNORE` y utiliza transacciones SQL para mantener la integridad de los datos.

Al finalizar, confirma el resumen de la descarga al servidor.

### 6.3 Análisis estadístico

Para calcular las estadísticas de las máquinas a partir de las lecturas almacenadas:

```sh
./estacion analizar
```

El programa filtra las filas corruptas o incompletas y calcula las métricas estadísticas mediante consultas SQL de agregación. Posteriormente, envía los resultados en formato JSON al servidor.

Los resultados numéricos dependen de los datos asignados individualmente a cada matrícula.

### 6.4 Ejecución de telemetría y alarmas MQTT

La estación permite ejecutar simultáneamente el envío de telemetría y la recepción de alarmas mediante MQTT.

Para ello, abre dos terminales y define en ambas las mismas variables de entorno.

**Terminal 1 — Envío de telemetría:**

```sh
./estacion enviar
```

Este proceso genera mediciones, las almacena en una cola local y publica latidos periódicos mediante MQTT. La cola local permite conservar los datos pendientes ante interrupciones temporales de la comunicación.

**Terminal 2 — Escucha de alarmas:**

```sh
./estacion escuchar
```

Este proceso recibe las alarmas mediante MQTT, gestiona los mensajes retenidos que ya hayan sido procesados y envía las confirmaciones correspondientes al servidor mediante HTTP.

Ambos procesos comparten la base de datos `estacion.db`. SQLite trabaja en modo WAL (`PRAGMA journal_mode=WAL`) y utiliza un tiempo de espera de bloqueo de 2000 ms mediante `sqlite3_busy_timeout`, facilitando el acceso simultáneo a la base de datos.

### 6.5 Ejecución con duración limitada

Para ejecutar los procesos durante un tiempo específico, se puede indicar la duración en segundos como argumento.

Ejemplo de ejecución durante 900 segundos:

```sh
./estacion enviar 900
./estacion escuchar 900
```

Para la sesión evaluada, sigue las fechas y la ventana de evaluación publicadas por el profesor. Mantén el proceso `enviar` activo para publicar los latidos cada 10 segundos que permiten al servidor dirigir alarmas a tu matrícula.

## 7. Consultar el progreso

Puedes consultar las métricas de progreso registradas por el servidor mediante el endpoint:

```sh
curl -s -H "Authorization: Bearer $ITLA_TOKEN" \
  "http://$ITLA_HOST:4000/api/v1/mi-progreso"
```

Este comando realiza una solicitud HTTP autenticada utilizando el token definido en las variables de entorno.

Si utilizas el nombre DDNS desde fuera del laboratorio, asegúrate de que la variable `ITLA_HOST` esté configurada correctamente.

## 8. Estructura del proyecto

El proyecto está organizado de forma modular, separando la lógica principal, la comunicación con el servidor, la gestión de datos y los procesos de telemetría.

```text
estacion/
├── src/
│   ├── main.c
│   ├── red.c
│   ├── red.h
│   ├── datos.c
│   ├── datos.h
│   ├── sensor.c
│   └── sensor.h
├── lib/
│   └── sqlite/
│       ├── sqlite3.c
│       └── sqlite3.h
├── tests/
│   └── test_estacion.c
├── Makefile
├── README.md
└── .gitignore
```

## 9. Base de datos y archivos que no se entregan

La base de datos `estacion.db` se crea automáticamente al ejecutar los subcomandos del programa.

No incluyas en el repositorio ni en la entrega final los siguientes archivos:

- `estacion.db`, `estacion.db-wal` ni `estacion.db-shm`.
- Los ejecutables `estacion` ni `test_estacion`.
- La carpeta `build/` ni los archivos `.o`.
- Tokens, contraseñas ni archivos `.env`.

El archivo `.gitignore` se encarga de excluir los archivos generados y las credenciales locales del repositorio.


## 10. Declaración de asistencia

Se utilizó un asistente de inteligencia artificial como apoyo para estructurar la arquitectura modular en C11, depurar el manejo de peticiones HTTP/MQTT con `libcurl` y redactar la documentación técnica. Todo el código fuente y las pruebas fueron revisados, compilados y validados línea por línea por el estudiante para su defensa oral.

## 11. Pruebas automatizadas

El proyecto incluye una suite de pruebas unitarias en C dentro de `tests/test_estacion.c`, diseñada para validar los diferentes componentes del sistema.

Las pruebas contemplan:

- Persistencia de datos en SQLite.
- Deduplicación de lecturas.
- Filtrado y cálculo de estadísticas mediante SQL.
- Funcionamiento de la cola FIFO de telemetría.
- Control de alarmas retenidas sin necesidad de conexión a internet.

Para compilar y ejecutar las pruebas:

```sh
make test
```

## Nota de compilación en Windows (MSYS2 / UCRT64)

El `Makefile` y las pruebas locales están configurados para entornos Linux/POSIX e incluyen la bandera `-ldl` para el enlazador. En Windows bajo **MSYS2 / UCRT64**, SQLite utiliza internamente las APIs nativas del sistema operativo, por lo que `libdl` no viene instalada por defecto.

Para compilar y ejecutar las pruebas (`make && make test-local`) en Windows sin modificar los archivos del proyecto, crea una librería estática vacía `libdl.a` en tu entorno UCRT64 una sola vez:

```bash
ar rcs /ucrt64/lib/libdl.a


