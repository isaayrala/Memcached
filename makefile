CC = gcc
CFLAGS = -Wall -Wextra -pthread -lm

# Lista de archivos fuente
SRCS = server.c hash_chaining.c lru.c
OBJS = $(SRCS:.c=.o)
TARGET = server

# Opción por defecto (compila sin setcap)
all: normal

# Regla para compilar sin privilegios
normal: $(TARGET)
	@echo "Compilación sin privilegios completada."

# Regla para compilar con privilegios (setcap)
privileged: $(TARGET)
	@echo "Compilación con privilegios completada."
	@echo "Asignando CAP_NET_BIND_SERVICE y CAP_SETPCAP a $(TARGET)..."
	sudo setcap 'cap_net_bind_service=+ep' ./$(TARGET)
	@echo "Permisos aplicados correctamente."

# Compilar los archivos fuente
$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $(TARGET) $(CFLAGS)

# Compilar archivos individuales
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Ejecutar el servidor
run: normal
	@echo "Ejecutando $(TARGET)..."
	./$(TARGET)

# Ejecutar el servidor con privilegios
run_privileged: privileged
	@echo "Ejecutando $(TARGET) con privilegios..."
	./$(TARGET)

# Limpiar archivos generados
clean:
	rm -f $(OBJS) $(TARGET)
