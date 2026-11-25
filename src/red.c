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

char *red_obtener_config(const char *host, const char *token) {
    size_t host_len = strlen(host);
    while (host_len > 0 && host[host_len - 1] == '/') {
        host_len--;
    }

    char url[512];
    int escritos;
    if (strncmp(host, "http://", 7) == 0 || strncmp(host, "https://", 8) == 0) {
        escritos = snprintf(url, sizeof(url), "%.*s/api/v1/config", (int)host_len, host);
    } else if (memchr(host, ':', host_len) != NULL) {
        escritos = snprintf(url, sizeof(url), "http://%.*s/api/v1/config", (int)host_len, host);
    } else {
        escritos = snprintf(url, sizeof(url), "http://%.*s:%d/api/v1/config",
                            (int)host_len, host, PUERTO_API);
    }

    if (escritos < 0 || (size_t)escritos >= sizeof(url)) {
        fprintf(stderr, "Error: URL demasiado larga.\n");
        return NULL;
    }

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

    curl_easy_cleanup(curl);
    curl_global_cleanup();

    if (res != CURLE_OK) {
        fprintf(stderr, "Error en peticion HTTP (%s): %s\n", url, curl_easy_strerror(res));
        free(buf.datos);
        return NULL;
    }

    if (codigo_http != 200) {
        fprintf(stderr, "Error del servidor (HTTP %ld): %s\n", codigo_http, buf.datos);
        free(buf.datos);
        return NULL;
    }

    return buf.datos;
}