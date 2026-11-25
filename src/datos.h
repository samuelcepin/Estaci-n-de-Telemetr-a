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

typedef struct {
    char maquina[64];
    long long validas;
    long long descartadas;
    double temp_min;
    double temp_max;
    double temp_prom;
    long long sobre_umbral;
} EstadisticaMaquina;

typedef struct {
    long long seq;
    long long ts;
    double temp;
    double corriente;
    int enviado;
    int intentos;
} RegistroTelemetria;

int datos_abrir(const char *ruta_db, sqlite3 **db_out);
int datos_guardar_config(sqlite3 *db, const char *clave, int len_clave,
                         const char *valor, int len_valor);

int datos_iniciar_transaccion(sqlite3 *db);
int datos_confirmar_transaccion(sqlite3 *db);
int datos_guardar_lectura(sqlite3 *db, const LecturaPlanta *lectura);
int datos_resumen_lecturas(sqlite3 *db, long long *filas_unicas, long long *suma_ids);

int datos_obtener_umbral_temp(sqlite3 *db, double *umbral_out);
int datos_analizar_maquinas(sqlite3 *db, double umbral_temp,
                            EstadisticaMaquina **lista_out, int *total_out);

int datos_obtener_intervalo_s(sqlite3 *db, int *intervalo_out);
int datos_siguiente_seq_telemetria(sqlite3 *db, long long *seq_out);
int datos_guardar_telemetria(sqlite3 *db, const RegistroTelemetria *reg);
int datos_obtener_telemetria_pendiente(sqlite3 *db, RegistroTelemetria *reg_out, int *hay_pendiente);
int datos_marcar_telemetria_enviada(sqlite3 *db, long long seq);
int datos_registrar_intento_telemetria(sqlite3 *db, long long seq);

void datos_cerrar(sqlite3 *db);

#endif


