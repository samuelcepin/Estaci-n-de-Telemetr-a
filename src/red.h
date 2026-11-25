#ifndef RED_H
#define RED_H

/* Devuelve un buffer dinamico con la respuesta HTTP 200 (liberar con free), o NULL si falla */
char *red_obtener_config(const char *host, const char *token);

#endif