#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include "red.h"
#include <ctype.h>
#include <errno.h>
#include <time.h>
#ifdef _WIN32
#include <windows.h>
#endif
#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define USER_AGENT_ESTACION "estacion-itla/1.0"
#define PUERTO_API 4000
#define PUERTO_MQTT 1883

typedef struct {
    char *datos;
    size_t tam;
} BufferMemoria;

typedef struct {
    AlarmaMqttCallback on_mensaje;
    void *user_data;
    volatile sig_atomic_t *detener;
    time_t tiempo_limite;
} ContextoSuscripcionMqtt;

static int curl_inicializado = 0;
static CURL *handle_http_reutilizable = NULL;

static void asegurar_curl_global(void) {
    if (!curl_inicializado) {
        curl_global_init(CURL_GLOBAL_DEFAULT);
        curl_inicializado = 1;
    }
}

static CURL *obtener_handle_http(void) {
    asegurar_curl_global();
    if (!handle_http_reutilizable) {
        handle_http_reutilizable = curl_easy_init();
    } else {
        curl_easy_reset(handle_http_reutilizable);
    }
    return handle_http_reutilizable;
}

void red_limpiar(void) {
    if (handle_http_reutilizable) {
        curl_easy_cleanup(handle_http_reutilizable);
        handle_http_reutilizable = NULL;
    }
    if (curl_inicializado) {
        curl_global_cleanup();
        curl_inicializado = 0;
    }
}

static size_t escribir_callback(void *contenido, size_t size, size_t nmemb, void *userp) {
    size_t total = size * nmemb;
    BufferMemoria *buf = (BufferMemoria *)userp;

    char *nuevo = realloc(buf->datos, buf->tam + total + 1);
    if (!nuevo) {
        return 0;
    }

    buf->datos = nuevo;
    memcpy(buf->datos + buf->tam, contenido, total);
    buf->tam += total;
    buf->datos[buf->tam] = '\0';

    return total;
}

static int empieza_con_ci(const char *texto, size_t len_texto, const char *prefijo) {
    size_t len_pref = strlen(prefijo);
    if (len_texto < len_pref) {
        return 0;
    }
    for (size_t i = 0; i < len_pref; i++) {
        if (tolower((unsigned char)texto[i]) != tolower((unsigned char)prefijo[i])) {
            return 0;
        }
    }
    return 1;
}

static size_t cabecera_callback(char *buffer, size_t size, size_t nitems, void *userdata) {
    size_t total = size * nitems;
    int *retry_after_out = (int *)userdata;

    if (retry_after_out && empieza_con_ci(buffer, total, "Retry-After:")) {
        size_t pos = 12;
        while (pos < total && (buffer[pos] == ' ' || buffer[pos] == '\t')) {
            pos++;
        }
        char num_buf[32];
        size_t j = 0;
        while (pos < total && j + 1 < sizeof(num_buf) && isdigit((unsigned char)buffer[pos])) {
            num_buf[j++] = buffer[pos++];
        }
        num_buf[j] = '\0';
        if (j > 0) {
            long segs = strtol(num_buf, NULL, 10);
            if (segs > 0 && segs <= 86400) {
                *retry_after_out = (int)segs;
            }
        }
    }

    return total;
}

static int progreso_callback(void *clientp, curl_off_t dltotal, curl_off_t dlnow,
                             curl_off_t ultotal, curl_off_t ulnow) {
    (void)dltotal;
    (void)dlnow;
    (void)ultotal;
    (void)ulnow;

    volatile sig_atomic_t *detener = (volatile sig_atomic_t *)clientp;
    if (detener && *detener) {
        return 1;
    }
    return 0;
}

static int progreso_mqtt_callback(void *clientp, curl_off_t dltotal, curl_off_t dlnow,
                                  curl_off_t ultotal, curl_off_t ulnow) {
    (void)dltotal;
    (void)dlnow;
    (void)ultotal;
    (void)ulnow;

    ContextoSuscripcionMqtt *ctx = (ContextoSuscripcionMqtt *)clientp;
    if (!ctx) {
        return 0;
    }
    if (ctx->detener && *(ctx->detener)) {
        return 1;
    }
    if (ctx->tiempo_limite > 0 && time(NULL) >= ctx->tiempo_limite) {
        return 1;
    }
    return 0;
}

static size_t escribir_mqtt_callback(void *contenido, size_t size, size_t nmemb, void *userp) {
    size_t total = size * nmemb;
    ContextoSuscripcionMqtt *ctx = (ContextoSuscripcionMqtt *)userp;

    if (total == 0 || !ctx || !ctx->on_mensaje) {
        return total;
    }

    const unsigned char *bytes = (const unsigned char *)contenido;
    const char *payload_ptr = NULL;
    size_t payload_len = 0;

    if (total >= 2) {
        size_t topic_len = ((size_t)bytes[0] << 8) | (size_t)bytes[1];
        if (2 + topic_len <= total) {
            payload_ptr = (const char *)(bytes + 2 + topic_len);
            payload_len = total - (2 + topic_len);
        }
    }

    if (!payload_ptr) {
        payload_ptr = (const char *)bytes;
        payload_len = total;
    }

    while (payload_len > 0 &&
           (payload_ptr[payload_len - 1] == '\n' ||
            payload_ptr[payload_len - 1] == '\r' ||
            payload_ptr[payload_len - 1] == '\0')) {
        payload_len--;
    }

    if (payload_len > 0) {
        char msg[512];
        size_t copiar = (payload_len < sizeof(msg) - 1) ? payload_len : (sizeof(msg) - 1);
        memcpy(msg, payload_ptr, copiar);
        msg[copiar] = '\0';
        ctx->on_mensaje(msg, ctx->user_data);
    }

    return total;
}

static int construir_url(char *dest, size_t dest_tam, const char *host, const char *ruta) {
    size_t host_len = strlen(host);
    while (host_len > 0 && host[host_len - 1] == '/') {
        host_len--;
    }

    int escritos;
    if (strncmp(host, "http://", 7) == 0 || strncmp(host, "https://", 8) == 0) {
        escritos = snprintf(dest, dest_tam, "%.*s%s", (int)host_len, host, ruta);
    } else if (memchr(host, ':', host_len) != NULL) {
        escritos = snprintf(dest, dest_tam, "http://%.*s%s", (int)host_len, host, ruta);
    } else {
        escritos = snprintf(dest, dest_tam, "http://%.*s:%d%s",
                            (int)host_len, host, PUERTO_API, ruta);
    }

    if (escritos < 0 || (size_t)escritos >= dest_tam) {
        fprintf(stderr, "Error: URL demasiado larga.\n");
        return -1;
    }
    return 0;
}

static void extraer_hostname(const char *host, char *dest, size_t dest_tam) {
    const char *inicio = host;
    if (strncmp(inicio, "http://", 7) == 0) {
        inicio += 7;
    } else if (strncmp(inicio, "https://", 8) == 0) {
        inicio += 8;
    } else if (strncmp(inicio, "mqtt://", 7) == 0) {
        inicio += 7;
    }

    size_t len = 0;
    while (inicio[len] != '\0' && inicio[len] != ':' && inicio[len] != '/') {
        len++;
    }

    if (len >= dest_tam) {
        len = dest_tam - 1;
    }
    memcpy(dest, inicio, len);
    dest[len] = '\0';
}

static long long reloj_ms(void) {
    struct timespec ahora;
    if (timespec_get(&ahora, TIME_UTC) != TIME_UTC) {
        return 0;
    }
    return (long long)ahora.tv_sec * 1000LL + (long long)(ahora.tv_nsec / 1000000L);
}

static void pausa_ms(unsigned int ms) {
#ifdef _WIN32
    Sleep(ms);
#else
    struct timespec req;
    req.tv_sec = (time_t)(ms / 1000u);
    req.tv_nsec = (long)(ms % 1000u) * 1000000L;
    while (nanosleep(&req, &req) == -1 && errno == EINTR) {
        /* nanosleep interrumpido: completar la espera restante */
    }
#endif
}

/* La API limita a cinco peticiones por segundo por estudiante.
   250 ms entre inicios deja margen para no disparar respuestas 429. */
static void respetar_limite_http(void) {
    static long long ultima_peticion_ms = 0;
    long long ahora = reloj_ms();

    if (ahora > 0 && ultima_peticion_ms > 0) {
        long long transcurrido = ahora - ultima_peticion_ms;
        if (transcurrido >= 0 && transcurrido < 250) {
            pausa_ms((unsigned int)(250 - transcurrido));
            ahora = reloj_ms();
        }
    }

    if (ahora > 0) {
        ultima_peticion_ms = ahora;
    }
}

static char *ejecutar_http(const char *url, const char *token,
                           const char *json_post, long *codigo_http_out,
                           int *retry_after_out, volatile sig_atomic_t *detener,
                           int silenciar_error_red) {
    BufferMemoria buf;
    buf.datos = malloc(1);
    if (!buf.datos) {
        return NULL;
    }
    buf.datos[0] = '\0';
    buf.tam = 0;

    if (retry_after_out) {
        *retry_after_out = 0;
    }

    CURL *curl = obtener_handle_http();
    if (!curl) {
        free(buf.datos);
        return NULL;
    }

    struct curl_slist *headers = NULL;
    if (json_post != NULL) {
        headers = curl_slist_append(headers, "Content-Type: application/json");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_post);
    }

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, USER_AGENT_ESTACION);
    curl_easy_setopt(curl, CURLOPT_HTTPAUTH, (long)CURLAUTH_BEARER);
    curl_easy_setopt(curl, CURLOPT_XOAUTH2_BEARER, token);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, escribir_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&buf);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 3L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

    if (retry_after_out) {
        curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, cabecera_callback);
        curl_easy_setopt(curl, CURLOPT_HEADERDATA, (void *)retry_after_out);
    }

    if (detener) {
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progreso_callback);
        curl_easy_setopt(curl, CURLOPT_XFERINFODATA, (void *)detener);
    }

    respetar_limite_http();
    CURLcode res = curl_easy_perform(curl);
    long codigo_http = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &codigo_http);

    if (headers) {
        curl_slist_free_all(headers);
    }

    if (res != CURLE_OK) {
        if (!silenciar_error_red && res != CURLE_ABORTED_BY_CALLBACK) {
            fprintf(stderr, "Error en peticion HTTP (%s): %s\n", url, curl_easy_strerror(res));
        }
        free(buf.datos);
        return NULL;
    }

    *codigo_http_out = codigo_http;
    return buf.datos;
}

char *red_obtener_config(const char *host, const char *token) {
    char url[512];
    if (construir_url(url, sizeof(url), host, "/api/v1/config") != 0) {
        return NULL;
    }

    long codigo_http = 0;
    char *respuesta = ejecutar_http(url, token, NULL, &codigo_http, NULL, NULL, 0);
    if (!respuesta) {
        return NULL;
    }

    if (codigo_http != 200) {
        fprintf(stderr, "Error del servidor (HTTP %ld): %s\n", codigo_http, respuesta);
        free(respuesta);
        return NULL;
    }

    return respuesta;
}

char *red_obtener_lecturas(const char *host, const char *token, long long desde_id, int limite) {
    char ruta[128];
    snprintf(ruta, sizeof(ruta), "/api/v1/lecturas?desde_id=%lld&limite=%d", desde_id, limite);

    char url[512];
    if (construir_url(url, sizeof(url), host, ruta) != 0) {
        return NULL;
    }

    long codigo_http = 0;
    char *respuesta = ejecutar_http(url, token, NULL, &codigo_http, NULL, NULL, 0);
    if (!respuesta) {
        return NULL;
    }

    if (codigo_http != 200) {
        fprintf(stderr, "Error descargando lecturas (HTTP %ld): %s\n", codigo_http, respuesta);
        free(respuesta);
        return NULL;
    }

    return respuesta;
}

int red_enviar_resumen_descarga(const char *host, const char *token,
                                long long filas_unicas, long long suma_ids) {
    char url[512];
    if (construir_url(url, sizeof(url), host, "/api/v1/respuestas/descarga") != 0) {
        return -1;
    }

    char cuerpo_json[256];
    snprintf(cuerpo_json, sizeof(cuerpo_json),
             "{\"filas_unicas\":%lld,\"suma_ids\":%lld}",
             filas_unicas, suma_ids);

    long codigo_http = 0;
    char *respuesta = ejecutar_http(url, token, cuerpo_json, &codigo_http, NULL, NULL, 0);
    if (!respuesta) {
        return -1;
    }

    if (codigo_http < 200 || codigo_http >= 300) {
        fprintf(stderr, "Error confirmando descarga (HTTP %ld): %s\n", codigo_http, respuesta);
        free(respuesta);
        return -1;
    }

    printf("Respuesta del servidor: %s\n", respuesta);
    free(respuesta);
    return 0;
}

int red_enviar_analisis(const char *host, const char *token, const char *cuerpo_json) {
    char url[512];
    if (construir_url(url, sizeof(url), host, "/api/v1/respuestas/analisis") != 0) {
        return -1;
    }

    long codigo_http = 0;
    char *respuesta = ejecutar_http(url, token, cuerpo_json, &codigo_http, NULL, NULL, 0);
    if (!respuesta) {
        return -1;
    }

    printf("Respuesta del servidor (HTTP %ld): %s\n", codigo_http, respuesta);

    if (codigo_http < 200 || codigo_http >= 300) {
        free(respuesta);
        return -1;
    }

    free(respuesta);
    return 0;
}

int red_enviar_telemetria(const char *host, const char *token,
                          long long seq, long long ts, double temp, double corriente,
                          volatile sig_atomic_t *detener,
                          RespuestaTelemetria *resp_out) {
    if (resp_out) {
        resp_out->codigo_http = 0;
        resp_out->retry_after_s = 0;
    }

    char url[512];
    if (construir_url(url, sizeof(url), host, "/api/v1/telemetria") != 0) {
        return -1;
    }

    char cuerpo_json[256];
    snprintf(cuerpo_json, sizeof(cuerpo_json),
             "{\"seq\":%lld,\"ts\":%lld,\"temp\":%.2f,\"corriente\":%.2f}",
             seq, ts, temp, corriente);

    long codigo_http = 0;
    int retry_after = 0;
    char *respuesta = ejecutar_http(url, token, cuerpo_json, &codigo_http,
                                    &retry_after, detener, 1);
    if (!respuesta) {
        return -1;
    }

    if (resp_out) {
        resp_out->codigo_http = codigo_http;
        resp_out->retry_after_s = retry_after;
    }

    free(respuesta);

    if (codigo_http >= 200 && codigo_http < 300) {
        return 0;
    }
    return 1;
}

int red_publicar_latido_mqtt(const char *host, const char *matricula, const char *token,
                             long long seq, long long pendientes, long long ts,
                             volatile sig_atomic_t *detener) {
    char hostname[256];
    extraer_hostname(host, hostname, sizeof(hostname));
    if (hostname[0] == '\0') {
        return -1;
    }

    char url[512];
    int escritos = snprintf(url, sizeof(url),
                            "mqtt://%s:%d/itla/estudiantes/%s/latido",
                            hostname, PUERTO_MQTT, matricula);
    if (escritos < 0 || (size_t)escritos >= sizeof(url)) {
        return -1;
    }

    char payload[128];
    snprintf(payload, sizeof(payload), "LATIDO;%lld;%lld;%lld\n", seq, pendientes, ts);

    asegurar_curl_global();
    CURL *curl = curl_easy_init();
    if (!curl) {
        return -1;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)strlen(payload));
    curl_easy_setopt(curl, CURLOPT_USERNAME, matricula);
    curl_easy_setopt(curl, CURLOPT_PASSWORD, token);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 2L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 3L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

    if (detener) {
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progreso_callback);
        curl_easy_setopt(curl, CURLOPT_XFERINFODATA, (void *)detener);
    }

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    return (res == CURLE_OK) ? 0 : -1;
}

int red_enviar_ack_alarma(const char *host, const char *token, const char *codigo) {
    char url[512];
    if (construir_url(url, sizeof(url), host, "/api/v1/alarmas/ack") != 0) {
        return -1;
    }

    char cuerpo_json[160];
    int escritos = snprintf(cuerpo_json, sizeof(cuerpo_json),
                            "{\"codigo\":\"%s\"}", codigo);
    if (escritos < 0 || (size_t)escritos >= sizeof(cuerpo_json)) {
        return -1;
    }

    long codigo_http = 0;
    char *respuesta = ejecutar_http(url, token, cuerpo_json, &codigo_http, NULL, NULL, 0);
    if (!respuesta) {
        return -1;
    }

    /* 409 significa que el servidor ya confirmó esa alarma: es idempotente. */
    if ((codigo_http >= 200 && codigo_http < 300) || codigo_http == 409) {
        printf("[ACK OK] HTTP %ld: %s\n", codigo_http, respuesta);
        free(respuesta);
        return 0;
    }

    fprintf(stderr, "Error confirmando alarma (HTTP %ld): %s\n", codigo_http, respuesta);
    free(respuesta);
    return -1;
}

int red_escuchar_alarmas_mqtt(const char *host, const char *matricula, const char *token,
                              volatile sig_atomic_t *detener, time_t tiempo_limite,
                              AlarmaMqttCallback on_mensaje, void *user_data) {
    char hostname[256];
    extraer_hostname(host, hostname, sizeof(hostname));
    if (hostname[0] == '\0') {
        return -1;
    }

    char url[512];
    int escritos = snprintf(url, sizeof(url),
                            "mqtt://%s:%d/itla/planta/%s/alarma",
                            hostname, PUERTO_MQTT, matricula);
    if (escritos < 0 || (size_t)escritos >= sizeof(url)) {
        return -1;
    }

    ContextoSuscripcionMqtt ctx;
    ctx.on_mensaje = on_mensaje;
    ctx.user_data = user_data;
    ctx.detener = detener;
    ctx.tiempo_limite = tiempo_limite;

    asegurar_curl_global();
    CURL *curl = curl_easy_init();
    if (!curl) {
        return -1;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_USERNAME, matricula);
    curl_easy_setopt(curl, CURLOPT_PASSWORD, token);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, escribir_mqtt_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&ctx);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 0L);
    curl_easy_setopt(curl, CURLOPT_UPKEEP_INTERVAL_MS, 10000L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progreso_mqtt_callback);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, (void *)&ctx);

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    if (res == CURLE_ABORTED_BY_CALLBACK) {
        return 0;
    }
    if (detener && *detener) {
        return 0;
    }
    if (tiempo_limite > 0 && time(NULL) >= tiempo_limite) {
        return 0;
    }

    if (res != CURLE_OK) {
        fprintf(stderr, "[MQTT ESCUCHA] Desconectado (%s).\n", curl_easy_strerror(res));
        return -1;
    }

    return -1;
}









