#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "dag.h"

/* probabilidad de que una actividad falle, de 0 a 100 */
#define PROB_FALLO 5

/* duerme 'ms' milisegundos */
static void dormir_ms(int ms) {
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

void child_run(const node_t *nd, int write_fd) {
    /* cada hijo necesita su propia semilla, si no todos sortean lo mismo */
    srand((unsigned)(time(NULL) ^ getpid()));

    printf("[hijo %d] '%s' (%s) empieza, insumos recibidos: [%s]\n",
           getpid(), nd->id, nd->name, nd->inbox);
    fflush(stdout);

    dormir_ms(nd->time_ms);

    int fallo = (rand() % 100) < PROB_FALLO;
    if (fallo) {
        fprintf(stderr, "[hijo %d] '%s' (%s) FALLO\n", getpid(), nd->id, nd->name);
        close(write_fd);
        _exit(1);
    }

    char msg[MAX_MSG];
    snprintf(msg, sizeof(msg), "%s:ok", nd->id);
    ssize_t escritos = write(write_fd, msg, strlen(msg));
    if (escritos < 0) {
        perror("write en hijo");
        close(write_fd);
        _exit(1);
    }

    close(write_fd);
    _exit(0);
}
