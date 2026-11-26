#include "datos.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int comprobar(int condicion, const char *mensaje) {
    if (!condicion) {
        fprintf(stderr, "FALLO: %s\n", mensaje);
        return 0;
    }
    return 1;
}

static int ejecutar_lectura(sqlite3 *db, long long id, double temp,
                            int tiene_vibracion, const char *estado) {
    LecturaPlanta lectura;
    memset(&lectura, 0, sizeof(lectura));
    lectura.id = id;
    lectura.maquina = "M1";
    lectura.ts = "1764000000";
    lectura.tiene_temp = 1;
    lectura.temp = temp;
    lectura.tiene_corriente = 1;
    lectura.corriente = 2.5;
    lectura.tiene_vibracion = tiene_vibracion;
    lectura.vibracion = 0.25;
    lectura.estado = estado;
    return datos_guardar_lectura(db, &lectura) == 0;
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "Uso: %s <ruta_db_temporal>\n", argv[0]);
        return 2;
    }

    sqlite3 *db = NULL;
    if (!comprobar(datos_abrir(argv[1], &db) == 0, "abrir base SQLite")) {
        return 1;
    }

    int ok = 1;
    sqlite3_stmt *stmt = NULL;
    const char *sql_wal = "PRAGMA journal_mode;";
    if (sqlite3_prepare_v2(db, sql_wal, -1, &stmt, NULL) != SQLITE_OK) {
        ok = comprobar(0, "preparar consulta journal_mode");
    } else {
        int rc = sqlite3_step(stmt);
        const unsigned char *modo = (rc == SQLITE_ROW) ? sqlite3_column_text(stmt, 0) : NULL;
        ok &= comprobar(modo && strcmp((const char *)modo, "wal") == 0,
                        "journal_mode debe ser WAL");
        sqlite3_finalize(stmt);
        stmt = NULL;
    }

    ok &= comprobar(datos_guardar_config(db, "umbral_temp", 11, "45.00", 5) == 0,
                    "guardar configuracion mediante sentencia preparada");
    ok &= comprobar(datos_iniciar_transaccion(db) == 0, "iniciar transaccion de lecturas");

    ok &= comprobar(ejecutar_lectura(db, 1, 40.0, 1, "OK"), "insertar lectura valida 1");
    ok &= comprobar(ejecutar_lectura(db, 2, 50.0, 1, "OK"), "insertar lectura valida 2");
    ok &= comprobar(ejecutar_lectura(db, 3, -999.0, 1, "OK"), "insertar temperatura invalida");
    ok &= comprobar(ejecutar_lectura(db, 4, 55.0, 0, "OK"), "insertar campo faltante");
    ok &= comprobar(ejecutar_lectura(db, 5, 70.0, 1, "FALLA"), "insertar estado invalido");
    /* ID duplicado: INSERT OR IGNORE debe conservar la primera fila. */
    ok &= comprobar(ejecutar_lectura(db, 1, 99.0, 1, "OK"), "probar insercion duplicada");
    ok &= comprobar(datos_confirmar_transaccion(db) == 0, "confirmar transaccion");

    long long filas = 0;
    long long suma_ids = 0;
    ok &= comprobar(datos_resumen_lecturas(db, &filas, &suma_ids) == 0,
                    "consultar resumen de lecturas");
    ok &= comprobar(filas == 5 && suma_ids == 15,
                    "PRIMARY KEY debe impedir duplicados (5 filas, suma 15)");

    EstadisticaMaquina *estadisticas = NULL;
    int total = 0;
    ok &= comprobar(datos_analizar_maquinas(db, 45.0, &estadisticas, &total) == 0,
                    "ejecutar estadisticas SQL");
    ok &= comprobar(total == 1, "debe existir una maquina en la prueba");
    if (total == 1) {
        const EstadisticaMaquina *m = &estadisticas[0];
        ok &= comprobar(strcmp(m->maquina, "M1") == 0, "nombre de maquina M1");
        ok &= comprobar(m->validas == 2 && m->descartadas == 3,
                        "contar 2 validas y 3 descartadas");
        ok &= comprobar(fabs(m->temp_min - 40.0) < 0.001, "temperatura minima 40");
        ok &= comprobar(fabs(m->temp_max - 50.0) < 0.001, "temperatura maxima 50");
        ok &= comprobar(fabs(m->temp_prom - 45.0) < 0.001, "temperatura promedio 45");
        ok &= comprobar(m->sobre_umbral == 1, "una lectura por encima de 45 grados");
    }
    free(estadisticas);

    long long seq = 0;
    ok &= comprobar(datos_siguiente_seq_telemetria(db, &seq) == 0 && seq == 1,
                    "primera secuencia de telemetria igual a 1");
    RegistroTelemetria reg;
    memset(&reg, 0, sizeof(reg));
    reg.seq = seq;
    reg.ts = 1764000000;
    reg.temp = 32.5;
    reg.corriente = 3.2;
    ok &= comprobar(datos_guardar_telemetria(db, &reg) == 0, "guardar telemetria pendiente");
    RegistroTelemetria pendiente;
    int hay_pendiente = 0;
    ok &= comprobar(datos_obtener_telemetria_pendiente(db, &pendiente, &hay_pendiente) == 0 &&
                    hay_pendiente && pendiente.seq == 1,
                    "recuperar primera telemetria pendiente");
    ok &= comprobar(datos_marcar_telemetria_enviada(db, 1) == 0,
                    "marcar telemetria como enviada");
    ok &= comprobar(datos_obtener_telemetria_pendiente(db, &pendiente, &hay_pendiente) == 0 &&
                    !hay_pendiente,
                    "no debe quedar telemetria pendiente despues del ACK local");

    int confirmada = 1;
    ok &= comprobar(datos_guardar_alarma(db, "TST001", "M1", 1764000000, 1764000001) == 0,
                    "guardar alarma");
    ok &= comprobar(datos_alarma_ya_confirmada(db, "TST001", &confirmada) == 0 && !confirmada,
                    "alarma nueva debe figurar como no confirmada");
    ok &= comprobar(datos_marcar_alarma_confirmada(db, "TST001", 1764000002) == 0,
                    "marcar alarma como confirmada");
    ok &= comprobar(datos_alarma_ya_confirmada(db, "TST001", &confirmada) == 0 && confirmada,
                    "alarma confirmada debe detectarse como tal");

    datos_cerrar(db);
    if (ok) {
        puts("OK: prueba SQLite superada (WAL, tablas, duplicados, SQL, cola y alarmas).");
        return 0;
    }
    return 1;
}
