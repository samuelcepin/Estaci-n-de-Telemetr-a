#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "datos.h"
#include "red.h"

static int ejecutar_config(void) {
    const char *host = getenv("ITLA_HOST");
    const char *matricula = getenv("ITLA_MATRICULA");
    const char *token = getenv("ITLA_TOKEN");

    if (!host || host[0] == '\0' ||
        !matricula || matricula[0] == '\0' ||
        !token || token[0] == '\0') {
        fprintf(stderr, "Error: debes definir ITLA_HOST, ITLA_MATRICULA e ITLA_TOKEN en el entorno.\n");
        return 1;
    }

    char *respuesta = red_obtener_config(host, token);
    if (!respuesta) {
        return 1;
    }

    sqlite3 *db = NULL;
    if (datos_abrir("estacion.db", &db) != 0) {
        free(respuesta);
        return 1;
    }

    int guardados = 0;
    const char *p = respuesta;

    while (*p != '\0') {
        const char *fin_linea = strchr(p, '\n');
        size_t len_linea = fin_linea ? (size_t)(fin_linea - p) : strlen(p);

        if (len_linea > 0 && p[len_linea - 1] == '\r') {
            len_linea--;
        }

        if (len_linea > 0 && p[0] != '#') {
            const char *igual = memchr(p, '=', len_linea);
            if (igual != NULL) {
                size_t len_clave = (size_t)(igual - p);
                const char *valor = igual + 1;
                size_t len_valor = len_linea - len_clave - 1;

                if (len_clave > 0) {
                    if (datos_guardar_config(db, p, (int)len_clave, valor, (int)len_valor) == 0) {
                        printf("Configuracion guardada -> %.*s = %.*s\n",
                               (int)len_clave, p, (int)len_valor, valor);
                        guardados++;
                    }
                }
            }
        }

        if (!fin_linea) {
            break;
        }
        p = fin_linea + 1;
    }

    datos_cerrar(db);
    free(respuesta);

    if (guardados == 0) {
        fprintf(stderr, "Advertencia: no se recibieron parametros clave=valor validos.\n");
        return 1;
    }

    printf("Total de parametros actualizados: %d\n", guardados);
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Uso: %s <subcomando>\n", argv[0]);
        return 1;
    }

    if (strcmp(argv[1], "config") == 0) {
        return ejecutar_config();
    }

    fprintf(stderr, "Subcomando desconocido: %s\n", argv[1]);
    return 1;
}