# Planificador Dieciochero

Tarea 1 de Sistemas Operativos. Simulador de actividades que se ejecutan como procesos, respetando dependencias entre ellas (como un DAG) y un límite de procesos simultáneos.

## Integrantes

- Alexis Godoy
- Francisco Grandon

## Cómo compilar

    make

Compila con gcc -Wall -Wextra -std=c17. No debería salir ningún warning.

Para limpiar los archivos compilados:

    make clean

## Cómo ejecutar

    ./planificador plan.txt K

- plan.txt: archivo con las actividades (ver formato más abajo).
- K: cantidad máxima de procesos que pueden estar corriendo al mismo tiempo.

Ejemplo:

    ./planificador tests/plan1.txt 2

Al final imprime un resumen con el estado de cada actividad: completada, fallida o abortada.

## Formato de plan.txt

Cada línea es una actividad:

    ID : nombre : tiempo_ms : dependencias

Ejemplo:

    1 : prender_carbon : 500 :
    2 : comprar_carne : 1200 :
    3 : asar_longaniza : 800 : 1, 2

Si el tiempo se deja vacío, se asigna uno al azar entre 100 y 5000 ms.

## Estructura del código

- src/dag.h: define las estructuras (node_t, dag_t) y las funciones que se comparten entre archivos.
- src/parser.c: lee el plan.txt y arma el grafo de dependencias.
- src/child.c: código que corre cada actividad como proceso hijo.
- src/scheduler.c: el planificador. Decide qué actividades lanzar, respeta el límite K, maneja las señales y los pipes.
- src/main.c: junta todo, lee los argumentos de la línea de comandos y llama al resto.
- tests/: archivos de prueba, incluyendo gen_plan.py para generar planes grandes (hasta 10000 actividades).

## Cómo funciona cada parte

### Parser (parser.c)

Lee el archivo línea por línea y separa cada actividad en sus 4 campos. Guarda cada actividad en una tabla hash (id → índice), para poder buscar rápido a qué actividad corresponde cada dependencia, incluso con miles de actividades. Sin la tabla hash, buscar cada dependencia sería mucho más lento con archivos grandes.

Para detectar si el plan tiene un ciclo (una actividad que depende, directa o indirectamente, de sí misma), usamos el algoritmo de Kahn: vamos consumiendo actividades que ya no tienen dependencias pendientes, y si al final quedan actividades sin consumir, es porque hay un ciclo.

### Hijo (child.c)

Cada actividad se ejecuta en un proceso separado, creado con fork(). El hijo:
1. Duerme la cantidad de milisegundos que le corresponde (simulando que está trabajando).
2. Sortea si la actividad falla (5% de probabilidad), para poder probar el manejo de errores.
3. Si no falla, escribe un mensaje corto en el pipe hacia el padre, avisando que terminó.

### Planificador (scheduler.c)

Es la parte más importante. Mantiene una cola de actividades listas para ejecutarse (las que ya no tienen dependencias pendientes) y lanza hasta K procesos a la vez.

En vez de estar preguntando todo el rato si algún hijo terminó (lo que gastaría CPU sin necesidad, algo prohibido por el enunciado), usamos poll(), que deja al programa dormido hasta que efectivamente pasa algo: un hijo escribe en su pipe, o llega una señal.

Para las señales (Ctrl+C y cuando un hijo termina) usamos una técnica llamada self-pipe: en vez de hacer cosas complicadas dentro del manejador de la señal (lo cual es peligroso, porque el manejador puede interrumpir al programa en cualquier punto), el manejador solo escribe un byte en un pipe especial. El programa principal, que está esperando con poll(), se despierta apenas ve ese byte y ahí sí reacciona con calma.

Cuando una actividad termina bien, el planificador le pasa su mensaje a las actividades que dependían de ella y revisa si ya pueden ejecutarse. Cuando una actividad falla, se marca como fallida y se recorren todos sus descendientes (con un BFS, o sea, capa por capa) para marcarlos como abortados, sin tocar el resto del programa.

Con Ctrl+C, el planificador manda una señal a todos los procesos activos para que terminen, espera a que todos terminen de verdad (para no dejar zombies), y marca como abortada cualquier actividad que no alcanzó a correr.

## Decisiones de diseño

- Tabla hash para los IDs: elegimos esto porque el enunciado pide soportar hasta 10000 actividades, y buscar cada dependencia comparando una por una habría sido muy lento.
- poll() en vez de busy-waiting: poll() bloquea el programa sin gastar CPU hasta que realmente hay algo que hacer.
- Self-pipe para las señales: usamos esto en vez de hacer todo directamente dentro del manejador de señales, porque dentro de un manejador no se pueden usar funciones como printf o malloc de forma segura.
- BFS para abortar ramas: usamos una cola en vez de una función recursiva, para evitar problemas si el grafo es muy grande (con recursión se podría llegar a desbordar la pila).

## Pruebas realizadas

- Ejemplo del enunciado (tests/plan1.txt), con distintos valores de K.
- Fallo de una actividad y verificación de que solo se aborta su rama, no el programa completo.
- Carga de estrés con 10000 actividades (tests/plan_grande.txt, generado con tests/gen_plan.py).

## Limitaciones conocidas

- El Makefile enlaza con -lpthread porque lo pide la rúbrica, pero en ningún archivo se usan hilos.
