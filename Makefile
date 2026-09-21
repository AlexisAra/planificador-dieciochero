CC     = gcc
CFLAGS = -Wall -Wextra -std=c17 -D_POSIX_C_SOURCE=200809L -g
SRC    = $(wildcard src/*.c)
OBJ    = $(SRC:.c=.o)

planificador: $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ) -lpthread

%.o: %.c src/dag.h
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) planificador
