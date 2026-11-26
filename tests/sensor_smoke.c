#include "sensor.h"

#include <stdio.h>

int main(void) {
    sensor_inicializar();

    double temp_min = 1000.0;
    double temp_max = -1000.0;
    double corriente_min = 1000.0;
    double corriente_max = -1000.0;

    for (long long seq = 1; seq <= 2000; ++seq) {
        RegistroTelemetria lectura;
        sensor_leer(seq, &lectura);

        if (lectura.seq != seq || lectura.ts <= 0 || lectura.enviado != 0 || lectura.intentos != 0) {
            fprintf(stderr, "FALLO: campos basicos incorrectos en seq=%lld\n", seq);
            return 1;
        }
        if (lectura.temp < 25.0 || lectura.temp > 45.0) {
            fprintf(stderr, "FALLO: temperatura fuera de rango: %.2f\n", lectura.temp);
            return 1;
        }
        if (lectura.corriente < 2.0 || lectura.corriente > 5.0) {
            fprintf(stderr, "FALLO: corriente fuera de rango: %.2f\n", lectura.corriente);
            return 1;
        }

        if (lectura.temp < temp_min) temp_min = lectura.temp;
        if (lectura.temp > temp_max) temp_max = lectura.temp;
        if (lectura.corriente < corriente_min) corriente_min = lectura.corriente;
        if (lectura.corriente > corriente_max) corriente_max = lectura.corriente;
    }

    printf("OK: sensor simulado dentro de rango; temp %.2f..%.2f C, corriente %.2f..%.2f A.\n",
           temp_min, temp_max, corriente_min, corriente_max);
    return 0;
}
