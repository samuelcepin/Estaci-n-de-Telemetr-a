#include "datos.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int datos_abrir(const char *ruta_db, sqlite3 **db_out) {
    if (sqlite3_open(ruta_db, db_out) != SQLITE_OK) {
        fprintf(stderr, "Error abriendo SQLite (%s): %s\n",
                ruta_db, *db_out ? sqlite3_errmsg(*db_out) : "sin memoria");
        if (*db_out) {
            sqlite3_close(*db_out);
            *db_out = NULL;
        }
        return -1;
    }

    sqlite3_busy_timeout(*db_out, 2000);

    char *err_msg = NULL;
    if (sqlite3_exec(*db_out, "PRAGMA journal_mode=WAL;", NULL, NULL, &err_msg) != SQLITE_OK) {
        fprintf(stderr, "Error activando modo WAL: %s\n", err_msg);
        sqlite3_free(err_msg);
        sqlite3_close(*db_out);
        *db_out = NULL;
        return -1;
    }

    const char *sql_config =
        "CREATE TABLE IF NOT EXISTS config ("
        "  clave TEXT PRIMARY KEY,"
        "  valor TEXT NOT NULL"
        ");";

    if (sqlite3_exec(*db_out, sql_config, NULL, NULL, &err_msg) != SQLITE_OK) {
        fprintf(stderr, "Error creando tabla config: %s\n", err_msg);
        sqlite3_free(err_msg);
        sqlite3_close(*db_out);
        *db_out = NULL;
        return -1;
    }

    const char *sql_lecturas =
        "CREATE TABLE IF NOT EXISTS lecturas_planta ("
        "  id INTEGER PRIMARY KEY,"
        "  maquina TEXT,"
        "  ts TEXT,"
        "  temp REAL,"
        "  corriente REAL,"
        "  vibracion REAL,"
        "  estado TEXT"
        ");";

    if (sqlite3_exec(*db_out, sql_lecturas, NULL, NULL, &err_msg) != SQLITE_OK) {
        fprintf(stderr, "Error creando tabla lecturas_planta: %s\n", err_msg);
        sqlite3_free(err_msg);
        sqlite3_close(*db_out);
        *db_out = NULL;
        return -1;
    }

    const char *sql_telemetria =
        "CREATE TABLE IF NOT EXISTS telemetria ("
        "  seq INTEGER PRIMARY KEY,"
        "  ts INTEGER NOT NULL,"
        "  temp REAL NOT NULL,"
        "  corriente REAL NOT NULL,"
        "  enviado INTEGER NOT NULL DEFAULT 0,"
        "  intentos INTEGER NOT NULL DEFAULT 0"
        ");";

    if (sqlite3_exec(*db_out, sql_telemetria, NULL, NULL, &err_msg) != SQLITE_OK) {
        fprintf(stderr, "Error creando tabla telemetria: %s\n", err_msg);
        sqlite3_free(err_msg);
        sqlite3_close(*db_out);
        *db_out = NULL;
        return -1;
    }

    return 0;
}

int datos_guardar_config(sqlite3 *db, const char *clave, int len_clave,
                         const char *valor, int len_valor) {
    const char *sql =
        "INSERT INTO config (clave, valor) VALUES (?, ?) "
        "ON CONFLICT(clave) DO UPDATE SET valor = excluded.valor;";

    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "Error preparando consulta config: %s\n", sqlite3_errmsg(db));
        return -1;
    }

    sqlite3_bind_text(stmt, 1, clave, len_clave, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, valor, len_valor, SQLITE_TRANSIENT);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        fprintf(stderr, "Error insertando configuracion: %s\n", sqlite3_errmsg(db));
        return -1;
    }

    return 0;
}

int datos_iniciar_transaccion(sqlite3 *db) {
    char *err_msg = NULL;
    if (sqlite3_exec(db, "BEGIN TRANSACTION;", NULL, NULL, &err_msg) != SQLITE_OK) {
        fprintf(stderr, "Error iniciando transaccion: %s\n", err_msg);
        sqlite3_free(err_msg);
        return -1;
    }
    return 0;
}

int datos_confirmar_transaccion(sqlite3 *db) {
    char *err_msg = NULL;
    if (sqlite3_exec(db, "COMMIT;", NULL, NULL, &err_msg) != SQLITE_OK) {
        fprintf(stderr, "Error confirmando transaccion: %s\n", err_msg);
        sqlite3_free(err_msg);
        return -1;
    }
    return 0;
}

int datos_guardar_lectura(sqlite3 *db, const LecturaPlanta *lectura) {
    const char *sql =
        "INSERT OR IGNORE INTO lecturas_planta "
        "(id, maquina, ts, temp, corriente, vibracion, estado) "
        "VALUES (?, ?, ?, ?, ?, ?, ?);";

    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "Error preparando consulta lectura: %s\n", sqlite3_errmsg(db));
        return -1;
    }

    sqlite3_bind_int64(stmt, 1, (sqlite3_int64)lectura->id);

    if (lectura->maquina && lectura->maquina[0] != '\0') {
        sqlite3_bind_text(stmt, 2, lectura->maquina, -1, SQLITE_TRANSIENT);
    } else {
        sqlite3_bind_null(stmt, 2);
    }

    if (lectura->ts && lectura->ts[0] != '\0') {
        sqlite3_bind_text(stmt, 3, lectura->ts, -1, SQLITE_TRANSIENT);
    } else {
        sqlite3_bind_null(stmt, 3);
    }

    if (lectura->tiene_temp) {
        sqlite3_bind_double(stmt, 4, lectura->temp);
    } else {
        sqlite3_bind_null(stmt, 4);
    }

    if (lectura->tiene_corriente) {
        sqlite3_bind_double(stmt, 5, lectura->corriente);
    } else {
        sqlite3_bind_null(stmt, 5);
    }

    if (lectura->tiene_vibracion) {
        sqlite3_bind_double(stmt, 6, lectura->vibracion);
    } else {
        sqlite3_bind_null(stmt, 6);
    }

    if (lectura->estado && lectura->estado[0] != '\0') {
        sqlite3_bind_text(stmt, 7, lectura->estado, -1, SQLITE_TRANSIENT);
    } else {
        sqlite3_bind_null(stmt, 7);
    }

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        fprintf(stderr, "Error insertando lectura: %s\n", sqlite3_errmsg(db));
        return -1;
    }

    return 0;
}

int datos_resumen_lecturas(sqlite3 *db, long long *filas_unicas, long long *suma_ids) {
    const char *sql = "SELECT COUNT(*), COALESCE(SUM(id), 0) FROM lecturas_planta;";

    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "Error preparando consulta resumen: %s\n", sqlite3_errmsg(db));
        return -1;
    }

    int rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        *filas_unicas = (long long)sqlite3_column_int64(stmt, 0);
        *suma_ids = (long long)sqlite3_column_int64(stmt, 1);
        sqlite3_finalize(stmt);
        return 0;
    }

    fprintf(stderr, "Error leyendo resumen de lecturas: %s\n", sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return -1;
}

int datos_obtener_umbral_temp(sqlite3 *db, double *umbral_out) {
    const char *sql = "SELECT valor FROM config WHERE clave = 'umbral_temp' LIMIT 1;";

    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "Error preparando consulta umbral_temp: %s\n", sqlite3_errmsg(db));
        return -1;
    }

    int rc = sqlite3_step(stmt);
    if (rc != SQLITE_ROW) {
        fprintf(stderr, "Error: umbral_temp no encontrado en config. Ejecuta './estacion config' primero.\n");
        sqlite3_finalize(stmt);
        return -1;
    }

    const unsigned char *texto = sqlite3_column_text(stmt, 0);
    if (!texto) {
        sqlite3_finalize(stmt);
        return -1;
    }

    char *fin = NULL;
    double val = strtod((const char *)texto, &fin);
    int valido = (fin != (const char *)texto && *fin == '\0');
    sqlite3_finalize(stmt);

    if (!valido) {
        fprintf(stderr, "Error: valor de umbral_temp invalido en config.\n");
        return -1;
    }

    *umbral_out = val;
    return 0;
}

int datos_analizar_maquinas(sqlite3 *db, double umbral_temp,
                            EstadisticaMaquina **lista_out, int *total_out) {
    #define COND_VALIDA \
        "(ts IS NOT NULL AND TRIM(ts) != '' " \
        "AND temp IS NOT NULL AND ABS(temp - (-999.0)) > 0.0001 " \
        "AND corriente IS NOT NULL " \
        "AND vibracion IS NOT NULL " \
        "AND estado = 'OK')"

    const char *sql =
        "SELECT "
        "  maquina, "
        "  SUM(CASE WHEN " COND_VALIDA " THEN 1 ELSE 0 END) AS validas, "
        "  SUM(CASE WHEN NOT " COND_VALIDA " THEN 1 ELSE 0 END) AS descartadas, "
        "  MIN(CASE WHEN " COND_VALIDA " THEN temp END) AS temp_min, "
        "  MAX(CASE WHEN " COND_VALIDA " THEN temp END) AS temp_max, "
        "  ROUND(AVG(CASE WHEN " COND_VALIDA " THEN temp END), 2) AS temp_prom, "
        "  SUM(CASE WHEN " COND_VALIDA " AND temp > ?1 THEN 1 ELSE 0 END) AS sobre_umbral "
        "FROM lecturas_planta "
        "WHERE maquina IS NOT NULL AND TRIM(maquina) != '' "
        "GROUP BY maquina "
        "ORDER BY maquina ASC;";

    #undef COND_VALIDA

    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "Error preparando consulta de analisis: %s\n", sqlite3_errmsg(db));
        return -1;
    }

    sqlite3_bind_double(stmt, 1, umbral_temp);

    EstadisticaMaquina *lista = NULL;
    int total = 0;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        EstadisticaMaquina *nueva = realloc(lista, (size_t)(total + 1) * sizeof(EstadisticaMaquina));
        if (!nueva) {
            fprintf(stderr, "Error de memoria leyendo estadisticas.\n");
            free(lista);
            sqlite3_finalize(stmt);
            return -1;
        }
        lista = nueva;

        EstadisticaMaquina *item = &lista[total];
        memset(item, 0, sizeof(*item));

        const unsigned char *maq = sqlite3_column_text(stmt, 0);
        snprintf(item->maquina, sizeof(item->maquina), "%s", maq ? (const char *)maq : "");

        item->validas = (long long)sqlite3_column_int64(stmt, 1);
        item->descartadas = (long long)sqlite3_column_int64(stmt, 2);
        item->temp_min = (sqlite3_column_type(stmt, 3) != SQLITE_NULL) ? sqlite3_column_double(stmt, 3) : 0.0;
        item->temp_max = (sqlite3_column_type(stmt, 4) != SQLITE_NULL) ? sqlite3_column_double(stmt, 4) : 0.0;
        item->temp_prom = (sqlite3_column_type(stmt, 5) != SQLITE_NULL) ? sqlite3_column_double(stmt, 5) : 0.0;
        item->sobre_umbral = (long long)sqlite3_column_int64(stmt, 6);

        total++;
    }

    sqlite3_finalize(stmt);

    *lista_out = lista;
    *total_out = total;
    return 0;
}

int datos_obtener_intervalo_s(sqlite3 *db, int *intervalo_out) {
    const char *sql = "SELECT valor FROM config WHERE clave = 'intervalo_s' LIMIT 1;";

    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "Error preparando consulta intervalo_s: %s\n", sqlite3_errmsg(db));
        return -1;
    }

    int rc = sqlite3_step(stmt);
    if (rc != SQLITE_ROW) {
        fprintf(stderr, "Error: intervalo_s no encontrado en config. Ejecuta './estacion config' primero.\n");
        sqlite3_finalize(stmt);
        return -1;
    }

    const unsigned char *texto = sqlite3_column_text(stmt, 0);
    if (!texto) {
        sqlite3_finalize(stmt);
        return -1;
    }

    char *fin = NULL;
    long val = strtol((const char *)texto, &fin, 10);
    int valido = (fin != (const char *)texto && *fin == '\0' && val > 0 && val <= 3600);
    sqlite3_finalize(stmt);

    if (!valido) {
        fprintf(stderr, "Error: valor de intervalo_s invalido en config.\n");
        return -1;
    }

    *intervalo_out = (int)val;
    return 0;
}

int datos_siguiente_seq_telemetria(sqlite3 *db, long long *seq_out) {
    const char *sql = "SELECT COALESCE(MAX(seq), 0) + 1 FROM telemetria;";

    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "Error preparando consulta seq telemetria: %s\n", sqlite3_errmsg(db));
        return -1;
    }

    int rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        *seq_out = (long long)sqlite3_column_int64(stmt, 0);
        sqlite3_finalize(stmt);
        return 0;
    }

    fprintf(stderr, "Error obteniendo siguiente seq: %s\n", sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return -1;
}

int datos_guardar_telemetria(sqlite3 *db, const RegistroTelemetria *reg) {
    const char *sql =
        "INSERT INTO telemetria (seq, ts, temp, corriente, enviado, intentos) "
        "VALUES (?, ?, ?, ?, 0, 0);";

    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "Error preparando insercion telemetria: %s\n", sqlite3_errmsg(db));
        return -1;
    }

    sqlite3_bind_int64(stmt, 1, (sqlite3_int64)reg->seq);
    sqlite3_bind_int64(stmt, 2, (sqlite3_int64)reg->ts);
    sqlite3_bind_double(stmt, 3, reg->temp);
    sqlite3_bind_double(stmt, 4, reg->corriente);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        fprintf(stderr, "Error guardando telemetria seq=%lld: %s\n",
                reg->seq, sqlite3_errmsg(db));
        return -1;
    }

    return 0;
}

int datos_obtener_telemetria_pendiente(sqlite3 *db, RegistroTelemetria *reg_out, int *hay_pendiente) {
    const char *sql =
        "SELECT seq, ts, temp, corriente, enviado, intentos "
        "FROM telemetria WHERE enviado = 0 ORDER BY seq ASC LIMIT 1;";

    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "Error preparando consulta pendiente: %s\n", sqlite3_errmsg(db));
        return -1;
    }

    int rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        reg_out->seq = (long long)sqlite3_column_int64(stmt, 0);
        reg_out->ts = (long long)sqlite3_column_int64(stmt, 1);
        reg_out->temp = sqlite3_column_double(stmt, 2);
        reg_out->corriente = sqlite3_column_double(stmt, 3);
        reg_out->enviado = sqlite3_column_int(stmt, 4);
        reg_out->intentos = sqlite3_column_int(stmt, 5);
        *hay_pendiente = 1;
        sqlite3_finalize(stmt);
        return 0;
    }

    if (rc == SQLITE_DONE) {
        *hay_pendiente = 0;
        sqlite3_finalize(stmt);
        return 0;
    }

    fprintf(stderr, "Error leyendo telemetria pendiente: %s\n", sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return -1;
}

int datos_contar_telemetria_pendiente(sqlite3 *db, long long *pendientes_out) {
    const char *sql = "SELECT COUNT(*) FROM telemetria WHERE enviado = 0;";

    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "Error preparando conteo de pendientes: %s\n", sqlite3_errmsg(db));
        return -1;
    }

    int rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        *pendientes_out = (long long)sqlite3_column_int64(stmt, 0);
        sqlite3_finalize(stmt);
        return 0;
    }

    fprintf(stderr, "Error contando telemetria pendiente: %s\n", sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return -1;
}

int datos_marcar_telemetria_enviada(sqlite3 *db, long long seq) {
    const char *sql =
        "UPDATE telemetria SET enviado = 1, intentos = intentos + 1 WHERE seq = ?;";

    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "Error preparando actualizacion enviada: %s\n", sqlite3_errmsg(db));
        return -1;
    }

    sqlite3_bind_int64(stmt, 1, (sqlite3_int64)seq);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        fprintf(stderr, "Error marcando enviada seq=%lld: %s\n", seq, sqlite3_errmsg(db));
        return -1;
    }

    return 0;
}

int datos_registrar_intento_telemetria(sqlite3 *db, long long seq) {
    const char *sql = "UPDATE telemetria SET intentos = intentos + 1 WHERE seq = ?;";

    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "Error preparando actualizacion intento: %s\n", sqlite3_errmsg(db));
        return -1;
    }

    sqlite3_bind_int64(stmt, 1, (sqlite3_int64)seq);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        fprintf(stderr, "Error registrando intento seq=%lld: %s\n", seq, sqlite3_errmsg(db));
        return -1;
    }

    return 0;
}

void datos_cerrar(sqlite3 *db) {
    if (db) {
        sqlite3_close(db);
    }
}







