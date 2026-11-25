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

    const char *sql_tabla =
        "CREATE TABLE IF NOT EXISTS config ("
        "  clave TEXT PRIMARY KEY,"
        "  valor TEXT NOT NULL"
        ");";

    if (sqlite3_exec(*db_out, sql_tabla, NULL, NULL, &err_msg) != SQLITE_OK) {
        fprintf(stderr, "Error creando tabla config: %s\n", err_msg);
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
        fprintf(stderr, "Error preparando consulta: %s\n", sqlite3_errmsg(db));
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

void datos_cerrar(sqlite3 *db) {
    if (db) {
        sqlite3_close(db);
    }
}