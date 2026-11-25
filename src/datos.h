#ifndef DATOS_H
#define DATOS_H

#include <sqlite3.h>

typedef struct {
    long long id;
    const char *maquina;
    const char *ts;
    int tiene_temp;
    double temp;
    int tiene_corriente;
    double corriente;
    int tiene_vibracion;
    double vibracion;
    const char *estado;
} LecturaPlanta;

int datos_abrir(const char *ruta_db, sqlite3 **db_out);
int datos_guardar_config(sqlite3 *db, const char *clave, int len_clave,
                         const char *valor, int len_valor);

int datos_iniciar_transaccion(sqlite3 *db);
int datos_confirmar_transaccion(sqlite3 *db);
int datos_guardar_lectura(sqlite3 *db, const LecturaPlanta *lectura);
int datos_resumen_lecturas(sqlite3 *db, long long *filas_unicas, long long *suma_ids);

void datos_cerrar(sqlite3 *db);

#endif