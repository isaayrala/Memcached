#ifndef __HASH_LRU_H__
#define __HASH_LRU_H__
#include <pthread.h>
#include "hash_chaining.h"

//FUNCIONES LRU
ListaLru crear_lru();
void agregar_lru(ListaLru lru, HList* nodo);
void eliminar_lru(ListaLru lru, HList* nodo);
void modificar_lru(ListaLru lru, HList* nodo);

//PARA TESTEO
void imprimir_lru(ListaLru lru);

//FUNCIONES DESALOJO/CHEQUEO MEMORIA DISPONIBLE
void* safe_malloc(size_t size_type, int size, TablaHash tabla, ListaLru lru);

void desalojo(TablaHash tabla, ListaLru lru);

#endif