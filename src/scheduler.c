#include <stdio.h>
#include <stdlib.h>
#include "dag.h"
#include <unistd.h>
#include <sys/wait.h>
/*
 * Cola de nodos listos*/
typedef struct {
    int *items;
    int head;
    int tail;
    int capacity;
} ready_queue_t;

typedef struct {
    int node_index;
    int read_fd;
} active_child_t;
/*
 * Se inicia la cola.
 * retorna 0 si funciona y -1 si no se pudo guardar  memoria.
 */
static int ready_queue_init(ready_queue_t *q, int capacity)
{
    q->items = malloc((size_t)capacity * sizeof(int));

    if (q->items == NULL) {
        return -1;
    }

    q->head = 0;
    q->tail = 0;
    q->capacity = capacity;

    return 0;
}


/*
 * Agrega el indice de un nodo al final de la cola.
 */
static int ready_queue_push(ready_queue_t *q, int node_index)
{
    if (q->tail >= q->capacity) {
        return -1;
    }

    q->items[q->tail] = node_index;
    q->tail++;

    return 0;
}

/*
 * saca el primer nodo disponible de la cola.
 * retorna 0 si habia un elemento y -1 si estaba vacia.
 */
static int ready_queue_pop(ready_queue_t *q, int *node_index)
{
    if (q->head >= q->tail) {
        return -1;
    }

    *node_index = q->items[q->head];
    q->head++;

    return 0;
}





/*
 *se libera la memoria utilizada por la cola.
 */
static void ready_queue_free(ready_queue_t *q)
{
    free(q->items);
    q->items = NULL;
}



/*
 * lanza un nodo como proceso hijo.
 *
 * el hijo conserva el extremo de escritura del pipe.
 * el padre conserva el extremo de lectura.
 */
static int launch_node(dag_t *g, int node_index, active_child_t *active)
{
    int fds[2];

    if (pipe(fds) < 0) {
        perror("pipe");
        return -1;
    }

    pid_t pid = fork();

    if (pid < 0) {
        perror("fork");
        close(fds[0]);
        close(fds[1]);
        return -1;
    }

    if (pid == 0) {
        /*
         * estamos en el hijo.
         * el hijo no necesita leer desde su propio pipe.
         */
        close(fds[0]);

        child_run(&g->nodes[node_index], fds[1]);

        /*
         * child_run actualmente termina con _exit(),
         * pero dejamos esto por seguridad.
         */
        _exit(1);
    }

    /*
     *desde aqui solo ejecuta el padre.
     *el padre no necesita escribir en este pipe.
     */
    close(fds[1]);

    g->nodes[node_index].pid = pid;
    g->nodes[node_index].state = ST_RUNNING;

    active->node_index = node_index;
    active->read_fd = fds[0];

    return 0;
}


/*
 *se ejecuta el planificador.
 *
 * buscar los nodos que no tienen dependencias pendientes
 * y agregar a la cola READY.
 */
int scheduler_run(dag_t *g, int K)
{
    if (g == NULL || K <= 0) {
        return -1;
    }

    ready_queue_t ready;


    /*
     * Aunque normalmente tendremos al menos un nodo,
     * usamos capacidad 1 para manejar tambien un DAG vacio.
     */
    int capacity = (g->n > 0) ? g->n : 1;

    if (ready_queue_init(&ready, capacity) < 0) {
        fprintf(stderr, "scheduler: sin memoria para cola READY\n");
        return -1;
    }

    for (int i = 0; i < g->n; i++) {
        if (g->nodes[i].pending == 0) {
            g->nodes[i].state = ST_READY;

            if (ready_queue_push(&ready, i) < 0) {
                fprintf(stderr, "scheduler: cola READY llena\n");
                ready_queue_free(&ready);
                return -1;
            }
        }
    }


    int max_active = (K < g->n) ? K : g->n;

    if (max_active == 0) {
        ready_queue_free(&ready);
        return 0;
    }

    active_child_t *active =
        malloc((size_t)max_active * sizeof(active_child_t));

    if (active == NULL) {
        fprintf(stderr, "scheduler: sin memoria para hijos activos\n");
        ready_queue_free(&ready);
        return -1;
    }

    for (;;) {
        int running = 0;

        /*
         * Lanzamos nodos mientras haya nodos READY
         * y no superemos el limite K.
         */
        while (running < max_active) {
            int node_index;

            if (ready_queue_pop(&ready, &node_index) < 0) {
                break;
            }

            if (launch_node(g, node_index, &active[running]) < 0) {

                /*
                 * Si no pudimos lanzar uno, esperamos los que
                 * ya habiamos creado antes de salir.
                 */
                for (int j = 0; j < running; j++) {
                    int status;
                    int idx = active[j].node_index;

                    waitpid(g->nodes[idx].pid, &status, 0);
                    close(active[j].read_fd);
                }

                free(active);
                ready_queue_free(&ready);
                return -1;
            }

            running++;
        }

        /*
         * Si no pudimos sacar ningun nodo de READY,
         * terminamos esta etapa.
         */
        if (running == 0) {
            break;
        }

        /*
         * Temporalmente esperamos los procesos creados.
         *
         * En el siguiente commit reemplazaremos esta parte
         * por poll(), que sera el mecanismo definitivo.
         */
        for (int i = 0; i < running; i++) {
            int status;
            int node_index = active[i].node_index;
            node_t *nd = &g->nodes[node_index];

            if (waitpid(nd->pid, &status, 0) < 0) {
                perror("waitpid");
                nd->state = ST_FAILED;
            } else if (WIFEXITED(status) &&
                       WEXITSTATUS(status) == 0) {
                nd->state = ST_DONE;
            } else {
                nd->state = ST_FAILED;
            }

            close(active[i].read_fd);
        }
    }

    free(active);

    ready_queue_free(&ready);

    return 0;
}
	
