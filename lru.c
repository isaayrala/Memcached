#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include "lru.h"
#include "hash_chaining.h"


//funcion de testeo que imprime la lru
void imprimir_lru(ListaLru lru) {
  printf("-----LRU-----\n");
  for(HList* nodo = lru->head; nodo != NULL; nodo = nodo->sig_lru){
    printf("Clave: %s, Valor: %s\n", nodo->dato->clave, nodo->dato->valor);
  }
  printf("-----FIN LRU-----\n");
}


//funcion que chequea si el programa sigue teniendo memoria suficiente.
//explicacion detallada en el informe.
void* safe_malloc(size_t size_type, int size, TablaHash tabla, ListaLru lru) {
  
  void* dato = malloc(size_type * size);
  
  while (dato == NULL) { //mientras que no haya memoria disponible,
    
    if (lru->head == NULL) {
      fprintf(stderr, "Error: Memoria insuficiente y LRU vacía.\n");
      exit(EXIT_FAILURE);
    }

    desalojo(tabla, lru); //liberamos memoria
    dato = malloc(size_type * size);
  
  }
  
  return dato;
}


//se encarga de liberar memoria del programa para poder reservar nueva.
//explicacion detallada en el informe.
void desalojo(TablaHash tabla, ListaLru lru) {
  
  pthread_mutex_lock(&lockLru);

  if (lru == NULL || lru->tail == NULL) {
    printf("Error: Intento de remover un nodo de una LRU vacía.\n");
    pthread_mutex_unlock(&lockLru);
    return;
  }

  HList* nodo = lru->tail;
  int flag;

  while (nodo) {

    flag = eliminar_nodo_tabla(tabla, nodo->dato->clave, lru, 0); //intentamos eliminar el nodo
    if (flag == -1) { //no pudimos tomar el lock
      nodo = nodo->prev_lru; //intentamos con el anterior (vamos del menos usado al mas)
    } else { //se logro eliminar
      pthread_mutex_unlock(&lockLru); //soltamos lock
      return;
    }
  }
  
  //nodo es NULL
  printf("Error: No se pudo remover ningún nodo de la LRU.\n");
  pthread_mutex_unlock(&lockLru); //soltamos el lock
}


//agregamos un elemento a la lru
//recibe el nodo que fue agregado a la tablahash y
//reacomoda puntero para formar la lru
void agregar_lru(ListaLru lru, HList* nodo) {
  
  if (lru->head == NULL) {
    nodo->sig_lru = NULL;
    nodo->prev_lru = NULL;
    lru->head = nodo;
    lru->tail = nodo;
    return;
  }

  nodo->sig_lru = lru->head;
  lru->head->prev_lru = nodo;
  lru->head = nodo;
  
  return;
}


//elimina un lemento de la lru
//no libera memoeria, solo reacomoda puntero
//para que no quede un nodo NULL en ella.
void eliminar_lru(ListaLru lru, HList* nodo) {
  
  //caso lru vacia
  if (lru == NULL) return;
  //caso un solo elem
  if (lru->head == lru->tail) {
    lru->head = NULL;
    lru->tail = NULL;
    return;
  }
  //caso primer elem
  if (lru->head == nodo) {
    lru->head = lru->head->sig_lru;
    lru->head->prev_lru = NULL;
    return;
  }
  //caso ult elem
  if (lru->tail == nodo) {
    lru->tail = lru->tail->prev_lru;
    lru->tail->sig_lru = NULL;
  } else {
    //caso elem del diome
    HList* temp2 = nodo->sig_lru;
    nodo->prev_lru->sig_lru = temp2;
    nodo->sig_lru->prev_lru = nodo->prev_lru;
  }
  
  return;
}


//mueve un elemento que ya se encuentra en la lru al inicio de esta, 
//ya que pasa a ser el "mas recientemente usado".
//se usa cuando un cliente hizo un pedido con una clave ya existente
//o cuando se busca un valor asociado a una clave.
void modificar_lru(ListaLru lru, HList* nodo) {

  //caso lista vacia
  if (lru == NULL || lru->head == NULL) {
    printf("Error: Intento de modificar una LRU vacía.\n");
    return;
  }
  //caso un solo elem
  if (lru->head == lru->tail) return;
  //caso primer elem
  if (lru->head == nodo) return;

  //caso ult elem
  if (lru->tail == nodo) {
    nodo->prev_lru->sig_lru = NULL;
    lru->tail = nodo->prev_lru;
  } else {
    //caso elem del diome
    HList* nodo2 = nodo->sig_lru;
    nodo->prev_lru->sig_lru = nodo2;
    nodo->sig_lru->prev_lru = nodo->prev_lru;
  }
  
  HList* inicio = lru->head;
  nodo->sig_lru = inicio;
  lru->head->prev_lru = nodo;
  lru->head = nodo;
  lru->head->prev_lru = NULL;
  
  return;
}


//funcion para crear la lru
ListaLru crear_lru() {
  ListaLru lru = malloc(sizeof(struct ListaLru));
  lru->head = NULL;
  lru->tail = NULL;
  return lru;
}
