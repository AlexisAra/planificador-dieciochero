#ifndef DAG_H
#define DAG_H

#include <stddef.h>
#include <sys/types.h>

#define MAX_MSG 64

typedef enum {
    ST_PENDING,   /* esperando dependencias */
    ST_READY,     /* listo para lanzar */
    ST_RUNNING,   /* proceso vivo */
    ST_DONE,      /* termino bien */
    ST_FAILED,    /* fallo */
    ST_ABORTED    /* abortada por fallo de ancestro o SIGINT */
} state_t;

typedef struct {
    char   *id;
    char   *name;
    int     time_ms;
    int     ndeps;
    int     pending;
    int    *children;
    int     nchildren;
    int    *parents;
    int     nparents;
    state_t state;
    pid_t   pid;
    char    inbox[MAX_MSG * 4];
} node_t;

typedef struct {
    node_t *nodes;
    int     n;
} dag_t;

/* Parser */
int  dag_load(const char *path, dag_t *g);   /* 0 ok, -1 error */
void dag_free(dag_t *g);

/* Hijo */
void child_run(const node_t *nd, int write_fd);

/* Planificador */
int  scheduler_run(dag_t *g, int K);

#endif
