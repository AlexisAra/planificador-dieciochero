#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/wait.h>
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

    printf("--- prueba: lanzar el nodo 0 como hijo ---\n");

    int fds[2];
    if (pipe(fds) < 0) {
        perror("pipe");
        dag_free(&g);
        return 1;
    }

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        dag_free(&g);
        return 1;
    }

    if (pid == 0) {
        close(fds[0]);
        child_run(&g.nodes[0], fds[1]);
    }

    close(fds[1]);
    char buf[128];
    ssize_t n = read(fds[0], buf, sizeof(buf) - 1);
    if (n > 0) {
        buf[n] = '\0';
        printf("[padre] recibi del hijo: '%s'\n", buf);
    } else {
        printf("[padre] no recibi nada (el hijo probablemente fallo)\n");
    }
    close(fds[0]);

    int status;
    waitpid(pid, &status, 0);
    if (WIFEXITED(status)) {
        printf("[padre] el hijo termino con codigo %d\n", WEXITSTATUS(status));
    }

    dag_free(&g);
    return 0;
}
