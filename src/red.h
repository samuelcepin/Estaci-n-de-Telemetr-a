#ifndef RED_H
#define RED_H

#include <signal.h>
#include <time.h>

typedef struct {
    long codigo_http;      /* 200, 503, o 0 si hubo fallo de red */
    int retry_after_s;     /* Segundos indicados por Retry-After (0 si no viene) */
} RespuestaTelemetria;

typedef void (*AlarmaMqttCallback)(const char *mensaje, void *user_data);

/* Devuelve un buffer dinamico con la respuesta HTTP 200 (liberar con free), o NULL si falla */
char *red_obtener_config(const char *host, const char *token);

/* Descarga un lote CSV de lecturas desde el ID indicado (liberar con free), o NULL si falla */
char *red_obtener_lecturas(const char *host, const char *token, long long desde_id, int limite);

/* Envia el resumen JSON de filas unicas y suma de IDs. Devuelve 0 si el servidor acepta (HTTP 2xx) */
int red_enviar_resumen_descarga(const char *host, const char *token,
                                long long filas_unicas, long long suma_ids);

/* Envia el JSON con las estadisticas SQL por maquina. Devuelve 0 si el servidor acepta (HTTP 2xx) */
int red_enviar_analisis(const char *host, const char *token, const char *cuerpo_json);

/* Envia una lectura de telemetria reutilizando el easy handle HTTP.
 * Devuelve:
 *   0  -> aceptada (HTTP 2xx)
 *   1  -> rechazada temporalmente por el servidor (ej. HTTP 503 con Retry-After)
 *  -1  -> error de red o cancelado por senal
 */
int red_enviar_telemetria(const char *host, const char *token,
                          long long seq, long long ts, double temp, double corriente,
                          volatile sig_atomic_t *detener,
                          RespuestaTelemetria *resp_out);

/* Publica un latido MQTT QoS 0 en itla/estudiantes/<matricula>/latido.
 * Devuelve 0 si se publico correctamente, o -1 si el broker no respondio.
 */
int red_publicar_latido_mqtt(const char *host, const char *matricula, const char *token,
                             long long seq, long long pendientes, long long ts,
                             volatile sig_atomic_t *detener);

/* Confirma la recepcion de una alarma via POST /api/v1/alarmas/ack.
 * Devuelve 0 si el servidor responde HTTP 2xx o 409 (ya confirmada).
 */
int red_enviar_ack_alarma(const char *host, const char *token,
                          const char *codigo);

/* Se suscribe a itla/planta/<matricula>/alarma con upkeep periodico.
 * Devuelve 0 si finalizo limpiamente por senal/tiempo, o -1 si se corto la conexion.
 */
int red_escuchar_alarmas_mqtt(const char *host, const char *matricula, const char *token,
                              volatile sig_atomic_t *detener, time_t tiempo_limite,
                              AlarmaMqttCallback on_mensaje, void *user_data);

/* Libera los handles persistentes de libcurl antes de salir */
void red_limpiar(void);

#endif





