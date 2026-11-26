#ifndef SENSOR_H
#define SENSOR_H

#include "datos.h"

void sensor_inicializar(void);
void sensor_leer(long long seq, RegistroTelemetria *reg);

#endif

