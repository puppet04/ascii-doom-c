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
} NetContext;

/* Inicializa o subsistema de rede no modo escolhido. Retorna 1 em sucesso, 0 em erro. */
int net_init(NetContext *net, NetMode mode, const char *ip, int port);

/* Envia o estado atual do jogador local para o outro participante. */
int net_send_state(NetContext *net, const Player *local_player, int shooting);

/* Recebe pacotes pendentes sem travar o loop. Retorna 1 se recebeu atualizacao. */
int net_receive_state(NetContext *net);

/* Fecha o socket e limpa recursos de rede. */
void net_close(NetContext *net);

#endif /* NETWORK_H */
