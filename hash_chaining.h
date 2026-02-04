#ifndef __HASH_CH_H__
#define __HASH_CH_H__
#include <pthread.h>

//capacidades
#define TH 100000
#define LOCKS TH / 100

//mutex
extern pthread_mutexattr_t recHash;
extern pthread_mutex_t locks[LOCKS];
extern pthread_mutexattr_t recLru;
extern pthread_mutex_t lockLru;
extern pthread_mutex_t lockStats;

//funciones auxiliares TH
typedef unsigned (*FuncionHash) (void* data);
typedef int (*FuncionComparadora) (void* data1, void* data2);
typedef void (*FuncionDestructora) (void* data);

//estructura de stats
typedef struct _stats {
  long long int put;
  long long int get;
  long long int del;
  long long int keys;
} *Stats;

//estructura que lleva los pares {clave,valor}
typedef struct _comando {
  char* clave;
  char* valor;
} *Comando;

//nodo de la tablahash/lru
//el dato es compartido, mientras que 
//hay un puntero al siguiente para la TH y
//un puntero al siguiente y al anterior de la lru
typedef struct _HList {
  Comando dato;
  struct _HList* prev_lru;
  struct _HList* sig_lru;
  struct _HList* sig;
} HList;

//estructura que lleva un puntero 
//al primer y ultimo elemento de la lru
typedef struct ListaLru {
  HList* head;
  HList* tail;
} *ListaLru;

//lista enlazada de la tablahash
typedef HList* CasillaHash;

//estructura de la tablahash
struct _tablahash {
  CasillaHash* arreglo;
  unsigned int capacidad;
  FuncionComparadora comp;
  FuncionDestructora destroy;
  FuncionHash hash;
};
typedef struct _tablahash* TablaHash;

//FUNCION HASH
unsigned funcion_hash(void *string);

//FUNCION STATS
Stats crear_stats();

//FUNCIONES COMANDO
Comando crear_comando(unsigned char* clave, unsigned char* valor, TablaHash tabla, ListaLru lru);

void destrCom(Comando dato);

int compCom(char* datoClave, char* clave);

//FUNCIONES LISTA ENLAZADA TH
HList* eliminar_nodo_lista(HList* lista, char* clave, int* flag, FuncionComparadora comp, FuncionDestructora destr, ListaLru Lru);

char* buscar_lista(HList* lista, char* clave, FuncionComparadora comp,ListaLru lru);

HList* agregar_lista(HList* lista, Comando dato, ListaLru lru, TablaHash tabla, Stats st);

//FUNCIONES TABLAHASH
TablaHash crear_tabla (unsigned capacidad, FuncionHash hash, FuncionComparadora comp, FuncionDestructora destroy);

char *buscar_tabla(TablaHash tabla, char* clave, ListaLru lru);

void destruir_tabla (TablaHash tabla);

void insertar_tabla(TablaHash tabla, Comando dato, ListaLru lru, Stats st);

int eliminar_nodo_tabla(TablaHash tabla, char* clave, ListaLru lru, int funcion);


#endif