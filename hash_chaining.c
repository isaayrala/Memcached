#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>
#include "hash_chaining.h"
#include "lru.h"


//crea el comando (par {clave,valor})
Comando crear_comando(unsigned char* clave, unsigned char* valor, TablaHash tabla, ListaLru lru) {
  Comando com = safe_malloc(sizeof(struct _comando), 1, tabla, lru);
  com->clave = safe_malloc(sizeof(char), strlen((char*)clave)+1, tabla, lru);
  strcpy(com->clave, (char*)clave);
  com->valor = safe_malloc(sizeof(char), strlen((char*)valor)+1, tabla, lru);
  strcpy(com->valor, (char*)valor);
  return com;
}

//libera el comando
void destrCom(Comando dato) {
  if (dato != NULL) {
    free(dato->clave);
    free(dato->valor);
    free(dato);
  }
}

//comparar comando
int compCom(char* datoClave, char* clave) {
  return (strcmp(datoClave, clave));
}


//funcion hash de la tablahash
unsigned funcion_hash(void *string) {
  char* s = (char*) string;
  unsigned num;
  for (num = 0; *s != '\0'; ++s) {
    num = *s + 31 * num;
  }
  return num;
}

//agrega elementos a la lista enlazada de la tablahash
//si la clave ya se encontraba, solo agrega el valor nuevo
//si no se encontraba agrega el nodo
HList* agregar_lista(HList* lista, Comando dato, ListaLru lru, TablaHash tabla, Stats st) {

  HList* nuevoNodo = safe_malloc(sizeof(HList), 1, tabla, lru);
  nuevoNodo->dato = dato;
  
  if (lista == NULL) {
    nuevoNodo->sig = lista;
    
    //bloqueamos la lru y agg el nodo insertado a esta
    pthread_mutex_lock(&lockLru);
    agregar_lru(lru,nuevoNodo);
    pthread_mutex_unlock(&lockLru);
    
    //bloqueamos stats y incrementamos la cantidad de claves
    pthread_mutex_lock(&lockStats);
    st->keys += 1;
    pthread_mutex_unlock(&lockStats);
    
    return nuevoNodo;
  }
  
  HList* temp = lista;
  for (; temp!= NULL; temp = temp->sig) { //verificamos si la clave ya se encontraba en la tablahash
                                         //en caso de que si, solo se actualiza el valor del par
    if (!(tabla->comp((void*)temp->dato->clave, (void*)dato->clave))) {
      
      tabla->destroy(temp->dato);
      temp->dato = dato;
      
      //como el nodo ya se encontraba,
      //lo movemos al inicio ya que fue el "ultimo utilizado"
      pthread_mutex_lock(&lockLru);
      modificar_lru(lru, temp);
      pthread_mutex_unlock(&lockLru);
      
      free(nuevoNodo);
      return lista;
    }
  }

  nuevoNodo->sig = lista;

  //bloqueamos la lru y agg el nodo insertado a esta
  pthread_mutex_lock(&lockLru);
  agregar_lru(lru,nuevoNodo);
  pthread_mutex_unlock(&lockLru);
  
  //bloqueamos stats y incrementamos la cantidad de claves
  pthread_mutex_lock(&lockStats);
  st->keys += 1;
  pthread_mutex_unlock(&lockStats);

  return nuevoNodo;
}

//crea la tablahash
TablaHash crear_tabla (unsigned capacidad, FuncionHash hash, FuncionComparadora comp, FuncionDestructora destroy) {
  TablaHash tabla = malloc(sizeof(struct _tablahash));
  tabla->capacidad = capacidad;
  tabla->hash = hash;
  tabla->comp = comp;
  tabla->destroy = destroy;
  tabla->arreglo = malloc(sizeof (CasillaHash)* capacidad);
  for (unsigned int i = 0; i < tabla->capacidad; i++) {
    tabla->arreglo[i] = NULL;
  }
  return tabla;
}

//destruye la tabla
void destruir_tabla (TablaHash tabla) { 
  for (unsigned int i = 0; i < tabla->capacidad; i++) {
    CasillaHash temp = tabla->arreglo[i];
    while (temp != NULL) {
      CasillaHash siguiente = temp->sig;
      tabla->destroy(temp->dato);
      free(temp);       
      temp = siguiente;
    }
  }
  free(tabla->arreglo);
  free(tabla);
}


//busca el valor asociado a la clave pasada como argumento en la lista enlazada.
//en caso de encontrarlo, lo retorna. 
//caso contrario, retorna NULL
char *buscar_lista(HList* lista, char* clave, FuncionComparadora comp, ListaLru lru) {
  
  if (lista == NULL) {
    return NULL; //si no se encuentra la clave
  }
  
  HList* temp = lista;
  for (; temp != NULL; temp = temp->sig) {
    if (comp(temp->dato->clave, clave) == 0) {
    
      //el par buscado se encuentra en la tablahash por lo que
      //movemos dicho par al inicio de la lru ya que fue el "ultimo utilizado"
      pthread_mutex_lock(&lockLru);
      modificar_lru(lru, temp);
      pthread_mutex_unlock(&lockLru);
      
      return temp->dato->valor;
    }
  }
  return NULL; //si no se encuentra la clave
}


//busca la clave en la tablahash (GET "clave") para retornar el valor asociado
char *buscar_tabla(TablaHash tabla, char* clave, ListaLru lru) {
  
  if (tabla == NULL) return NULL;
  
  int idx = tabla->hash(clave) % tabla->capacidad; //indice del array de la hash dnde se escontraria la clave
  
  pthread_mutex_lock(&(locks[idx % LOCKS])); //bloqueamos dicha seccion de la tablahash para poder buscar la clave
  
  if (tabla->arreglo[idx] == NULL) {
    pthread_mutex_unlock(&(locks[idx % LOCKS])); //como la casilla es NULL, desbloqueamos
    return NULL;
  }
  else {
    char* encontrado = buscar_lista(tabla->arreglo[idx], clave, (FuncionComparadora)tabla->comp, lru); //buscamos en la lista
    pthread_mutex_unlock(&(locks[idx % LOCKS])); //como ya realizamos la busqueda, desbloqueamos
    return encontrado; //retornamos el valor o NULL
  }
}  

//inserta el comando en la tablahash (PUT "clave" "valor")
void insertar_tabla(TablaHash tabla, Comando dato, ListaLru lru, Stats st) {
  
  if (tabla == NULL) return;
  
  int idx = tabla->hash(dato->clave) % tabla->capacidad; //indice del array de la hash dnde se escontraria la clave
  
  pthread_mutex_lock(&(locks[idx % LOCKS])); //bloqueamos dicha seccion de la tablahash para poder buscar la clave

  tabla->arreglo[idx] = agregar_lista(tabla->arreglo[idx], dato, lru, tabla, st); //agregamos el par en la lista enlazada

  pthread_mutex_unlock(&(locks[idx % LOCKS])); //una vez que se agrego el par/valor se desbloquea el mutex
}

//elimina el nodo de la lista enlazada de la tablahash
HList* eliminar_nodo_lista(HList* lista, char* clave, int* flag, FuncionComparadora comp, FuncionDestructora destr, ListaLru lru) {
  
  if (lista == NULL) return NULL;

  HList* temp = lista;
  HList* anterior = NULL; //llevaremos un puntero al anterior para reacomodar punteros al eliminar

  while (temp != NULL) {
    
    if (comp(clave, temp->dato->clave) == 0) {
      
      *flag = 1; //si se encontro el elemento, seteamos la bandera en 1 para indicarlo

      if (anterior == NULL) { //el elemento a eliminar es el primero
        lista = temp->sig;
      } else {
        anterior->sig = temp->sig;  // Saltar el nodo eliminado
      }

      //bloqueamos la lru para reacomodar 
      //punteros y removerlo de esta.
      pthread_mutex_lock(&lockLru);
      eliminar_lru(lru,temp);
      pthread_mutex_unlock(&lockLru);

      destr(temp->dato); //liberamos el comando
      free(temp); //liberamos el nodo
      return lista;
    }

    anterior = temp;
    temp = temp->sig;
  }
  
  return lista;
}

//elimina el nodo de la tablahash y la lru
//explicacion detallada en el informe.
int eliminar_nodo_tabla(TablaHash tabla, char* clave, ListaLru lru, int funcion) {
  
  if (tabla == NULL) return 0;
  
  int idx = tabla->hash(clave) % tabla->capacidad;
  
  if (funcion == 1) { //se llama para el pedido DEL
    pthread_mutex_lock(&(locks[idx % LOCKS]));
    
  } else { //se llama desde desalojo()
    int c = pthread_mutex_trylock(&(locks[idx % LOCKS]));
    if (c != 0) return -1; //la seccion se encontraba bloqueada
  }
  int flag = 0; //bandera para retornar si el elemento se encontraba o no
  tabla->arreglo[idx] = eliminar_nodo_lista(tabla->arreglo[idx], clave, &flag,(FuncionComparadora) tabla->comp, (FuncionDestructora) tabla->destroy,lru);
  pthread_mutex_unlock(&(locks[idx % LOCKS])); //una vez eliminado el par, soltamos el lock

  return flag; //retornamos la bandera para indicar si pudimos eliminar
}

//crea la estructura que lleva la cantidad de 
//pedidos y claves ingresadas por el cliente
Stats crear_stats() {
  Stats st = malloc(sizeof(struct _stats));
  st->put = 0;
  st->get = 0;
  st->del = 0;
  st->keys = 0;
  return st;
}