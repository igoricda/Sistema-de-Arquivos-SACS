CC = gcc
CFLAGS = -Wall -Wextra -g

# Nome do executável
TARGET = sacs_fs

# Arquivos objetos
OBJS = sacs.o main.o

# Regra padrão
all: $(TARGET)

# Linkagem
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS)

# Compilar main.c
main.o: main.c sacs.h
	$(CC) $(CFLAGS) -c main.c

# Compilar sacs.c
sacs.o: sacs.c sacs.h
	$(CC) $(CFLAGS) -c sacs.c

# Limpeza
clean:
	rm -f $(OBJS) $(TARGET)