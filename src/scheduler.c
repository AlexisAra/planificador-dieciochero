#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <poll.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>

#include "dag.h"


/* se ejecuta la cola de los nodos */
typedef struct {
    int *items;
    int head;
    int tail;
    int capacity;
} ready_queue_t;


/*
 * datos  que guarda el padre de cada hijo
 */
typedef struct {
    int node_index;
    int read_fd;

    int pipe_done;
    int exited;
    int status;

    char message[MAX_MSG];
} active_child_t;


/*
 * pipe usado para despertar poll()
 * cuando llega SIGINT o SIGCHLD.
 */
static int sigpipe[2] = {-1, -1};

static volatile sig_atomic_t got_sigint = 0;


/* COLA LISTA                                                */

static int ready_init(ready_queue_t *q, int capacity)
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


static int ready_push(ready_queue_t *q, int node_index)
{
    if (q->tail >= q->capacity) {
        return -1;
    }

    q->items[q->tail] = node_index;
    q->tail++;

    return 0;
}


static int ready_pop(ready_queue_t *q, int *node_index)
{
    if (q->head >= q->tail) {
        return -1;
    }

    *node_index = q->items[q->head];
    q->head++;

    return 0;
}


static void ready_free(ready_queue_t *q)
{
    free(q->items);
    q->items = NULL;
}

/*
 * aca solo marcamos SIGINT y escribimos un byte al self-pipe.
 */
static void signal_handler(int sig)
{
    unsigned char byte;

    if (sig == SIGINT) {
        got_sigint = 1;
        byte = 'I';
    } else {
        byte = 'C';
    }

    if (sigpipe[1] >= 0) {
        ssize_t ignored;

        ignored = write(sigpipe[1], &byte, 1);

        (void)ignored;
    }
}


/*
 * se deja  el descriptor no bloqueante.
 *
 * esto es importante porque el signal handler nunca
 * debe quedar bloqueado intentando escribir al pipe.
 */
static int make_nonblocking(int fd)
{
    int flags;

    flags = fcntl(fd, F_GETFL, 0);

    if (flags < 0) {
        return -1;
    }

    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        return -1;
    }

    return 0;
}


/*
 * se crea el self-pipe y se instalan  handlers para
 * SIGINT y SIGCHLD.
 */
static int setup_signals(struct sigaction *old_int,
                         struct sigaction *old_chld)
{
    got_sigint = 0;

    if (pipe(sigpipe) < 0) {
        perror("pipe senales");
        return -1;
    }

    if (make_nonblocking(sigpipe[0]) < 0 ||
        make_nonblocking(sigpipe[1]) < 0) {

        perror("fcntl");

        close(sigpipe[0]);
        close(sigpipe[1]);

        sigpipe[0] = -1;
        sigpipe[1] = -1;

        return -1;
    }

    struct sigaction sa;

    memset(&sa, 0, sizeof(sa));

    sa.sa_handler = signal_handler;

    sigemptyset(&sa.sa_mask);

    sa.sa_flags = SA_NOCLDSTOP;


    if (sigaction(SIGINT, &sa, old_int) < 0) {

        perror("sigaction SIGINT");

        close(sigpipe[0]);
        close(sigpipe[1]);

        sigpipe[0] = -1;
        sigpipe[1] = -1;

        return -1;
    }


    if (sigaction(SIGCHLD, &sa, old_chld) < 0) {

        perror("sigaction SIGCHLD");

        sigaction(SIGINT, old_int, NULL);

        close(sigpipe[0]);
        close(sigpipe[1]);

        sigpipe[0] = -1;
        sigpipe[1] = -1;

        return -1;
    }

    return 0;
}


/*
 * restaura  los handlers originales y cierra
 * los descriptores utilizados para señales.
 */
static void restore_signals(const struct sigaction *old_int,
                            const struct sigaction *old_chld)
{
    sigaction(SIGINT, old_int, NULL);

    sigaction(SIGCHLD, old_chld, NULL);


    if (sigpipe[0] >= 0) {
        close(sigpipe[0]);
    }

    if (sigpipe[1] >= 0) {
        close(sigpipe[1]);
    }


    sigpipe[0] = -1;
    sigpipe[1] = -1;
}


/*
 * se vacia  el self-pipe.
 */
static void drain_sigpipe(void)
{
    unsigned char buffer[64];

    for (;;) {

        ssize_t n;

        n = read(sigpipe[0],
                 buffer,
                 sizeof(buffer));

        if (n > 0) {
            continue;
        }

        if (n < 0 && errno == EINTR) {
            continue;
        }

        break;
    }
}


/*
 * los hijos heredan los handlers del padre despues
 * del fork().
 */

static void reset_signals_in_child(void)
{
    struct sigaction sa;

    memset(&sa, 0, sizeof(sa));

    sa.sa_handler = SIG_DFL;

    sigemptyset(&sa.sa_mask);


    sigaction(SIGINT, &sa, NULL);

    sigaction(SIGCHLD, &sa, NULL);


    if (sigpipe[0] >= 0) {
        close(sigpipe[0]);
    }

    if (sigpipe[1] >= 0) {
        close(sigpipe[1]);
    }
}



static int launch_node(dag_t *g,
                       int node_index,
                       active_child_t *active)
{
    int fds[2];

    if (pipe(fds) < 0) {

        perror("pipe");

        return -1;
    }


    pid_t pid;

    pid = fork();


    if (pid < 0) {

        perror("fork");

        close(fds[0]);
        close(fds[1]);

        return -1;
    }


    /*
     * el hijo
     */
    if (pid == 0) {

        /*
         * solamente escribe el hijo
         */
        close(fds[0]);

        reset_signals_in_child();

        child_run(&g->nodes[node_index],
                  fds[1]);

        /*
         * child_run deberia terminar con _exit().
         */
        _exit(1);
    }


    /*
     * El padre solamente lee.
     */
    close(fds[1]);


    g->nodes[node_index].pid = pid;

    g->nodes[node_index].state = ST_RUNNING;


    active->node_index = node_index;

    active->read_fd = fds[0];

    active->pipe_done = 0;

    active->exited = 0;

    active->status = 0;

    active->message[0] = '\0';


    return 0;
}


static int propagate_success(dag_t *g,
                             int node_index,
                             ready_queue_t *ready,
                             const char *message)
{
    node_t *node;

    node = &g->nodes[node_index];


    for (int i = 0;
         i < node->nchildren;
         i++) {

        int child_index;

        child_index = node->children[i];


        node_t *child;

        child = &g->nodes[child_index];


        /*
         * si esta rama ya fue abortada,
         * no hacemos nada.
         */
        if (child->state == ST_ABORTED) {
            continue;
        }


        /*
         * se guarda  el mensaje del padre
         * en el inbox del hijo.
         */
        if (message[0] != '\0') {

            size_t used;

            size_t capacity;

            used = strlen(child->inbox);

            capacity = sizeof(child->inbox);


            if (used < capacity - 1) {

                snprintf(child->inbox + used,
                         capacity - used,
                         "%s%s",
                         used > 0 ? "\n" : "",
                         message);
            }
        }


        /*
         * una  dependencia ya termino.
         */
        if (child->pending > 0) {
            child->pending--;
        }


        /*
         * si ya no quedan dependencias,
         * el hijo pasa a READY.
         */
        if (child->pending == 0 &&
            child->state == ST_PENDING) {

            child->state = ST_READY;


            if (ready_push(ready,
                           child_index) < 0) {

                return -1;
            }
        }
    }


    return 0;
}


/*
 * aborta todos los descendientes de un nodo fallido.
 */
static int abort_descendants(dag_t *g,
                             int node_index)
{
    int capacity;

    capacity = (g->n > 0)
                   ? g->n
                   : 1;


    int *queue;

    queue = malloc((size_t)capacity *
                   sizeof(int));


    if (queue == NULL) {
        return -1;
    }


    int head = 0;

    int tail = 0;


    node_t *root;

    root = &g->nodes[node_index];


    /*
     * empezamos por los hijos directos.
     */
    for (int i = 0;
         i < root->nchildren;
         i++) {

        int index;

        index = root->children[i];


        node_t *node;

        node = &g->nodes[index];


        if (node->state == ST_PENDING ||
            node->state == ST_READY) {

            /*
             * marcamos antes de agregar a la cola
             * para evitar duplicados.
             */
            node->state = ST_ABORTED;

            queue[tail++] = index;
        }
    }


    while (head < tail) {

        int current_index;

        current_index = queue[head++];


        node_t *current;

        current = &g->nodes[current_index];


        for (int i = 0;
             i < current->nchildren;
             i++) {

            int child_index;

            child_index =
                current->children[i];


            node_t *child;

            child =
                &g->nodes[child_index];


            if (child->state == ST_PENDING ||
                child->state == ST_READY) {

                child->state = ST_ABORTED;

                queue[tail++] =
                    child_index;
            }
        }
    }


    free(queue);

    return 0;
}


/*
 * Recoge todos los procesos hijos que ya terminaron.
 */
static void reap_children(dag_t *g,
                          active_child_t *active,
                          int running)
{
    int status;

    pid_t pid;


    while ((pid = waitpid(-1,
                          &status,
                          WNOHANG)) > 0) {

        for (int i = 0;
             i < running;
             i++) {

            int node_index;

            node_index =
                active[i].node_index;


            if (g->nodes[node_index].pid ==
                pid) {

                active[i].exited = 1;

                active[i].status = status;

                break;
            }
        }
    }
}


/* LEER EL  PIPE DE UN HIJO                                      */

static int read_child_pipe(active_child_t *active)
{
    ssize_t n;


    do {

        n = read(active->read_fd,
                 active->message,
                 sizeof(active->message) - 1);

    } while (n < 0 &&
             errno == EINTR);


    if (n > 0) {

        active->message[n] = '\0';

    } else {

        active->message[0] = '\0';
    }


    close(active->read_fd);

    active->read_fd = -1;

    active->pipe_done = 1;


    if (n < 0) {
        return -1;
    }


    return 0;
}


/* FINALIZAR LOS  HIJOS                                           */

static int finalize_children(dag_t *g,
                             active_child_t *active,
                             int *running,
                             ready_queue_t *ready)
{
    int i = 0;


    while (i < *running) {

        active_child_t *current;

        current = &active[i];


        /*
         * Para finalizar necesitamos conocer
         * tanto el estado del proceso como el pipe.
         */
        if (!current->exited ||
            !current->pipe_done) {

            i++;

            continue;
        }


        int node_index;

        node_index =
            current->node_index;


        node_t *node;

        node = &g->nodes[node_index];


        if (WIFEXITED(current->status) &&
            WEXITSTATUS(current->status) == 0) {

            node->state = ST_DONE;


            if (propagate_success(g,
                                  node_index,
                                  ready,
                                  current->message) < 0) {

                return -1;
            }
        }

        /*
         * SI FALLA
         */
        else {

            node->state = ST_FAILED;


            if (abort_descendants(g,
                                  node_index) < 0) {

                return -1;
            }
        }


        node->pid = 0;


        /*
         * Sacamos el hijo de active[].
         */
        for (int j = i;
             j < *running - 1;
             j++) {

            active[j] =
                active[j + 1];
        }


        (*running)--;
    }


    return 0;
}

/*EL CONTROL+C ASESINARA A LOS HIJOS*/

static void mark_unfinished_aborted(dag_t *g)
{
    for (int i = 0;
         i < g->n;
         i++) {

        if (g->nodes[i].state == ST_PENDING ||
            g->nodes[i].state == ST_READY ||
            g->nodes[i].state == ST_RUNNING) {

            g->nodes[i].state =
                ST_ABORTED;
        }
    }
}


/*
 * Mata los hijos activos y los recoge con waitpid().
 */
static void terminate_active(dag_t *g,
                             active_child_t *active,
                             int running)
{
    /*
     * Primero enviamos SIGTERM.
     */
    for (int i = 0;
         i < running;
         i++) {

        int node_index;

        node_index =
            active[i].node_index;


        pid_t pid;

        pid =
            g->nodes[node_index].pid;

	 if (pid > 0 && !active[i].exited) {
            if (kill(pid, SIGTERM) < 0 &&
                errno != ESRCH) {

                perror("kill");
            }
        }


        if (active[i].read_fd >= 0) {

            close(active[i].read_fd);

            active[i].read_fd = -1;
        }
    }


    /*
     * Luego esperamos a todos para no dejar zombies.
     */
    for (int i = 0;
         i < running;
         i++) {

        int node_index;

        node_index =
            active[i].node_index;


        pid_t pid;

        pid =
            g->nodes[node_index].pid;


        int status;


	if (pid <= 0 || active[i].exited) {
         continue;
	}

        while (waitpid(pid,
                       &status,
                       0) < 0) {

            if (errno == EINTR) {
                continue;
            }


            if (errno != ECHILD) {
                perror("waitpid");
            }


            break;
        }


        g->nodes[node_index].pid = 0;
    }
}


/* SCHEDULER PRINCIPAL                                       */

int scheduler_run(dag_t *g,
                  int K)
{
    if (g == NULL ||
        K <= 0) {

        return -1;
    }


    int capacity;

    capacity = (g->n > 0)
                   ? g->n
                   : 1;


    ready_queue_t ready;


    if (ready_init(&ready,
                   capacity) < 0) {

        fprintf(stderr,
                "scheduler: sin memoria para READY\n");

        return -1;
    }


    /*
     * Inicialmente entran a READY todos
     * los nodos sin dependencias.
     */
    for (int i = 0;
         i < g->n;
         i++) {

        if (g->nodes[i].pending == 0) {

            g->nodes[i].state =
                ST_READY;


            if (ready_push(&ready,
                           i) < 0) {

                ready_free(&ready);

                return -1;
            }
        }
    }


    int max_active;

    max_active =
        (K < g->n)
            ? K
            : g->n;


    if (max_active == 0) {

        ready_free(&ready);

        return 0;
    }


    active_child_t *active;

    active =
        calloc((size_t)max_active,
               sizeof(*active));


    /*
     * +1 porque pfds[0] sera el self-pipe
     * de las señales.
     */
    struct pollfd *pfds;

    pfds =
        calloc((size_t)max_active + 1U,
               sizeof(*pfds));


    if (active == NULL ||
        pfds == NULL) {

        fprintf(stderr,
                "scheduler: sin memoria\n");

        free(active);
        free(pfds);

        ready_free(&ready);

        return -1;
    }


    struct sigaction old_int;

    struct sigaction old_chld;


    if (setup_signals(&old_int,
                      &old_chld) < 0) {

        free(active);
        free(pfds);

        ready_free(&ready);

        return -1;
    }


    int running = 0;

    int result = 0;


    for (;;) {

        /*
         * Ctrl+C.
         */
        if (got_sigint) {

            mark_unfinished_aborted(g);

            terminate_active(g,
                             active,
                             running);

            result = 130;

            break;
        }


        /*
         * Recogemos hijos que hayan terminado.
         */
        reap_children(g,
                      active,
                      running);


        /*
         * se procesan  los que ya tienen:
         * - estado de salida
         * - pipe leido/cerrado
         */
        if (finalize_children(g,
                              active,
                              &running,
                              &ready) < 0) {

            fprintf(stderr,
                    "scheduler: error finalizando hijos\n");

            mark_unfinished_aborted(g);

            terminate_active(g,
                             active,
                             running);

            result = -1;

            break;
        }


        /*
         * se lanzan  nuevos procesos hasta llegar a K.
         */
        while (running < max_active) {

            int node_index;


            if (ready_pop(&ready,
                          &node_index) < 0) {

                break;
            }


            /*
             * Puede haber nodos viejos en READY
             * que fueron abortados por otra rama.
             */
            if (g->nodes[node_index].state !=
                ST_READY) {

                continue;
            }


            if (launch_node(g,
                            node_index,
                            &active[running]) < 0) {

                mark_unfinished_aborted(g);

                terminate_active(g,
                                 active,
                                 running);

                result = -1;

                goto cleanup;
            }


            running++;
        }


        /*
         * Si no hay hijos vivos y no  pudimos
         * lanzar otro, terminamos.
         */
        if (running == 0) {
            break;
        }


        /*
         * pfds[0] vigila las  señales.
         */
        pfds[0].fd =
            sigpipe[0];

        pfds[0].events =
            POLLIN;

        pfds[0].revents =
            0;


        /*
         * Los demas vigilan los pipes
         * de los hijos activos.
         */
        for (int i = 0;
             i < running;
             i++) {

            if (active[i].pipe_done) {

                pfds[i + 1].fd = -1;

            } else {

                pfds[i + 1].fd =
                    active[i].read_fd;
            }


            pfds[i + 1].events =
                POLLIN | POLLHUP;

            pfds[i + 1].revents =
                0;
        }


        /*
         * -1 = esperar indefinidamente.
         * Por lo cual.
         * No hay busy-waiting.
         */
        int poll_result;

        poll_result =
            poll(pfds,
                 (nfds_t)running + 1U,
                 -1);


        if (poll_result < 0) {

            if (errno == EINTR) {

                continue;
            }


            perror("poll");


            mark_unfinished_aborted(g);

            terminate_active(g,
                             active,
                             running);

            result = -1;

            break;
        }


        /*
         * Ocurrio SIGINT o SIGCHLD.
         */
        if (pfds[0].revents &
            POLLIN) {

            drain_sigpipe();
        }


        /*
         * comienzo de loop
         */
        if (got_sigint) {
            continue;
        }


        /*
         * Procesamos SIGCHLD.
         */
        reap_children(g,
                      active,
                      running);


        /*
         * Revisamos los pipes de los hijos.
         */
        for (int i = 0;
             i < running;
             i++) {

            short events;

            events =
                pfds[i + 1].revents;


            if (active[i].pipe_done) {
                continue;
            }


            if (events &
                (POLLIN |
                 POLLHUP |
                 POLLERR |
                 POLLNVAL)) {

                if (read_child_pipe(
                        &active[i]) < 0) {

                    if (errno != EAGAIN) {

                        perror(
                            "read pipe hijo");
                    }
                }
            }
        }


        /*
         * El hijo puede haber terminado justo
         * despues de leer el pipe.
         */
        reap_children(g,
                      active,
                      running);
    }


cleanup:

    restore_signals(&old_int,
                    &old_chld);


    free(pfds);

    free(active);

    ready_free(&ready);


    return result;
}
