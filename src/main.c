/*************************************************************************************/
/*                  Programación para mecatrónicos                                   */
/*  Nombre:    Samuel Cepin                                                          */
/*  Matricula: 2024-0566                                                             */
/*  Seccion:   Sabados                                                               */
/*  Practica:  Practica                                                              */
/*  Fecha:     26/11/2026                                                            */
/*  Link Practica: https://github.com/samuelcepin/Estaci-n-de-Telemetr-a             */
/*************************************************************************************/

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <unistd.h>
#endif

#include <ctype.h>
#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "datos.h"
#include "red.h"
#include "sensor.h"

#define LIMITE_LOTE 200
#define INTERVALO_LATIDO_S 10

typedef struct {
    sqlite3 *db;
    const char *host;
    const char *token;
} ContextoEscucha;

static volatile sig_atomic_t detener_programa = 0;

static void manejar_senal(int sig) {
    (void)sig;
    detener_programa = 1;
}

#ifdef _WIN32
static BOOL WINAPI manejar_consola_win(DWORD evento) {
    if (evento == CTRL_C_EVENT || evento == CTRL_BREAK_EVENT || evento == CTRL_CLOSE_EVENT) {
        detener_programa = 1;
        return TRUE;
    }
    return FALSE;
}
#endif

static void dormir_ms(unsigned int ms) {
#ifdef _WIN32
    Sleep(ms);
#else
    struct timespec req;
    req.tv_sec = (time_t)(ms / 1000u);
    req.tv_nsec = (long)(ms % 1000u) * 1000000L;
    while (nanosleep(&req, &req) == -1 && errno == EINTR && !detener_programa) {
        /* Continuar el tiempo restante, salvo que se haya solicitado detener. */
    }
#endif
}

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

static void revertir_transaccion(sqlite3 *db) {
    char *err_msg = NULL;
    int rc = sqlite3_exec(db, "ROLLBACK;", NULL, NULL, &err_msg);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Error revirtiendo transaccion: %s\n",
                err_msg ? err_msg : sqlite3_errmsg(db));
    }
    sqlite3_free(err_msg);
}

static int ejecutar_descargar(void) {
    const char *host = NULL;
    const char *matricula = NULL;
    const char *token = NULL;
    (void)matricula;

    if (validar_entorno(&host, &matricula, &token) != 0) {
        return 1;
    }

    sqlite3 *db = NULL;
    if (datos_abrir("estacion.db", &db) != 0) {
        return 1;
    }

    if (datos_iniciar_transaccion(db) != 0) {
        datos_cerrar(db);
        return 1;
    }

    long long desde_id = 0;
    int numero_lote = 0;
    int error = 0;

    for (;;) {
        char *csv = red_obtener_lecturas(host, token, desde_id, LIMITE_LOTE);
        if (!csv) {
            error = 1;
            break;
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
                    es_primera_linea = 0; /* Cabecera CSV. */
                } else {
                    es_primera_linea = 0;
                    LecturaPlanta lectura;
                    if (parsear_fila_csv(p, &lectura) == 0) {
                        if (datos_guardar_lectura(db, &lectura) != 0) {
                            error = 1;
                            break;
                        }
                        /* Cuenta filas recibidas, incluso si INSERT OR IGNORE
                           descarta una repetida; esto determina el fin de página. */
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

        free(csv);
        if (error) {
            break;
        }

        numero_lote++;
        printf("Lote %d procesado: %d filas recibidas (ultimo ID: %lld)\n",
               numero_lote, filas_en_lote, max_id_lote);

        if (filas_en_lote < LIMITE_LOTE) {
            break;
        }

        if (max_id_lote <= desde_id) {
            fprintf(stderr, "Error: la paginacion no avanzo (desde_id=%lld).\n", desde_id);
            error = 1;
            break;
        }

        desde_id = max_id_lote;
    }

    if (error) {
        revertir_transaccion(db);
        datos_cerrar(db);
        return 1;
    }

    if (datos_confirmar_transaccion(db) != 0) {
        revertir_transaccion(db);
        datos_cerrar(db);
        return 1;
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

    if (agregar_texto(&json, &json_len, &json_cap, "{\"maquinas\":[") != 0) {
        free(lista);
        free(json);
        return 1;
    }

    for (int i = 0; i < total; i++) {
        const EstadisticaMaquina *m = &lista[i];
        int rc = agregar_texto(
            &json, &json_len, &json_cap,
            "%s{\"maquina\":\"%s\",\"validas\":%lld,\"descartadas\":%lld,"
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

    if (agregar_texto(&json, &json_len, &json_cap, "]}") != 0) {
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

static int ejecutar_enviar(int duracion_s) {
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

    int intervalo_s = 0;
    if (datos_obtener_intervalo_s(db, &intervalo_s) != 0) {
        datos_cerrar(db);
        return 1;
    }

    long long siguiente_seq = 1;
    if (datos_siguiente_seq_telemetria(db, &siguiente_seq) != 0) {
        datos_cerrar(db);
        return 1;
    }

    detener_programa = 0;
    signal(SIGINT, manejar_senal);
    signal(SIGTERM, manejar_senal);
#ifdef _WIN32
    SetConsoleCtrlHandler(manejar_consola_win, TRUE);
#endif

    sensor_inicializar();

    time_t inicio = time(NULL);
    time_t proximo_muestreo = inicio;
    time_t proximo_envio = inicio;
    time_t proximo_latido = inicio;
    int backoff_s = 1;

    printf("Telemetria iniciada -> intervalo: %ds | latido MQTT: %ds | siguiente seq: %lld (Ctrl+C para detener)\n",
           intervalo_s, INTERVALO_LATIDO_S, siguiente_seq);
    fflush(stdout);

    while (!detener_programa) {
        time_t ahora = time(NULL);

        if (duracion_s > 0 && (ahora - inicio) >= duracion_s) {
            break;
        }

        /* 1. Generar y guardar nueva lectura cada intervalo_s sin bloquearse por red */
        if (ahora >= proximo_muestreo) {
            RegistroTelemetria nueva;
            sensor_leer(siguiente_seq, &nueva);

            if (datos_guardar_telemetria(db, &nueva) != 0) {
                datos_cerrar(db);
                return 1;
            }

            printf("[MUESTRA] seq=%lld ts=%lld temp=%.2f C corriente=%.2f A -> guardada en SQLite\n",
                   nueva.seq, nueva.ts, nueva.temp, nueva.corriente);
            fflush(stdout);

            siguiente_seq++;
            proximo_muestreo = ahora + intervalo_s;
        }

        /* 2. Si no estamos en ventana de espera (Retry-After/backoff), vaciar cola en orden seq */
        if (ahora >= proximo_envio && !detener_programa) {
            while (!detener_programa) {
                RegistroTelemetria pendiente;
                int hay_pendiente = 0;

                if (datos_obtener_telemetria_pendiente(db, &pendiente, &hay_pendiente) != 0) {
                    datos_cerrar(db);
                    return 1;
                }

                if (!hay_pendiente) {
                    break;
                }

                RespuestaTelemetria resp;
                int rc = red_enviar_telemetria(host, token,
                                               pendiente.seq, pendiente.ts,
                                               pendiente.temp, pendiente.corriente,
                                               &detener_programa, &resp);

                if (detener_programa) {
                    break;
                }

                if (rc == 0) {
                    if (datos_marcar_telemetria_enviada(db, pendiente.seq) != 0) {
                        datos_cerrar(db);
                        return 1;
                    }
                    backoff_s = 1;
                    printf("[ENVIO OK] seq=%lld aceptada (HTTP %ld)\n",
                           pendiente.seq, resp.codigo_http);
                    fflush(stdout);
                } else if (rc == 1) {
                    if (datos_registrar_intento_telemetria(db, pendiente.seq) != 0) {
                        datos_cerrar(db);
                        return 1;
                    }
                    int espera = (resp.retry_after_s > 0) ? resp.retry_after_s : backoff_s;
                    if (resp.retry_after_s <= 0 && backoff_s < 30) {
                        backoff_s *= 2;
                        if (backoff_s > 30) backoff_s = 30;
                    }
                    proximo_envio = time(NULL) + espera;
                    printf("[ESPERA HTTP %ld] seq=%lld retenida; reintento en %ds (muestreo sigue activo)\n",
                           resp.codigo_http, pendiente.seq, espera);
                    fflush(stdout);
                    break;
                } else {
                    if (datos_registrar_intento_telemetria(db, pendiente.seq) != 0) {
                        datos_cerrar(db);
                        return 1;
                    }
                    int espera = backoff_s;
                    if (backoff_s < 30) {
                        backoff_s *= 2;
                        if (backoff_s > 30) backoff_s = 30;
                    }
                    proximo_envio = time(NULL) + espera;
                    printf("[ERROR RED] seq=%lld retenida; reintento en %ds (muestreo sigue activo)\n",
                           pendiente.seq, espera);
                    fflush(stdout);
                    break;
                }
            }
        }

        /* 3. Publicar latido MQTT QoS 0 cada 10 s (si el broker no responde, se descarta sin encolar) */
        if (ahora >= proximo_latido && !detener_programa) {
            long long pendientes = 0;
            if (datos_contar_telemetria_pendiente(db, &pendientes) != 0) {
                datos_cerrar(db);
                return 1;
            }

            long long seq_actual = (siguiente_seq > 1) ? (siguiente_seq - 1) : 0;
            long long ts_latido = (long long)time(NULL);

            int rc_mqtt = red_publicar_latido_mqtt(host, matricula, token,
                                                   seq_actual, pendientes, ts_latido,
                                                   &detener_programa);

            if (!detener_programa) {
                if (rc_mqtt == 0) {
                    printf("[LATIDO MQTT] publicado -> LATIDO;%lld;%lld;%lld\n",
                           seq_actual, pendientes, ts_latido);
                } else {
                    printf("[LATIDO MQTT] broker sin respuesta, latido descartado (QoS 0)\n");
                }
                fflush(stdout);
            }

            time_t fin_latido = time(NULL);
            proximo_latido = (fin_latido >= ahora + INTERVALO_LATIDO_S)
                                 ? (fin_latido + 1)
                                 : (ahora + INTERVALO_LATIDO_S);
        }

        dormir_ms(100);
    }

    printf("\nDeteniendo telemetria de forma limpia y cerrando SQLite...\n");
    datos_cerrar(db);
    return 0;
}

static void procesar_mensaje_alarma(const char *mensaje, void *user_data) {
    ContextoEscucha *ctx = (ContextoEscucha *)user_data;
    if (!ctx || !mensaje) {
        return;
    }

    if (strncmp(mensaje, "NINGUNA;", 8) == 0) {
        return;
    }

    if (strncmp(mensaje, "ALARMA;", 7) != 0) {
        return;
    }

    const char *p_codigo = mensaje + 7;
    const char *sep1 = strchr(p_codigo, ';');
    if (!sep1 || sep1 == p_codigo) {
        return;
    }

    const char *p_maquina = sep1 + 1;
    const char *sep2 = strchr(p_maquina, ';');
    if (!sep2 || sep2 == p_maquina) {
        return;
    }

    const char *p_ts = sep2 + 1;
    if (*p_ts == '\0') {
        return;
    }

    char codigo[64];
    size_t len_cod = (size_t)(sep1 - p_codigo);
    if (len_cod >= sizeof(codigo)) {
        len_cod = sizeof(codigo) - 1;
    }
    memcpy(codigo, p_codigo, len_cod);
    codigo[len_cod] = '\0';

    char maquina[64];
    size_t len_maq = (size_t)(sep2 - p_maquina);
    if (len_maq >= sizeof(maquina)) {
        len_maq = sizeof(maquina) - 1;
    }
    memcpy(maquina, p_maquina, len_maq);
    maquina[len_maq] = '\0';

    char *fin_ts = NULL;
    long long ts_servidor = strtoll(p_ts, &fin_ts, 10);
    if (fin_ts == p_ts || *fin_ts != '\0') {
        return;
    }

    int ya_confirmada = 0;
    if (datos_alarma_ya_confirmada(ctx->db, codigo, &ya_confirmada) != 0) {
        return;
    }

    if (ya_confirmada) {
        printf("[ALARMA RETENIDA] codigo=%s maquina=%s ts=%lld -> ya confirmada, ignorada\n",
               codigo, maquina, ts_servidor);
        fflush(stdout);
        return;
    }

    long long ts_recibida = (long long)time(NULL);
    if (datos_guardar_alarma(ctx->db, codigo, maquina, ts_servidor, ts_recibida) != 0) {
        return;
    }

    printf("[ALARMA] codigo=%s maquina=%s ts=%lld -> guardada en SQLite, enviando ACK...\n",
           codigo, maquina, ts_servidor);
    fflush(stdout);

    if (red_enviar_ack_alarma(ctx->host, ctx->token, codigo) == 0) {
        long long ts_ack = (long long)time(NULL);
        if (datos_marcar_alarma_confirmada(ctx->db, codigo, ts_ack) != 0) {
            fprintf(stderr, "No se pudo guardar localmente la confirmacion de alarma %s.\n", codigo);
        }
    }
}

static int ejecutar_escuchar(int duracion_s) {
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

    detener_programa = 0;
    signal(SIGINT, manejar_senal);
    signal(SIGTERM, manejar_senal);
#ifdef _WIN32
    SetConsoleCtrlHandler(manejar_consola_win, TRUE);
#endif

    ContextoEscucha ctx;
    ctx.db = db;
    ctx.host = host;
    ctx.token = token;

    time_t limite = (duracion_s > 0) ? (time(NULL) + duracion_s) : 0;

    printf("Escuchando alarmas en itla/planta/%s/alarma (Ctrl+C para detener)...\n", matricula);
    fflush(stdout);

    while (!detener_programa) {
        if (limite > 0 && time(NULL) >= limite) {
            break;
        }

        int rc = red_escuchar_alarmas_mqtt(host, matricula, token,
                                           &detener_programa, limite,
                                           procesar_mensaje_alarma, &ctx);

        if (rc == 0 || detener_programa || (limite > 0 && time(NULL) >= limite)) {
            break;
        }

        printf("[MQTT ESCUCHA] Reintentando conexion en 2s...\n");
        fflush(stdout);
        for (int i = 0; i < 20 && !detener_programa; i++) {
            if (limite > 0 && time(NULL) >= limite) {
                break;
            }
            dormir_ms(100);
        }
    }

    printf("\nDeteniendo escucha de alarmas y cerrando SQLite...\n");
    datos_cerrar(db);
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Uso: %s <subcomando>\n", argv[0]);
        return 1;
    }

    int rc = 1;
    if (strcmp(argv[1], "config") == 0) {
        rc = ejecutar_config();
    } else if (strcmp(argv[1], "descargar") == 0) {
        rc = ejecutar_descargar();
    } else if (strcmp(argv[1], "analizar") == 0) {
        rc = ejecutar_analizar();
    } else if (strcmp(argv[1], "enviar") == 0) {
        int duracion_s = (argc >= 3) ? atoi(argv[2]) : 0;
        rc = ejecutar_enviar(duracion_s);
    } else if (strcmp(argv[1], "escuchar") == 0) {
        int duracion_s = (argc >= 3) ? atoi(argv[2]) : 0;
        rc = ejecutar_escuchar(duracion_s);
    } else {
        fprintf(stderr, "Subcomando desconocido: %s\n", argv[1]);
    }

    red_limpiar();
    return rc;
}





