#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <errno.h>
#include <limits.h>

#include "dag.h"

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "Uso: %s plan.txt K\n", argv[0]);
        return 1;
    }

    char *end = NULL;
    errno = 0;
    long k_long = strtol(argv[2], &end, 10);

    if (errno != 0 || end == argv[2] || *end != '\0' ||
        k_long <= 0 || k_long > INT_MAX) {
        fprintf(stderr, "K invalido: %s\n", argv[2]);
        return 1;
    }
    int K = (int)k_long;

    srand((unsigned)time(NULL));

    dag_t g;
    if (dag_load(argv[1], &g) < 0) {
        fprintf(stderr, "Error cargando %s\n", argv[1]);
        return 1;
    }

    int result = scheduler_run(&g, K);

    printf("=== resultado final ===\n");
    for (int i = 0; i < g.n; i++) {
        const char *estado_txt;
        switch (g.nodes[i].state) {
            case ST_DONE:    estado_txt = "completada"; break;
            case ST_FAILED:  estado_txt = "fallida"; break;
            case ST_ABORTED: estado_txt = "abortada"; break;
            default:         estado_txt = "sin terminar"; break;
        }
        printf("%s (%s): %s\n", g.nodes[i].id, g.nodes[i].name, estado_txt);
    }

    dag_free(&g);

    if (result == 130) {
        fprintf(stderr, "Planificacion abortada por Ctrl+C\n");
        return 130;
    }
    if (result < 0) {
        fprintf(stderr, "Error durante la planificacion\n");
        return 1;
    }
    return 0;
}
