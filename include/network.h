#ifndef NETWORK_H
#define NETWORK_H

#include "player.h"

/*
 * MODULO 4: MULTIPLAYER LAN/UDP VIA WINSOCK2
 * Responsavel pela comunicacao em tempo real entre dois jogadores na mesma rede
 * (ou no mesmo PC via 127.0.0.1) usando sockets UDP nao-bloqueantes.
 *
 * Obs: Os headers do Windows (<winsock2.h>) ficam isolados em src/network.c
 * para evitar conflito de nomes com a biblioteca grafica Raylib.
 */

typedef enum {
    NET_MODE_OFFLINE = 0,
    NET_MODE_HOST = 1,
    NET_MODE_CLIENT = 2
} NetMode;

typedef struct {
    int player_id;
    float x;
    float y;
    float angle;
    int health;
    int shooting;
} NetPacket;

#define NET_CTRL_MAGIC 0x4D4F4441u

typedef enum {
    NET_MSG_DISCOVER_REQ   = 1,
    NET_MSG_DISCOVER_OFFER = 2,
    NET_MSG_LOBBY_STATE    = 3,
    NET_MSG_LOBBY_LEAVE    = 4
} NetMsgType;

typedef struct {
    unsigned int magic;
    int msg_type;
    int level_idx;
    int character_idx;
    int ready;
    int game_started;
    int port;
    char player_name[32];
    char map_name[32];
} NetCtrlPacket;

typedef struct {
    char ip[64];
    char host_name[32];
    char map_name[32];
    int level_idx;
    int port;
    double last_seen;
} NetDiscoveredRoom;

typedef struct {
    NetMode mode;
    int initialized;
    int connected;
    int port;
    unsigned long long sock_handle;
    unsigned int peer_ip;
    unsigned short peer_port;
    int has_peer_addr;
    NetPacket remote_state;
    int remote_character;
    int remote_ready;
    int remote_level;
    int remote_started;
    char remote_name[32];
    char remote_map_name[32];
} NetContext;

/* Inicializa o subsistema de rede no modo escolhido. Retorna 1 em sucesso, 0 em erro. */
int net_init(NetContext *net, NetMode mode, const char *ip, int port);

/* Descobre o IPv4 local ativo da maquina (fallback para "127.0.0.1"). */
void net_get_local_ipv4(char *out_ip, int out_size);

/* Envia estado do lobby (pronto/personagem/fase/inicio) para o par conectado. */
int net_send_lobby_state(NetContext *net, const NetCtrlPacket *pkt);

/* Notifica o outro jogador que saiu do lobby. */
int net_send_lobby_leave(NetContext *net);

/* Processa pacotes de lobby e descoberta LAN sem bloquear. */
int net_poll_lobby(NetContext *net, const NetCtrlPacket *host_offer, int *peer_left_out);

/* Scanner UDP de salas abertas na LAN e em localhost. */
int net_discovery_open(unsigned long long *out_sock);
void net_discovery_send_probe(unsigned long long sock_handle, int port);
int net_discovery_poll(unsigned long long sock_handle, NetDiscoveredRoom *rooms,
                       int *room_count, int max_rooms, double now);
void net_discovery_close(unsigned long long *sock_handle);

/* Envia o estado atual do jogador local para o outro participante. */
int net_send_state(NetContext *net, const Player *local_player, int shooting);

/* Recebe pacotes pendentes sem travar o loop. Retorna 1 se recebeu atualizacao. */
int net_receive_state(NetContext *net);

/* Fecha o socket e limpa recursos de rede. */
void net_close(NetContext *net);

#endif /* NETWORK_H */
