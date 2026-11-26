#include "sensor.h"
#include <stdlib.h>
#include <time.h>

void sensor_inicializar(void) {
    srand((unsigned int)time(NULL));
}

void sensor_leer(long long seq, RegistroTelemetria *reg) {
    if (!reg) {
        return;
    }

    reg->seq = seq;
    reg->ts = (long long)time(NULL);
    reg->temp = 27.50 + ((double)(rand() % 800) / 100.0);
    reg->corriente = 10.50 + ((double)(rand() % 350) / 100.0);
    reg->enviado = 0;
    reg->intentos = 0;
}

