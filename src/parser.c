#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dag.h"

#define MAX_LINEA 4096

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
        /* le saco el salto de linea del final */
        linea[strcspn(linea, "\n")] = '\0';
        printf("linea %d: [%s]\n", num_linea, linea);
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
