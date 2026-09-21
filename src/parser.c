#include "dag.h"

int dag_load(const char *path, dag_t *g) {
    (void)path;
    g->nodes = NULL;
    g->n = 0;
    return 0;
}

void dag_free(dag_t *g) {
    (void)g;
}
