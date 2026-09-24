#include <stdio.h>
#include <stdlib.h>
#include "dag.h"

/*
 * Cola de nodos listos*/
typedef struct {
    int *items;
    int head;
    int tail;
    int capacity;
} ready_queue_t;


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
 *se libera la memoria utilizada por la cola.
 */
static void ready_queue_free(ready_queue_t *q)
{
    free(q->items);
    q->items = NULL;
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

    ready_queue_free(&ready);

    return 0;
}
	
