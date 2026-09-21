#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "dag.h"

#define MAX_LINEA 4096

/* saca los espacios del principio y del final (modifica el string) */
static char *trim(char *s) {
    while (isspace((unsigned char)*s)) {
        s++;
    }
    if (*s == '\0') {
        return s;
    }
    char *fin = s + strlen(s) - 1;
    while (fin > s && isspace((unsigned char)*fin)) {
        *fin = '\0';
        fin--;
    }
    return s;
}

/* parte la linea en 4 campos. devuelve 0 si esta bien, -1 si no */
static int separar_campos(char *linea, char **id, char **nombre,
                          char **tiempo, char **deps) {
    char *p1 = strchr(linea, ':');
    if (p1 == NULL) return -1;
    char *p2 = strchr(p1 + 1, ':');
    if (p2 == NULL) return -1;
    char *p3 = strchr(p2 + 1, ':');
    if (p3 == NULL) return -1;

    *p1 = '\0';
    *p2 = '\0';
    *p3 = '\0';

    *id = trim(linea);
    *nombre = trim(p1 + 1);
    *tiempo = trim(p2 + 1);
    *deps = trim(p3 + 1);
    return 0;
}

int dag_load(const char *path, dag_t *g) {
    FILE *f = fopen(path, "r");
    if (f == NULL) {
        perror("No se pudo abrir el archivo");
        return -1;
    }

    char linea[MAX_LINEA];
    int num_linea = 0;

    while (fgets(linea, MAX_LINEA, f) != NULL) {
        num_linea++;
        linea[strcspn(linea, "\n")] = '\0';

        char *copia = trim(linea);
        /* ignoro lineas vacias y comentarios */
        if (copia[0] == '\0' || copia[0] == '#') {
            continue;
        }

        char *id, *nombre, *tiempo, *deps;
        if (separar_campos(linea, &id, &nombre, &tiempo, &deps) < 0) {
            fprintf(stderr, "Linea %d mal formada\n", num_linea);
            fclose(f);
            return -1;
        }
        printf("linea %d: id=[%s] nombre=[%s] tiempo=[%s] deps=[%s]\n",
               num_linea, id, nombre, tiempo, deps);
    }

    fclose(f);

    /* por ahora no armamos el grafo */
    g->nodes = NULL;
    g->n = 0;
    return 0;
}

void dag_free(dag_t *g) {
    (void)g;
}
