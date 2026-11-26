#include "sensor.h"

#include <math.h>
#include <stdlib.h>
#include <time.h>

void sensor_inicializar(void) {
    srand((unsigned int)time(NULL));
}

void sensor_leer(long long seq, RegistroTelemetria *reg) {
    if (!reg) {
        return;
    }

    /* Onda lenta entre aproximadamente 27 y 43 °C, con ruido pequeño. */
    double ruido_temp = ((double)(rand() % 101) / 100.0) - 0.5;
    double fase = (double)seq * 0.035;
    reg->seq = seq;
    reg->ts = (long long)time(NULL);
    reg->temp = 35.0 + 7.5 * sin(fase) + ruido_temp;

    /* Corriente simulada entre 2.5 y 4.5 A. */
    reg->corriente = 3.5 + ((double)(rand() % 201) / 100.0) - 1.0;
    reg->enviado = 0;
    reg->intentos = 0;
}

