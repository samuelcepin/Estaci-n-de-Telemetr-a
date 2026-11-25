#include <stdio.h>
#include <string.h>
#include <curl/curl.h>
#include "sqlite3.h"

static int es_subcomando_valido(const char *cmd) {
    return strcmp(cmd, "config") == 0 ||
           strcmp(cmd, "descargar") == 0 ||
           strcmp(cmd, "analizar") == 0 ||
           strcmp(cmd, "enviar") == 0 ||
           strcmp(cmd, "escuchar") == 0;
}

int main(int argc, char *argv[]) {
    if (argc != 2 || !es_subcomando_valido(argv[1])) {
        fprintf(stderr, "Uso: %s <config|descargar|analizar|enviar|escuchar>\n", argv[0]);
        return 1;
    }

    printf("Subcomando: %s\n", argv[1]);
    printf("Entorno listo -> SQLite %s | %s\n", sqlite3_libversion(), curl_version());
    return 0;
}