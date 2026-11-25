#include "red.h"
#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define USER_AGENT_ESTACION "estacion-itla/1.0"
#define PUERTO_API 4000

typedef struct {
    char *datos;
    size_t tam;
} BufferMemoria;

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

static char *ejecutar_http(const char *url, const char *token,
                           const char *json_post, long *codigo_http_out) {
    BufferMemoria buf;
    buf.datos = malloc(1);
    if (!buf.datos) {
        return NULL;
    }
    buf.datos[0] = '\0';
    buf.tam = 0;

    curl_global_init(CURL_GLOBAL_DEFAULT);
    CURL *curl = curl_easy_init();
    if (!curl) {
        free(buf.datos);
        curl_global_cleanup();
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
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);

    CURLcode res = curl_easy_perform(curl);
    long codigo_http = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &codigo_http);

    if (headers) {
        curl_slist_free_all(headers);
    }
    curl_easy_cleanup(curl);
    curl_global_cleanup();

    if (res != CURLE_OK) {
        fprintf(stderr, "Error en peticion HTTP (%s): %s\n", url, curl_easy_strerror(res));
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
    char *respuesta = ejecutar_http(url, token, NULL, &codigo_http);
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
    char *respuesta = ejecutar_http(url, token, NULL, &codigo_http);
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
    char *respuesta = ejecutar_http(url, token, cuerpo_json, &codigo_http);
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

