#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "dag.h"

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Uso: %s plan.txt\n", argv[0]);
        return 1;
    }
    srand((unsigned)time(NULL));

    dag_t g;
    if (dag_load(argv[1], &g) < 0) {
        fprintf(stderr, "Error cargando %s\n", argv[1]);
        return 1;
    }
    for (int i = 0; i < g.n; i++) {
        printf("%s (%s) %dms deps=%d\n", g.nodes[i].id, g.nodes[i].name,
               g.nodes[i].time_ms, g.nodes[i].ndeps);
    }
    dag_free(&g);
    return 0;
}
