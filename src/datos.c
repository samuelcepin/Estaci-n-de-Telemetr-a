#include "datos.h"
#include <stdio.h>

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

void datos_cerrar(sqlite3 *db) {
    if (db) {
        sqlite3_close(db);
    }
}

