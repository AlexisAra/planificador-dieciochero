#!/usr/bin/env python3
import random
import sys

def generar(n, max_deps, path):
    with open(path, "w") as f:
        for i in range(1, n + 1):
            nombre = f"actividad_{i}"
            tiempo = random.randint(100, 5000)

            # elijo dependencias solo entre ids menores a i
            posibles = list(range(1, i))
            cantidad = min(len(posibles), random.randint(0, max_deps))
            deps = sorted(random.sample(posibles, cantidad)) if cantidad > 0 else []
            deps_txt = ", ".join(str(d) for d in deps)

            f.write(f"{i} : {nombre} : {tiempo} : {deps_txt}\n")

if __name__ == "__main__":
    if len(sys.argv) < 3:
        print(f"Uso: {sys.argv[0]} cantidad_nodos archivo_salida [max_deps]")
        sys.exit(1)

    n = int(sys.argv[1])
    path = sys.argv[2]
    max_deps = int(sys.argv[3]) if len(sys.argv) > 3 else 3

    generar(n, max_deps, path)
    print(f"Generado {path} con {n} actividades (max {max_deps} deps c/u)")
