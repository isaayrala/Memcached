#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/resource.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <sys/epoll.h>
#include "hash_chaining.h"
#include "lru.h"
#include <pthread.h>
#include <fcntl.h>
#include <math.h>
//#include <sys/capability.h>

//variables globales
#define MAX_EVENTS 100
#define NTHREADS 4
#define PORT 8888

//codigos binarios
enum code {
	PUT = 11,
	DEL = 12,
	GET = 13,

	STATS = 21,

	OK = 101,
  OKE = 116,
	EINVALID = 111,
	ENOTFOUND = 112,
	EBINARY = 113,
	EBIG = 114,
	EUNK = 115,
};

//mutex
pthread_mutexattr_t recHash;
pthread_mutex_t locks[LOCKS];
pthread_mutexattr_t recLru;
pthread_mutex_t lockLru;
pthread_mutex_t lockStats;

//estructura para pasar argumentos a wait_for_clients
typedef struct _wfc {
  int lsock;
	TablaHash *th;
	ListaLru *lru;
} *Wfc;

//fd de la instancia epoll
int epfd;

//estructura global de stats
Stats st;

//funcion utilizada en mk_lsock, para abortar en caso de error
void quit(char *s) {
	perror(s);
	abort();
}

//funcion de lectura utilizada para leer los bytes del tamaño de clave/valor.
//lee los bytes del socket, y los transforma en un entero en formato little-endian.
int readn(int fd) {
  
  uint32_t temp;
  int res = 0;
  
  int rc = read(fd, &temp, sizeof(temp));
  if (rc <= 0) {
    perror("readn failed");
    return -1;  
    //retorna -1 si no lee correctamente
  }
  
  //convierte los 4 bytes a un entero en formato little-endian
  //ya que lo recibe del socket(big-endian)
  //ntohl -> thread-safe
  res = ntohl(temp);
  
  //Chatgpt
  // Comprobamos si el tamaño de la clave es válido (máximo 2^32 - 1)
  // Usamos uint32_t para evitar desbordamientos y asegurarnos del tamaño máximo
  uint32_t max_size = (1ULL << 32) - 1;  // 2^32 - 1

  // Comprobamos si el tamaño de la clave es válido (máximo 2^32 - 1)
  if ((uint32_t)res > max_size) {
    printf("Error: El tamaño de la clave excede el límite permitido de 2^32 - 1 bytes\n");
    char comm = EBIG;
    write(fd, &comm, 1);
    return -1;
  }
  
  return res;
}

//funcion de lectura utilizada para leer la clave o el valor, segun corresponda.
void readm(int fd, void* buf, int len) {
  
  int leido = 0; //llevamos un contador de los bytes que se leyeron
  
  while (leido < len) { //verifica que la cantidad de bytes leidos sea igual al tamaño de la clave/valor
    int rc = read(fd, (char*)buf + leido, len - leido); //vamos leyendo de a poco
    if (rc <= 0) {
      perror("readm failed");
      return;
    }
    leido += rc; //aumentamos el contador de bytes leido
  }
}


//parser de pedidos
//una vez obtenido el pedido del cliente, realiza la accion correspondiente en la tablahash
void parserBin(int csock, int epfd, TablaHash *th, ListaLru* lru) {
  
  char comando = 0;
  
  // Leemos el comando del cliente
  int rc = read(csock, &comando, 1);

  if (rc == 0) {
    printf("El cliente cerró la conexión\n");
    close(csock);
    return;
  } else if (rc < 0) {
    perror("Error leyendo el comando");
    close(csock);
    return;
  } else if (rc != 1) {
    fprintf(stderr, "Comando incompleto leído: %d bytes\n", rc);
    close(csock);
    return;
  }

  if (comando == PUT) {  
    
    int lenClave = readn(csock); //leemos la longitud de la clave
    if (lenClave <= 0) {
      perror("Error leyendo la longitud de la clave");
      return;
    }
    unsigned char* clave = safe_malloc(sizeof(unsigned char), lenClave + 1, (*th), (*lru)); //reservamos memoria
    readm(csock, clave, lenClave); //leemos la clave
    clave[lenClave] = '\0'; 
    
    int lenValor = readn(csock);  //leemos la longitud del valor
    if (lenValor <= 0) {
      perror("Error leyendo la longitud del valor");
      free(clave);
      return;
    }
    unsigned char* valor = safe_malloc(sizeof(unsigned char), lenValor + 1, (*th), (*lru)); //reservamos memoria
    readm(csock, valor, lenValor); //leemos el valor
    valor[lenValor] = '\0';  
    
    Comando com = crear_comando(clave, valor, (*th), (*lru)); //creamos el comando que insertaremos en la th
    free(clave); //liberamos memoria ya que en crear_comando se reserva nueva
    free(valor); //liberamos memoria ya que en crear_comando se reserva nueva
    
    //insertamos en la tablahash
    insertar_tabla((*th), com, (*lru), st);
    
    //tomamos el lock para modificar uno de los contadores de Stats
    //en este caso, incrementamos la cantidad de puts realizados
    pthread_mutex_lock(&lockStats);
    st->put += 1;
    pthread_mutex_unlock(&lockStats);
    
    //enviamos la respuesta la servidor una vez hecho su pedido
    char comm = OK;
    write(csock, &comm, 1);
  
  } else if (comando == DEL) { 
    
    int lenClave = readn(csock); //leemos la longitud de la clave
    if (lenClave <= 0) {
      perror("Error leyendo la longitud de la clave");
      return;
    }
    char* clave = safe_malloc(sizeof(char), lenClave+1, (*th), (*lru)); //reservamos memoria
    readm(csock, clave, lenClave); //leemos la clave
    clave[lenClave] = '\0';
    
    //eliminamos el par {clave,valor} correspondiente 
    //a la clave ingresada como argumento
    int c = eliminar_nodo_tabla((*th), clave, (*lru), 1);
    free(clave); //una vez eliminado el comando, liberamos la clave
    
    //tomamos el lock para modificar uno de los contadores de Stats
    //en este caso, incrementamos la cantidad de dels realizados
    pthread_mutex_lock(&lockStats);
    st->del += 1;
    pthread_mutex_unlock(&lockStats);

    if (c) { //verificamos si la clave fue encontrada o no

      //tomamos el lock para modificar uno de los contadores de Stats
      //en este caso, decrementamos la cantidad de keys ya que 
      //eliminamos un par {clave,valor}
      pthread_mutex_lock(&lockStats);
      st->keys -= 1;
      pthread_mutex_unlock(&lockStats);
      
      //respondemos al cliente
      char comm = OK;
      write(csock, &comm, 1);

    } else { //en caso de no encontrar el par a eliminar

      //respondemos al cliente
      char comm = ENOTFOUND;
      write(csock, &comm, 1);
    }
  
  } else if (comando == GET) { 
    
    int lenClave = readn(csock); //leemos la longitud de la clave
    if (lenClave <= 0) {
      perror("Error leyendo la longitud de la clave");
      return;
    }
    char* clave = safe_malloc(sizeof(char), lenClave+1, (*th), (*lru)); //reservamos memoria
    readm(csock, clave, lenClave); //leemos la clave
    clave[lenClave] = '\0';
    
    //buscamos en la tablahash el valor asociado a la clave que se pasa como argumento
    char* v = buscar_tabla((*th), clave, (*lru));
    free(clave); //una vez realizada la busqueda del comando, liberamos la clave

    //tomamos el lock para modificar uno de los contadores de Stats
    //en este caso, incrementamos la cantidad de gets realizados
    pthread_mutex_lock(&lockStats);
    st->get += 1;
    pthread_mutex_unlock(&lockStats);
    
    if (v != NULL) { //si encontramos el valor

      //mandamos el valor solicitado al cliente
      char comm = OKE;
      int len = strlen(v);
      int len_net = htonl(len); //convertimos el valor a big-endian (para poder enviarlo por el socket)
                               //htonl -> thread-safe
      int bufLength = 1 + 4 + len;
      char buffer[bufLength];
      //armamos una unica rta a enviar
      // OK + lenValor + valor
      memcpy(buffer, &comm, sizeof(char));
      memcpy(buffer+1, &len_net, 4);
      memcpy(buffer+5, v, len);
      write(csock, buffer, bufLength);

    } else { //en caso de no encontrar el par
      
      //respondemos al cliente
      char comm = ENOTFOUND;
      write(csock, &comm, 1);
    }
  
  } else if (comando == STATS) { //obtenemos la cantidad de veces que se realizó c/pedido
                                //al igual que la cantidad de claves ingresadas
    char buffer[128];
    int len = snprintf(buffer, sizeof(buffer), "PUTS=%lld DELS=%lld GETS=%lld KEYS=%lld\n",
                         st->put, st->del, st->get, st->keys);
    if (len < 0) {
      perror("Error formateando la cadena");
      return;
    }
    
    char comm = OKE;
    int len_net = htonl(len); //convertimos el valor a big-endian (para poder enviarlo por el socket)
                             //htonl -> thread-safe
    int bufLength = 1 + 4 + len;
    char buff[bufLength];
    //armamos una unica rta a enviar
    // OK + lenStats + stats
    memcpy(buff, &comm, sizeof(char));
    memcpy(buff+1, &len_net, 4);
    memcpy(buff+5, buffer, len);
    write(csock, buff, bufLength);
  
  } else { //el comando ingresado por el cliente no es válido
    
    char buffer[1024];
    read(csock, buffer, sizeof(buffer)); //leemos basura restante en el socket

    //respondemos al cliente
    char comm = EINVALID;
    write(csock, &comm, 1);
  }
  
  //rearmamos el socket
  //necesario debido a EPOLLONESHOT
  struct epoll_event event;
  event.events = EPOLLIN | EPOLLONESHOT;
  event.data.fd = csock;
  if (epoll_ctl(epfd, EPOLL_CTL_MOD, csock, &event) == -1) {
    perror("epoll_ctl: conn_sock");
    exit(EXIT_FAILURE);
  }
}

//espera la llegada de clientes estableciendo
//conexiones con esto para que manden sus pedidos.
//estos pedidos son manejados con parserBin
void *wait_for_clients(void* args) {

  //guardamos los argumentos recibidos
  Wfc w = (Wfc)args;
  TablaHash *th = w->th;
  ListaLru *lru = w->lru;
  int lsock = w->lsock;
  
  struct epoll_event events[MAX_EVENTS];
  int nfds, conn_sock;
  
  for (;;) { //bucle infinito para la espera de clientes
    printf("Esperando eventos\n");
    nfds = epoll_wait(epfd, events, MAX_EVENTS, -1); //en events se guardan los fd que posean eventos disponibles (ready list)
                                                    //y retorna la cantidad de estos
    if (nfds == -1) { 
      perror("epoll_wait");
      exit(EXIT_FAILURE);
    }
    
    for (int n = 0; n < nfds; ++n) {
      if (events[n].data.fd == lsock) { //si hay un evento,
        conn_sock = accept(lsock, NULL, NULL); //acepta la conexion con el socket
        if (conn_sock == -1) {
          perror("accept");
          exit(EXIT_FAILURE);
        }
        
        //agregamos la nueva conexion a la instancia epoll
        struct epoll_event event;
        event.events = EPOLLIN | EPOLLONESHOT;
        event.data.fd = conn_sock;
        if (epoll_ctl(epfd, EPOLL_CTL_ADD, conn_sock, &event) == -1) {
          perror("epoll_ctl: conn_sock");
          exit(EXIT_FAILURE);
        }
        
        //rearmamos el socket de escucha
        event.events = EPOLLIN | EPOLLONESHOT;
        event.data.fd = lsock;
        if (epoll_ctl(epfd, EPOLL_CTL_MOD, lsock, &event) == -1) {
          perror("epoll_ctl: lsock");
          exit(EXIT_FAILURE);
        }
          
      } else {
        //una vez establecida la conexion, se manejan los pedidos
        parserBin(events[n].data.fd, epfd, th, lru);
      }
    }
  }
}

//The Linux Programming Interface
//In order for an unprivileged user to employ the program,
//we must set this capability in the file permitted capability set, with
//sudo setcap cap_net_bind_service=p ./a.out
/* static int modifyCap(int capability, int setting) {
	cap_t caps;
 	cap_value_t capList[1];
 	
	// Retrieve caller's current capabilities 
 	caps = cap_get_proc();
 	if (caps == NULL)
 		return -1;
 	
	//Change setting of 'capability' in the effective set of 'caps'. The
 	//third argument, 1, is the number of items in the array 'capList'. 
 	capList[0] = capability;
 	
	if (cap_set_flag(caps, CAP_EFFECTIVE, 1, capList, setting) == -1) {
 		cap_free(caps);
 		return -1;
 	}
 	
	// Push modified capability sets back to kernel, to change
 	//caller's capabilities 
 	if (cap_set_proc(caps) == -1) {
 		cap_free(caps);
 		return -1;
 	}

 	// Free the structure that was allocated by libcap 
 	if (cap_free(caps) == -1)
 		return -1;
 	
	return 0;
}

static int dropAllCaps(void) { // Drop all capabilities from all sets 
 	cap_t empty;
 	int s;
 	empty = cap_init();
 	if (empty == NULL)
 		return -1;
 	s = cap_set_proc(empty);
 	if (cap_free(empty) == -1)
 		return -1;
 	return s;
} */


//crea un socket de escucha en puerto 889 TCP (puerto privilegiado)
int mk_lsock() {
	struct sockaddr_in sa;
	int lsock;
	int rc;
	int yes = 1; // se utiliza para activar el SO_REUSEADDR

	/* if (modifyCap(CAP_NET_BIND_SERVICE, CAP_SET) == -1) {
	  perror("modifyCap() failed");
	} */

	// Crear socket
	lsock = socket(AF_INET, SOCK_STREAM, 0);
	if (lsock < 0)
		quit("socket");

  //SOL_REUSEADDR se utiliza para poder reiniciar 
  //el servidor de inmediato si este se detiene 
	if (setsockopt(lsock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes) == 1)
		quit("setsockopt");

  //convertimos a big-endian para poder bindear
	sa.sin_family = AF_INET;
	sa.sin_port = htons(PORT);
	sa.sin_addr.s_addr = htonl(INADDR_ANY);

	// Bindear al puerto 889 TCP, en todas las direcciones disponibles 
	rc = bind(lsock, (struct sockaddr *)&sa, sizeof sa);
	if (rc < 0)
		quit("bind");

	// Setear en modo escucha
	rc = listen(lsock, 10);
	if (rc < 0)
		quit("listen");

	//dropAllCaps();

	return lsock;
}

//Chatgpt
//configura el limite de memoria
void limitar_memoria(size_t max_memoria_mb) {
  struct rlimit limite;
  limite.rlim_cur = max_memoria_mb * 1024 * 1024;  // Soft limit
  limite.rlim_max = max_memoria_mb * 1024 * 1024;  // Hard limit  
  if (setrlimit(RLIMIT_AS, &limite) != 0) {
    perror("Error al establecer límite de memoria");
    exit(EXIT_FAILURE);
  }
}

int main() {
	
	//establece el limite de memoria
  limitar_memoria(2000);  //limitar a 2GB

  //inicializamos los mutex de la tablahash recursivos
	pthread_mutexattr_init(&recHash);
	pthread_mutexattr_settype(&recHash, PTHREAD_MUTEX_RECURSIVE_NP);
	for (int i = 0; i < LOCKS; i++) {
		pthread_mutex_init(&locks[i], &recHash);
	}
	
  //inicializamos el mutex de la lru recursivo
	pthread_mutexattr_init(&recLru);
	pthread_mutexattr_settype(&recLru, PTHREAD_MUTEX_RECURSIVE_NP);
	pthread_mutex_init(&lockLru, &recLru);

  //inicializamos el mutex de stats
	pthread_mutex_init(&lockStats, 0);

	int lsock;
	lsock = mk_lsock(); //socket de escucha
	
	if ((epfd = epoll_create1(0)) == -1) { //creamos la instancia epoll
		perror("epoll_create1");
		exit(EXIT_FAILURE);
	}

	struct epoll_event ev;
	memset(&ev, 0, sizeof(struct epoll_event)); //inicializamos la estructura con ceros, para evitar que haya basura
	ev.events = EPOLLIN | EPOLLONESHOT; //indica que el fd esta disponible para leer
	ev.data.fd = lsock; //agg socket de escucha a la estructura
	
	if (epoll_ctl(epfd, EPOLL_CTL_ADD, lsock, &ev) == -1) { //se agrega el socket a la interest list
		perror("epoll_ctl: lsock");
		exit(EXIT_FAILURE);
	}	

  //creamos las estructuras
	st = crear_stats();
	TablaHash th = crear_tabla(TH, (FuncionHash) funcion_hash, (FuncionComparadora) compCom, (FuncionDestructora) destrCom);
	ListaLru lru = crear_lru();

  //guardamos las estructuras + el socket de escucha 
  //en una estructura para pasarlo a wait_for_clients
	Wfc w = safe_malloc(sizeof(struct _wfc),1,th,lru);
	w->lsock = lsock;
	w->th = &th;
	w->lru = &lru;
	
  //creamos los threads y llamamos a wait_for_clients
	pthread_t threads[NTHREADS];
	for (unsigned i = 0; i < NTHREADS; i++) {
	 	pthread_create(&(threads[i]), NULL, wait_for_clients, (void*)w);
  }

  //join correspondiente
	for (unsigned i = 0; i < NTHREADS; i++) {
	 	pthread_join((threads[i]), NULL);
	}

	return 0;
}
