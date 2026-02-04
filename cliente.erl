-module(cliente).
-define(PORT, 8888).
-define(PUT, 11).
-define(DEL, 12).
-define(GET, 13).
-define(STATS, 21).
-define(OK, 101).
-define(EINVALID, 111).
-define(ENOTFOUND, 112).
-define(EBINARY, 113).
-define(EBIG, 114).
-define(EUNK, 115).
-define(OKE, 116).
-export([start/1, connect/1, server_hash/2, put/3, get/2, del/2, stats/1, status/1, server_answer/1, create_msg/4, messenger/2, test_put/2, test_get/2, test_del/2, test_put_large/2, test_get_large/2, test_del_large/2]).

%lanzamos un proceso por cliente
start(Servers) -> spawn(?MODULE, connect, [Servers]).

%recibe una lista de IPs (correspondiente a los servers), establece una conexion entre el cliente
%y cada server creando una lista de pares {socket de la conexion, 0}, el cero se utiliza para contar
%las claves que el cliente ingresa en cada servidor.
%Luego, pide un ID por teclado para identificar a cada cliente. 
%Por ultimo, llama a messenger(), la cual manejará los pedidos.
connect(Servers) ->
  Connections = lists:map(fun({IP,Port}) ->
    case gen_tcp:connect(IP, Port, [binary, {active, false}]) of %map que conecta cada servidor, dada su IP
      {ok, Socket} -> {Socket, 0};
      {error, Reason} -> {error, Reason}
    end
  end, Servers),
  
  io:format("~n Ingrese su ID: "),
  Id = io:get_line(""),
  IdSinSalto = string:trim(Id),
  io:format("Ingreso ~p~n", [IdSinSalto]),
  messenger(Connections,IdSinSalto).

%se encarga de manejar envios y respuestas.
%recibe los pedidos de los clientes, se encarga de asignar un servidor y 
%crea el mensaje correspondiente para mandarlo al servidor asigando.
messenger(Connections,Id) ->
  receive
    {11, Key, Value} ->
      {AssServer,_ServerCount} = server_hash(Key, Connections), %asigna servidor
      Msg = create_msg(11,Key,Value,Id), %crea mensaje binario
      %io:format("Armamos el mje: ~p~n", [Msg]),
      case gen_tcp:send(AssServer, Msg) of
        {error, Reason} -> exit({error, Reason});
        ok -> server_answer(AssServer) %espera rta del servidor si se pudo enviar el pedido del cliente
      end,
      NewConnections = lists:map(fun({Socket,Count}) -> if Socket == AssServer -> {Socket,Count+1}; %aumenta el numero de claves, por cantidad de puts
                                                        true -> {Socket,Count} end end, Connections),
      messenger(NewConnections,Id); %llamada recursiva para mas pedidos
    {12,Key} ->
      {AssServer,_ServerCount} = server_hash(Key, Connections), %asigna servidor
      Msg = create_msg(12,Key,basura,Id), %crea mensaje binario
      case gen_tcp:send(AssServer, Msg) of
        {error, Reason} -> exit({error, Reason});
        ok -> server_answer(AssServer) %espera rta del servidor si se pudo enviar el pedido del cliente
      end,
      NewConnections = lists:map(fun({Socket,Count}) -> if Socket == AssServer -> {Socket,Count-1}; %decrementa el numero de claves, por cantidad de dels
                                                        true -> {Socket,Count} end end, Connections),
      messenger(NewConnections,Id); %llamada recursiva para mas pedidos
    {13,Key} ->
      {AssServer,_ServerCount} = server_hash(Key, Connections), %asigna servidor
      Msg = create_msg(13,Key,basura,Id), %crea mensaje binario
      case gen_tcp:send(AssServer, Msg) of
        {error, Reason} -> exit({error, Reason});
        ok -> server_answer(AssServer) %espera rta del servidor si se pudo enviar el pedido del cliente
      end,
      messenger(Connections,Id); %llamada recursiva para mas pedidos
    {21} ->
      Msg = <<?STATS:8>>,
      lists:map(fun({Socket,_Count}) -> 
        case gen_tcp:send(Socket, Msg) of 
          {error, Reason} -> exit({error, Reason});
          ok -> server_answer(Socket) %espera rta del servidor si se pudo enviar el pedido del cliente
        end
      end, Connections),
      messenger(Connections,Id); %llamada recursiva para mas pedidos
    {status} ->
      calculate_percentages(Connections),
      messenger(Connections,Id); %llamada recursiva para mas pedidos
    _ -> ok
  end.

%recibe la lista de pares {conexion, cant claves}, y obtiene
%la cantidad total de claves ingresadas por el cliente, mas el porcentaje de 
%estas en cada servidor
calculate_percentages(Servers) ->
  %calcula el total de claves
  TotalKeys = lists:sum([Keys || {_, Keys} <- Servers]),
  %calcula el porcentaje de claves en cada servidor
  Percentages =
    case TotalKeys of
      0 -> [{Server, 0} || {Server, _} <- Servers];  %evita división por cero
      _ -> [{Server, round((Keys * 100) / TotalKeys)} || {Server, Keys} <- Servers]
    end,
  io:format("Total ~p claves: ", [TotalKeys]),
  lists:foldl(fun({_Server, Percentage}, Index) ->
    io:format("server ~p: ~p%~n", [Index, Percentage]),
    Index + 1
  end, 1, Percentages),
  Percentages.


%---------------------------------------- funciones para testeo
test_put_large(0, _) -> ok;
test_put_large(N, Pid) ->
  Key = lists:duplicate(128, $K) ++ integer_to_list(N),  %% Clave larga (~128 caracteres)
  Value = lists:duplicate(100*1024, $A),  %% 100 MB de 'A'
  put(Pid, Key, Value),
  test_put_large(N-1, Pid).

test_get_large(0, _) -> ok;
test_get_large(N, Pid) ->
  Key = lists:duplicate(128, $K) ++ integer_to_list(N),  %% Clave larga (~128 caracteres)
  %Value = lists:duplicate(100 * 900 * 1024, $A),  %% 100 MB de 'A'
  get(Pid, Key),
  test_get_large(N-1, Pid).

test_del_large(0, _) -> ok;
test_del_large(N, Pid) ->
  Key = lists:duplicate(128, $K) ++ integer_to_list(N),  %% Clave larga (~128 caracteres)
  %Value = lists:duplicate(100 * 900 * 1024, $A),  %% 100 MB de 'A'
  del(Pid, Key),
  test_del_large(N-1, Pid).

test_put(0, _) -> ok;
test_put(N, Pid) ->
  Str = integer_to_list(N),
  put(Pid, Str, Str),
  test_put(N-1, Pid).

test_get(0, _) -> ok;
test_get(N, Pid) ->
  Str = integer_to_list(N),
  get(Pid, Str),
  test_get(N-1, Pid).

test_del(0, _) -> ok;
test_del(N, Pid) ->
  Str = integer_to_list(N),
  del(Pid, Str),
  test_del(N-1, Pid).
%---------------------------------------- funciones para testeo

%funcion utilizada por el cliente para ingresar un par {clave,valor}.
%la funcion tmb recibe el Pid de la conexion para poder mandar el pedido
%al messenger() que se encarga de ellos.
put(Pid,K,V) ->
  Pid ! {11,K,V}.

%funcion utilizada por el cliente para obtener el valor asociado a la clave ingresada.
%la funcion tmb recibe el Pid de la conexion para poder mandar el pedido
%al messenger() que se encarga de ellos.
get(Pid,K) ->
  Pid ! {13,K}.

%funcion utilizada por el cliente para eliminar un par {clave,valor}.
%la funcion tmb recibe el Pid de la conexion para poder mandar el pedido
%al messenger() que se encarga de ellos.
del(Pid,K) -> 
  Pid ! {12,K}.

%funcion para ver el estado de los servidor conectados, la cantidad
%de claves ingresada por el cliente.
%la funcion tmb recibe el Pid de la conexion para poder mandar el pedido
%al messenger() que se encarga de ellos.
status(Pid) ->
  Pid ! {status}.

%funcion para ver las estadísticas asociadas a esta ejecucion de la caché.
%la funcion tmb recibe el Pid de la conexion para poder mandar el pedido
%al messenger() que se encarga de ellos.
stats(Pid) ->
  Pid ! {21}.

%recibe la respuesta del servidor al cliente.
server_answer(Socket) ->
  case gen_tcp:recv(Socket, 1) of  % Leer solo el primer byte (código)
    {error, Reason} -> exit({error, Reason});
    {ok, <<?OK>>} -> io:format("OK~n");
    {ok, <<?EINVALID>>} -> io:format("Error: Comando inválido~n");
    {ok, <<?ENOTFOUND>>} -> io:format("Error: Clave no encontrada~n");
    {ok, <<?EBIG>>} -> io:format("Error: Valor demasiado grande~n");
    {ok, <<?EUNK>>} -> io:format("Error: Error desconocido~n");
    {ok, <<?OKE>>} ->  % "OKE": el servidor envía longitud + valor/estadísticas
          case gen_tcp:recv(Socket, 4) of  % Leer los 4 bytes de la longitud
            {ok, <<Len:32/integer>>} -> 
              case gen_tcp:recv(Socket, Len) of  % Leer el valor completo
                {ok, Rta} -> io:format("OK ~s~n", [Rta]);
                {error, Reason} -> exit({error, {rta_no_recibida, Reason}})
              end;
            {error, Reason} -> exit({error, {longitud_no_recibida, Reason}})
          end;
    _ -> io:format("Respuesta inesperada del servidor~n")
  end.


%asigna el servidor.
server_hash(K, Connections) ->
  Hash = erlang:phash2(K, length(Connections)),
  lists:nth(Hash+1,Connections).

%crea el mensaje para enviar el pedido al servidor.
create_msg(Com,K,V,Id) ->
  Key = string:concat(Id,K),
  KeyId = list_to_binary(Key),
  if 
    Com == 11 ->
      Value = list_to_binary(V),
      KeySize = <<(byte_size(KeyId)):32/big>>,
      ValueSize = <<(byte_size(Value)):32/big>>,
      <<?PUT:8, KeySize/binary, KeyId/binary, ValueSize/binary, Value/binary>>;
    true ->
      <<Com:8, (byte_size(KeyId)):32/big, KeyId/binary>>
  end.
