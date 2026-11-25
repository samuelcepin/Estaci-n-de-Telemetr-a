#ifndef DATOS_H
#define DATOS_H

#include <sqlite3.h>

int datos_abrir(const char *ruta_db, sqlite3 **db_out);
int datos_guardar_config(sqlite3 *db, const char *clave, int len_clave,
                         const char *valor, int len_valor);
void datos_cerrar(sqlite3 *db);

#endif
