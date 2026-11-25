#ifndef RED_H
#define RED_H

/* Devuelve un buffer dinamico con la respuesta HTTP 200 (liberar con free), o NULL si falla */
char *red_obtener_config(const char *host, const char *token);

/* Descarga un lote CSV de lecturas desde el ID indicado (liberar con free), o NULL si falla */
char *red_obtener_lecturas(const char *host, const char *token, long long desde_id, int limite);

/* Envia el resumen JSON de filas unicas y suma de IDs. Devuelve 0 si el servidor acepta (HTTP 2xx) */
int red_enviar_resumen_descarga(const char *host, const char *token,
                                long long filas_unicas, long long suma_ids);

#endif

