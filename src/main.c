#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "datos.h"
#include "red.h"

#define LIMITE_LOTE 200

static int validar_entorno(const char **host, const char **matricula, const char **token) {
    *host = getenv("ITLA_HOST");
    *matricula = getenv("ITLA_MATRICULA");
    *token = getenv("ITLA_TOKEN");

    if (!*host || (*host)[0] == '\0' ||
        !*matricula || (*matricula)[0] == '\0' ||
        !*token || (*token)[0] == '\0') {
        fprintf(stderr, "Error: debes definir ITLA_HOST, ITLA_MATRICULA e ITLA_TOKEN en el entorno.\n");
        return -1;
    }
    return 0;
}

static int ejecutar_config(void) {
    const char *host = NULL;
    const char *matricula = NULL;
    const char *token = NULL;
    if (validar_entorno(&host, &matricula, &token) != 0) {
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

/* Divide una linea CSV en exactamente 7 columnas respetando campos vacios */
static int parsear_fila_csv(char *linea, LecturaPlanta *out) {
    char *cols[7] = {0};
    char *cursor = linea;

    for (int i = 0; i < 7; i++) {
        cols[i] = cursor;
        if (i < 6) {
            char *coma = strchr(cursor, ',');
            if (!coma) {
                return -1;
            }
            *coma = '\0';
            cursor = coma + 1;
        }
    }

    if (cols[0][0] == '\0') {
        return -1;
    }

    char *fin_num = NULL;
    out->id = strtoll(cols[0], &fin_num, 10);
    if (fin_num == cols[0] || *fin_num != '\0') {
        return -1;
    }

    out->maquina = cols[1];
    out->ts = cols[2];

    if (cols[3][0] != '\0') {
        out->temp = strtod(cols[3], &fin_num);
        out->tiene_temp = (fin_num != cols[3] && *fin_num == '\0') ? 1 : 0;
    } else {
        out->temp = 0.0;
        out->tiene_temp = 0;
    }

    if (cols[4][0] != '\0') {
        out->corriente = strtod(cols[4], &fin_num);
        out->tiene_corriente = (fin_num != cols[4] && *fin_num == '\0') ? 1 : 0;
    } else {
        out->corriente = 0.0;
        out->tiene_corriente = 0;
    }

    if (cols[5][0] != '\0') {
        out->vibracion = strtod(cols[5], &fin_num);
        out->tiene_vibracion = (fin_num != cols[5] && *fin_num == '\0') ? 1 : 0;
    } else {
        out->vibracion = 0.0;
        out->tiene_vibracion = 0;
    }

    out->estado = cols[6];
    return 0;
}

static int ejecutar_descargar(void) {
    const char *host = NULL;
    const char *matricula = NULL;
    const char *token = NULL;
    if (validar_entorno(&host, &matricula, &token) != 0) {
        return 1;
    }

    sqlite3 *db = NULL;
    if (datos_abrir("estacion.db", &db) != 0) {
        return 1;
    }

    long long desde_id = 0;
    int numero_lote = 0;

    while (1) {
        char *csv = red_obtener_lecturas(host, token, desde_id, LIMITE_LOTE);
        if (!csv) {
            datos_cerrar(db);
            return 1;
        }

        if (datos_iniciar_transaccion(db) != 0) {
            free(csv);
            datos_cerrar(db);
            return 1;
        }

        int filas_en_lote = 0;
        long long max_id_lote = desde_id;
        int es_primera_linea = 1;
        char *p = csv;

        while (*p != '\0') {
            char *fin_linea = strchr(p, '\n');
            if (fin_linea) {
                *fin_linea = '\0';
            }

            size_t len = strlen(p);
            if (len > 0 && p[len - 1] == '\r') {
                p[len - 1] = '\0';
                len--;
            }

            if (len > 0) {
                if (es_primera_linea && !isdigit((unsigned char)p[0])) {
                    es_primera_linea = 0;
                } else {
                    es_primera_linea = 0;
                    LecturaPlanta lectura;
                    if (parsear_fila_csv(p, &lectura) == 0) {
                        if (datos_guardar_lectura(db, &lectura) != 0) {
                            datos_confirmar_transaccion(db);
                            free(csv);
                            datos_cerrar(db);
                            return 1;
                        }
                        filas_en_lote++;
                        if (lectura.id > max_id_lote) {
                            max_id_lote = lectura.id;
                        }
                    }
                }
            }

            if (!fin_linea) {
                break;
            }
            p = fin_linea + 1;
        }

        if (datos_confirmar_transaccion(db) != 0) {
            free(csv);
            datos_cerrar(db);
            return 1;
        }

        free(csv);

        if (filas_en_lote == 0 || max_id_lote <= desde_id) {
            break;
        }

        numero_lote++;
        printf("Lote %d procesado: %d filas (ultimo ID: %lld)\n",
               numero_lote, filas_en_lote, max_id_lote);
        desde_id = max_id_lote;
    }

    long long filas_unicas = 0;
    long long suma_ids = 0;
    if (datos_resumen_lecturas(db, &filas_unicas, &suma_ids) != 0) {
        datos_cerrar(db);
        return 1;
    }

    printf("Descarga completada -> Filas unicas: %lld | Suma de IDs: %lld\n",
           filas_unicas, suma_ids);

    int rc_envio = red_enviar_resumen_descarga(host, token, filas_unicas, suma_ids);
    datos_cerrar(db);

    return (rc_envio == 0) ? 0 : 1;
}

static int agregar_texto(char **buf, size_t *len, size_t *cap, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int necesarios = vsnprintf(NULL, 0, fmt, args);
    va_end(args);

    if (necesarios < 0) {
        return -1;
    }

    size_t req = *len + (size_t)necesarios + 1;
    if (req > *cap) {
        size_t nueva_cap = (*cap == 0) ? 512 : *cap * 2;
        while (nueva_cap < req) {
            nueva_cap *= 2;
        }
        char *nuevo = realloc(*buf, nueva_cap);
        if (!nuevo) {
            return -1;
        }
        *buf = nuevo;
        *cap = nueva_cap;
    }

    va_start(args, fmt);
    vsnprintf(*buf + *len, *cap - *len, fmt, args);
    va_end(args);
    *len += (size_t)necesarios;
    return 0;
}

static int ejecutar_analizar(void) {
    const char *host = NULL;
    const char *matricula = NULL;
    const char *token = NULL;
    if (validar_entorno(&host, &matricula, &token) != 0) {
        return 1;
    }

    sqlite3 *db = NULL;
    if (datos_abrir("estacion.db", &db) != 0) {
        return 1;
    }

    double umbral_temp = 0.0;
    if (datos_obtener_umbral_temp(db, &umbral_temp) != 0) {
        datos_cerrar(db);
        return 1;
    }

    EstadisticaMaquina *lista = NULL;
    int total = 0;
    if (datos_analizar_maquinas(db, umbral_temp, &lista, &total) != 0) {
        datos_cerrar(db);
        return 1;
    }

    datos_cerrar(db);

    if (total == 0) {
        fprintf(stderr, "Error: no hay lecturas en estacion.db. Ejecuta './estacion descargar' primero.\n");
        free(lista);
        return 1;
    }

    char *json = NULL;
    size_t json_len = 0;
    size_t json_cap = 0;

    if (agregar_texto(&json, &json_len, &json_cap, "{") != 0) {
        free(lista);
        free(json);
        return 1;
    }

    for (int i = 0; i < total; i++) {
        const EstadisticaMaquina *m = &lista[i];
        int rc = agregar_texto(
            &json, &json_len, &json_cap,
            "%s\"%s\":{\"validas\":%lld,\"descartadas\":%lld,"
            "\"temp_min\":%.2f,\"temp_max\":%.2f,\"temp_prom\":%.2f,\"sobre_umbral\":%lld}",
            (i > 0) ? "," : "",
            m->maquina,
            m->validas,
            m->descartadas,
            m->temp_min,
            m->temp_max,
            m->temp_prom,
            m->sobre_umbral);

        if (rc != 0) {
            fprintf(stderr, "Error de memoria construyendo JSON de analisis.\n");
            free(lista);
            free(json);
            return 1;
        }
    }

    if (agregar_texto(&json, &json_len, &json_cap, "}") != 0) {
        free(lista);
        free(json);
        return 1;
    }

    free(lista);

    printf("Umbral de temperatura aplicado: %.2f\n", umbral_temp);
    printf("JSON generado: %s\n", json);

    int rc_envio = red_enviar_analisis(host, token, json);
    free(json);

    return (rc_envio == 0) ? 0 : 1;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Uso: %s <subcomando>\n", argv[0]);
        return 1;
    }

    if (strcmp(argv[1], "config") == 0) {
        return ejecutar_config();
    }

    if (strcmp(argv[1], "descargar") == 0) {
        return ejecutar_descargar();
    }

    if (strcmp(argv[1], "analizar") == 0) {
        return ejecutar_analizar();
    }

    fprintf(stderr, "Subcomando desconocido: %s\n", argv[1]);
    return 1;
}



