#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "dag.h"

#define MAX_LINEA 4096
#define TIEMPO_MIN 100
#define TIEMPO_MAX 5000

/* ---------- tabla hash simple: id (string) -> indice del nodo ---------- */

typedef struct hash_entry {
    char *key;
    int   idx;
    struct hash_entry *next;
} hash_entry_t;

typedef struct {
    hash_entry_t **buckets;
    int n_buckets;
} hash_t;

static unsigned long hash_str(const char *s) {
    unsigned long h = 5381;
    while (*s) {
        h = ((h << 5) + h) + (unsigned char)(*s);
        s++;
    }
    return h;
}

static void hash_init(hash_t *h, int n_buckets) {
    h->n_buckets = n_buckets;
    h->buckets = calloc(n_buckets, sizeof(hash_entry_t *));
}

/* devuelve el indice si ya existe, o -1 si no esta */
static int hash_get(hash_t *h, const char *key) {
    unsigned long b = hash_str(key) % h->n_buckets;
    for (hash_entry_t *e = h->buckets[b]; e != NULL; e = e->next) {
        if (strcmp(e->key, key) == 0) {
            return e->idx;
        }
    }
    return -1;
}

/* devuelve 0 si lo pudo insertar, -1 si ya existia */
static int hash_put(hash_t *h, const char *key, int idx) {
    if (hash_get(h, key) != -1) {
        return -1;
    }
    unsigned long b = hash_str(key) % h->n_buckets;
    hash_entry_t *e = malloc(sizeof(hash_entry_t));
    e->key = malloc(strlen(key) + 1);
    strcpy(e->key, key);
    e->idx = idx;
    e->next = h->buckets[b];
    h->buckets[b] = e;
    return 0;
}

static void hash_free(hash_t *h) {
    for (int i = 0; i < h->n_buckets; i++) {
        hash_entry_t *e = h->buckets[i];
        while (e != NULL) {
            hash_entry_t *sig = e->next;
            free(e->key);
            free(e);
            e = sig;
        }
    }
    free(h->buckets);
}

/* ---------- funciones de texto que ya teniamos ---------- */

static char *trim(char *s) {
    while (isspace((unsigned char)*s)) {
        s++;
    }
    if (*s == '\0') {
        return s;
    }
    char *fin = s + strlen(s) - 1;
    while (fin > s && isspace((unsigned char)*fin)) {
        *fin = '\0';
        fin--;
    }
    return s;
}

static int separar_campos(char *linea, char **id, char **nombre,
                          char **tiempo, char **deps) {
    char *p1 = strchr(linea, ':');
    if (p1 == NULL) return -1;
    char *p2 = strchr(p1 + 1, ':');
    if (p2 == NULL) return -1;
    char *p3 = strchr(p2 + 1, ':');
    if (p3 == NULL) return -1;

    *p1 = '\0';
    *p2 = '\0';
    *p3 = '\0';

    *id = trim(linea);
    *nombre = trim(p1 + 1);
    *tiempo = trim(p2 + 1);
    *deps = trim(p3 + 1);
    return 0;
}

static char *copiar(const char *s) {
    char *r = malloc(strlen(s) + 1);
    if (r != NULL) {
        strcpy(r, s);
    }
    return r;
}

static int leer_tiempo(const char *txt) {
    if (txt[0] == '\0') {
        return TIEMPO_MIN + rand() % (TIEMPO_MAX - TIEMPO_MIN + 1);
    }
    char *resto;
    long v = strtol(txt, &resto, 10);
    if (*resto != '\0' || v <= 0) {
        return -1;
    }
    return (int)v;
}

/* agrega 'valor' al arreglo dinamico *arr, que tiene *n elementos usados */
static void agregar_a_arreglo(int **arr, int *n, int valor) {
    *arr = realloc(*arr, (*n + 1) * sizeof(int));
    (*arr)[*n] = valor;
    (*n)++;
}

/* ---------- paso 1: leer lineas y guardar nodos (igual que antes) ---------- */

static int leer_nodos(FILE *f, dag_t *g, char ***deps_txt_out) {
    char **deps_txt = NULL;
    int capacidad = 0;
    char linea[MAX_LINEA];
    int num_linea = 0;

    while (fgets(linea, MAX_LINEA, f) != NULL) {
        num_linea++;
        linea[strcspn(linea, "\n")] = '\0';

        char *copia = trim(linea);
        if (copia[0] == '\0' || copia[0] == '#') {
            continue;
        }

        char *id, *nombre, *tiempo, *deps;
        if (separar_campos(linea, &id, &nombre, &tiempo, &deps) < 0) {
            fprintf(stderr, "Linea %d mal formada\n", num_linea);
            goto error;
        }
        if (id[0] == '\0') {
            fprintf(stderr, "Linea %d: id vacio\n", num_linea);
            goto error;
        }

        int t = leer_tiempo(tiempo);
        if (t < 0) {
            fprintf(stderr, "Linea %d: tiempo invalido '%s'\n", num_linea, tiempo);
            goto error;
        }

        if (g->n == capacidad) {
            int nueva = (capacidad == 0) ? 16 : capacidad * 2;
            node_t *tmp = realloc(g->nodes, nueva * sizeof(node_t));
            char **tmp2 = realloc(deps_txt, nueva * sizeof(char *));
            if (tmp == NULL || tmp2 == NULL) {
                fprintf(stderr, "Sin memoria\n");
                goto error;
            }
            g->nodes = tmp;
            deps_txt = tmp2;
            capacidad = nueva;
        }

        node_t *nd = &g->nodes[g->n];
        memset(nd, 0, sizeof(node_t));
        nd->id = copiar(id);
        nd->name = copiar(nombre);
        nd->time_ms = t;
        nd->state = ST_PENDING;
        deps_txt[g->n] = copiar(deps);
        g->n++;
    }

    *deps_txt_out = deps_txt;
    return 0;

error:
    *deps_txt_out = deps_txt;
    return -1;
}

/* ---------- paso 2: construir la tabla hash y detectar ids repetidos ---------- */

static int construir_hash(dag_t *g, hash_t *h) {
    int n_buckets = g->n < 16 ? 16 : g->n * 2;
    hash_init(h, n_buckets);
    for (int i = 0; i < g->n; i++) {
        if (hash_put(h, g->nodes[i].id, i) < 0) {
            fprintf(stderr, "Id repetido: '%s'\n", g->nodes[i].id);
            return -1;
        }
    }
    return 0;
}

/* ---------- paso 3: resolver dependencias (parents/children) ---------- */

static int resolver_deps(dag_t *g, hash_t *h, char **deps_txt) {
    for (int i = 0; i < g->n; i++) {
        char *texto = deps_txt[i];
        if (texto[0] == '\0') {
            continue;
        }
        char *copia = copiar(texto);
        char *tok = strtok(copia, ",");
        while (tok != NULL) {
            char *dep_id = trim(tok);
            int j = hash_get(h, dep_id);
            if (j < 0) {
                fprintf(stderr, "Nodo '%s' depende de '%s', que no existe\n",
                        g->nodes[i].id, dep_id);
                free(copia);
                return -1;
            }
            /* i depende de j: j es padre de i, i es hijo de j */
            agregar_a_arreglo(&g->nodes[i].parents, &g->nodes[i].nparents, j);
            agregar_a_arreglo(&g->nodes[j].children, &g->nodes[j].nchildren, i);
            g->nodes[i].ndeps++;
            g->nodes[i].pending++;
            tok = strtok(NULL, ",");
        }
        free(copia);
    }
    return 0;
}

/* ---------- paso 4: detectar ciclos con Kahn ---------- */

static int detectar_ciclo(dag_t *g) {
    int *pending_copia = malloc(g->n * sizeof(int));
    int *cola = malloc(g->n * sizeof(int));
    int inicio = 0, fin_cola = 0;

    for (int i = 0; i < g->n; i++) {
        pending_copia[i] = g->nodes[i].pending;
        if (pending_copia[i] == 0) {
            cola[fin_cola++] = i;
        }
    }

    int procesados = 0;
    while (inicio < fin_cola) {
        int actual = cola[inicio++];
        procesados++;
        for (int c = 0; c < g->nodes[actual].nchildren; c++) {
            int hijo = g->nodes[actual].children[c];
            pending_copia[hijo]--;
            if (pending_copia[hijo] == 0) {
                cola[fin_cola++] = hijo;
            }
        }
    }

    free(pending_copia);
    free(cola);

    if (procesados != g->n) {
        fprintf(stderr, "Se detecto un ciclo en el plan (%d de %d nodos alcanzables)\n",
                procesados, g->n);
        return -1;
    }
    return 0;
}

/* ---------- funcion principal ---------- */

int dag_load(const char *path, dag_t *g) {
    FILE *f = fopen(path, "r");
    if (f == NULL) {
        perror("No se pudo abrir el archivo");
        return -1;
    }

    g->nodes = NULL;
    g->n = 0;
    char **deps_txt = NULL;
    hash_t h = {0};
    int hash_creado = 0;

    if (leer_nodos(f, g, &deps_txt) < 0) {
        goto error;
    }
    fclose(f);
    f = NULL;

    if (construir_hash(g, &h) < 0) {
        goto error;
    }
    hash_creado = 1;

    if (resolver_deps(g, &h, deps_txt) < 0) {
        goto error;
    }

    if (detectar_ciclo(g) < 0) {
        goto error;
    }

    for (int i = 0; i < g->n; i++) {
        free(deps_txt[i]);
    }
    free(deps_txt);
    hash_free(&h);
    return 0;

error:
    if (f != NULL) fclose(f);
    if (deps_txt != NULL) {
        for (int i = 0; i < g->n; i++) {
            free(deps_txt[i]);
        }
        free(deps_txt);
    }
    if (hash_creado) hash_free(&h);
    dag_free(g);
    return -1;
}

void dag_free(dag_t *g) {
    for (int i = 0; i < g->n; i++) {
        free(g->nodes[i].id);
        free(g->nodes[i].name);
        free(g->nodes[i].children);
        free(g->nodes[i].parents);
    }
    free(g->nodes);
    g->nodes = NULL;
    g->n = 0;
}
