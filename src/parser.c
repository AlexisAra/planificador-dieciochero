#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "dag.h"

#define MAX_LINEA 4096
#define TIEMPO_MIN 100
#define TIEMPO_MAX 5000

/* saca los espacios del principio y del final (modifica el string) */
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

/* parte la linea en 4 campos. devuelve 0 si esta bien, -1 si no */
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

/* copia un string a memoria nueva (strdup a mano) */
static char *copiar(const char *s) {
    char *r = malloc(strlen(s) + 1);
    if (r != NULL) {
        strcpy(r, s);
    }
    return r;
}

/* convierte el campo tiempo. vacio -> aleatorio. devuelve -1 si es invalido */
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

int dag_load(const char *path, dag_t *g) {
    FILE *f = fopen(path, "r");
    if (f == NULL) {
        perror("No se pudo abrir el archivo");
        return -1;
    }

    g->nodes = NULL;
    g->n = 0;

    /* deps_txt[i] guarda el texto de dependencias del nodo i, para resolverlo despues */
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

        /* id repetido? (busqueda lineal por ahora, despues lo mejoramos con hash) */
        for (int i = 0; i < g->n; i++) {
            if (strcmp(g->nodes[i].id, id) == 0) {
                fprintf(stderr, "Linea %d: id repetido '%s'\n", num_linea, id);
                goto error;
            }
        }

        int t = leer_tiempo(tiempo);
        if (t < 0) {
            fprintf(stderr, "Linea %d: tiempo invalido '%s'\n", num_linea, tiempo);
            goto error;
        }

        /* si no hay espacio, duplico la capacidad */
        if (g->n == capacidad) {
            int nueva = (capacidad == 0) ? 16 : capacidad * 2;
            node_t *tmp = realloc(g->nodes, nueva * sizeof(node_t));
            char **tmp2 = realloc(deps_txt, nueva * sizeof(char *));
            if (tmp != NULL) g->nodes = tmp;
            if (tmp2 != NULL) deps_txt = tmp2;
            if (tmp == NULL || tmp2 == NULL) {
                fprintf(stderr, "Sin memoria\n");
                goto error;
            }
            capacidad = nueva;
        }

        node_t *nd = &g->nodes[g->n];
        memset(nd, 0, sizeof(node_t));
        nd->id = copiar(id);
        nd->name = copiar(nombre);
        nd->time_ms = t;
        nd->state = ST_PENDING;
        deps_txt[g->n] = copiar(deps);
        if (nd->id == NULL || nd->name == NULL || deps_txt[g->n] == NULL) {
            fprintf(stderr, "Sin memoria\n");
            goto error;
        }
        g->n++;
    }

    fclose(f);

    /* por ahora solo mostramos lo que guardamos */
    for (int i = 0; i < g->n; i++) {
        printf("nodo %d: id=%s nombre=%s tiempo=%dms deps_txt=[%s]\n",
               i, g->nodes[i].id, g->nodes[i].name,
               g->nodes[i].time_ms, deps_txt[i]);
        free(deps_txt[i]);
    }
    free(deps_txt);
    return 0;

error:
    fclose(f);
    for (int i = 0; i < g->n; i++) {
        free(deps_txt[i]);
    }
    free(deps_txt);
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
