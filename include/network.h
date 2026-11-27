#ifndef NETWORK_H
#define NETWORK_H

#include "player.h"

/*
 * MODULO 4: MULTIPLAYER LAN/UDP VIA WINSOCK2 (ATE 4 JOGADORES - CO-OP / PVP)
 * Responsavel pela comunicacao em tempo real entre ate 4 jogadores na mesma rede
 * (ou no mesmo PC via 127.0.0.1) em topologia estrela (Host retransmite snapshots
 * via sockets UDP nao-bloqueantes).
 *
 * Obs: Os headers do Windows (<winsock2.h>) ficam isolados em src/network.c
 * para evitar conflito de nomes com a biblioteca grafica Raylib.
 */

#define NET_MAX_PLAYERS 4
#define DEFAULT_PORT 5000
#define NET_DEFAULT_RELAY_URL "wss://sector404-relay.onrender.com"

/* Modo de operacao da sessao de rede nesta instancia do jogo. */
typedef enum {
    NET_MODE_OFFLINE = 0,
    NET_MODE_HOST = 1,
    NET_MODE_CLIENT = 2
} NetMode;

/* Regra da partida multiplayer (cooperativo contra inimigos ou PvP entre jogadores). */
typedef enum {
    NET_GAME_COOP = 0,
    NET_GAME_PVP  = 1
} NetGameMode;

/* Transporte de rede usado pela sessao (UDP local/LAN ou WebSocket Relay na nuvem). */
typedef enum {
    NET_TRANSPORT_UDP = 0,
    NET_TRANSPORT_RELAY = 1
} NetTransport;

/* Estado da conexao ao servidor Relay online. */
typedef enum {
    NET_RELAY_IDLE = 0,
    NET_RELAY_CONNECTING = 1,
    NET_RELAY_READY = 2,
    NET_RELAY_FAILED = 3,
    NET_RELAY_CLOSED = 4
} NetRelayStatus;

/* Pacote legado simples usado no modo 1v1 e nos testes unitarios de loopback. */
typedef struct {
    int player_id;
    float x;
    float y;
    float angle;
    int health;
    int shooting;
} NetPacket;

/* Numeros magicos para distinguir tipos de pacotes UDP nao-bloqueantes. */
#define NET_CTRL_MAGIC    0x4D4F4441u /* "ADOM": controle de lobby/descoberta */
#define NET_ROSTER_MAGIC  0x524D4441u /* "ADMR": lista completa dos 4 slots no lobby */
#define NET_INPUT_MAGIC   0x494D4441u /* "ADMI": estado de 1 cliente + eventos PvP -> Host */
#define NET_SNAP_MAGIC    0x534D4441u /* "ADMS": snapshot completo dos 4 jogadores -> Clientes */
#define NET_RESTORE_MAGIC 0x544D4441u /* "ADMT": restauracao de estado ao reconectar na partida */
#define NET_MAX_SAVED_PLAYERS 8

typedef enum {
    NET_MSG_DISCOVER_REQ   = 1, /* Broadcast do cliente procurando hosts na LAN */
    NET_MSG_DISCOVER_OFFER = 2, /* Resposta do host com nome, fase, modo e lotacao */
    NET_MSG_LOBBY_STATE    = 3, /* Heartbeat periodico sincronizando pronto, personagem, fase e inicio */
    NET_MSG_LOBBY_LEAVE    = 4  /* Aviso imediato ao fechar/sair da sala */
} NetMsgType;

/* Payload de controle enviado antes e durante a sala de espera multiplayer. */
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
    int game_mode;
    int visual_id;
    CharacterAppearance appearance;
    int player_count;
    int slot_id;
    unsigned int host_session_id;
    unsigned int client_id;
    int in_game;
} NetCtrlPacket;

/* Dados completos de cada um dos 4 slots de jogador no lobby e durante a partida. */
typedef struct {
    int active;
    int player_id; /* 1..4 */
    int character_idx;
    int visual_id;
    CharacterAppearance appearance;
    int ready;
    char name[32];
    float x;
    float y;
    float angle;
    int health;
    int max_health;
    int shooting;
    int kills;
    int has_pos;
    unsigned int client_id;
    int ammo;
    int active_weapon;
    int score;
} NetPlayerSlot;

/* Estado salvo pelo Host quando um jogador sai ou cai para permitir reconexao integra. */
typedef struct {
    int valid;
    unsigned int client_id;
    unsigned int last_ip;
    int preferred_slot;
    char name[32];
    int character_idx;
    int visual_id;
    CharacterAppearance appearance;
    float x;
    float y;
    float angle;
    int health;
    int max_health;
    int ammo;
    int active_weapon;
    int score;
    AbilityState ability_state;
    int outgoing_pvp_dmg[NET_MAX_PLAYERS];
    int killed_by_counts[NET_MAX_PLAYERS];
    int applied_pvp_dmg;
} NetSavedPlayerState;

/* Pacote enviado pelo Host ao cliente que reconecta durante uma partida em andamento. */
typedef struct {
    unsigned int magic;
    unsigned int host_session_id;
    unsigned int client_id;
    int slot;
    int level_idx;
    int game_mode;
    int has_saved_state;
    NetSavedPlayerState saved;
} NetRestoreStatePacket;

/* Endereco UDP de um peer conectado (usado no roteamento estrela do Host). */
typedef struct {
    unsigned int ip;
    unsigned short port;
    int has_addr;
    double last_seen;
} NetPeerAddr;

/* Guarda temporaria contra pacotes atrasados logo apos um cliente enviar LEAVE. */
typedef struct {
    unsigned int ip;
    unsigned short port;
    double ignore_until;
} NetLeftPeerGuard;

/* Pacote enviado pelo Host sincronizando os 4 slots no lobby. */
typedef struct {
    unsigned int magic;
    int level_idx;
    int game_mode;
    int game_started;
    int your_slot;
    int active_count;
    char map_name[32];
    NetPlayerSlot slots[NET_MAX_PLAYERS];
} NetLobbyRosterPacket;

/* Pacote enviado por cada Cliente ao Host durante a partida. */
typedef struct {
    unsigned int magic;
    int slot;
    int player_id;
    int visual_id;
    CharacterAppearance appearance;
    char name[32];
    float x;
    float y;
    float angle;
    int health;
    int max_health;
    int shooting;
    int outgoing_pvp_dmg[NET_MAX_PLAYERS]; /* Dano cumulativo causado em cada slot */
    int killed_by_counts[NET_MAX_PLAYERS]; /* Mortes cumulativas sofridas por cada slot */
    unsigned int client_id;
    int character_idx;
    int ammo;
    int active_weapon;
    int score;
    AbilityState ability_state;
    int applied_pvp_dmg;
} NetMatchInputPacket;

/* Snapshot completo dos 4 jogadores transmitido pelo Host a todos os clientes. */
typedef struct {
    unsigned int magic;
    int game_mode;
    int level_idx;
    int your_slot;
    int active_count;
    NetPlayerSlot slots[NET_MAX_PLAYERS];
    int total_dmg_to_slot[NET_MAX_PLAYERS];
    int last_attacker_of_slot[NET_MAX_PLAYERS];
    int slot_pvp_kills[NET_MAX_PLAYERS];
} NetMatchSnapshotPacket;

/* Registro de uma sala descoberta pelo scanner UDP de LAN em localhost/rede local. */
typedef struct {
    char ip[64];
    char host_name[32];
    char map_name[32];
    int level_idx;
    int port;
    double last_seen; /* Timestamp usado para expirar salas inativas apos 3s */
    int game_mode;
    int player_count;
    unsigned int host_session_id;
    int in_game;
} NetDiscoveredRoom;

/* Estado completo de uma sessao de rede (socket Winsock2 + estado 1..4 jogadores). */
typedef struct {
    NetMode mode;
    int initialized;
    int connected;
    int port;
    unsigned long long sock_handle; /* SOCKET opaco (unsigned long long para nao expor winsock2.h) */
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

    /* Campos multi-jogador (ate 4 jogadores em estrela + Co-op / PvP) */
    int local_slot;                         /* 0 para Host, 1..3 para Cliente (-1 ate atribuir) */
    int game_mode;                          /* NET_GAME_COOP ou NET_GAME_PVP */
    int active_player_count;                /* Quantidade atual de jogadores na sala (1..4) */
    NetPlayerSlot slots[NET_MAX_PLAYERS];
    NetPeerAddr peer_addrs[NET_MAX_PLAYERS];
    int outgoing_pvp_dmg[NET_MAX_PLAYERS];  /* Dano cumulativo local contra outros slots */
    int killed_by_counts[NET_MAX_PLAYERS];  /* Mortes cumulativas locais por atacante */
    int remote_pvp_dmg_matrix[NET_MAX_PLAYERS][NET_MAX_PLAYERS];
    int remote_killed_by_matrix[NET_MAX_PLAYERS][NET_MAX_PLAYERS];
    int total_dmg_to_slot[NET_MAX_PLAYERS];
    int last_attacker_of_slot[NET_MAX_PLAYERS];
    int slot_pvp_kills[NET_MAX_PLAYERS];
    int applied_pvp_dmg;
    double last_host_rx;

    /* Sessao unica de Host, identificacao persistente de cliente e estado de reconexao */
    unsigned int host_session_id;
    unsigned int local_client_id;
    int in_game;
    int host_disconnected;
    int has_restore_state;
    NetSavedPlayerState restore_state;
    NetSavedPlayerState saved_players[NET_MAX_SAVED_PLAYERS];
    NetLeftPeerGuard left_guards[NET_MAX_SAVED_PLAYERS];

    /* Transporte Online via WebSocket Relay (porta 443) */
    NetTransport transport;
    unsigned long long relay_handle;
    char room_code[16];
    char relay_url[160];
} NetContext;

/* Inicializa o subsistema de rede no modo escolhido. Retorna 1 em sucesso, 0 em erro. */
int net_init(NetContext *net, NetMode mode, const char *ip, int port);

/* Retorna identificador persistente do cliente local para reconexao segura. */
unsigned int net_get_local_client_id(void);

/* Descobre o IPv4 local ativo da maquina (fallback para "127.0.0.1"). */
void net_get_local_ipv4(char *out_ip, int out_size);

/* Lista todos os IPs IPv4 ativos da maquina (Wi-Fi/LAN + VPN/Radmin) separados por " | ". */
void net_get_all_local_ipv4s(char *out_buf, int out_size);

/* Marca a partida como iniciada na sessao de rede e renova timestamps dos peers. */
void net_begin_match(NetContext *net, int level_idx, const char *map_name);

/* Atualiza os dados locais do slot 0 no Host (nome, skin, modo, fase). */
void net_host_update_local_slot(NetContext *net, const char *name, int char_idx,
                                int visual_id, const CharacterAppearance *app,
                                int game_mode, int level_idx);

/* Expira clientes silenciosos no lobby apos timeout_sec segundos sem heartbeat. */
void net_lobby_prune_stale(NetContext *net, double timeout_sec);

/* Host envia o roster completo (slots 0..3 + modo + fase) para todos os clientes. */
int net_broadcast_lobby_roster(NetContext *net, int level_idx, const char *map_name,
                               int game_started);

/* Envia estado do lobby (pronto/personagem/fase/inicio) para o par conectado. */
int net_send_lobby_state(NetContext *net, const NetCtrlPacket *pkt);

/* Notifica os demais jogadores ao sair ou fechar o lobby/partida. */
int net_send_lobby_leave(NetContext *net);

/* Processa pacotes de lobby e descoberta LAN sem bloquear. */
int net_poll_lobby(NetContext *net, const NetCtrlPacket *host_offer, int *peer_left_out);

/* Scanner UDP de salas abertas na LAN e em localhost. */
/* Abre socket UDP nao-bloqueante para escutar respostas de hosts na rede local. */
int net_discovery_open(unsigned long long *out_sock);
/* Envia pacote de busca (broadcast na LAN e loopback local) na porta informada. */
void net_discovery_send_probe(unsigned long long sock_handle, int port);
/* Le ofertas recebidas, atualiza a lista de salas e remove as que expiraram. */
int net_discovery_poll(unsigned long long sock_handle, NetDiscoveredRoom *rooms,
                       int *room_count, int max_rooms, double now);
/* Fecha o socket temporario do scanner de salas. */
void net_discovery_close(unsigned long long *sock_handle);

/* API classica 1v1 (mantida 100% compativel com testes unitarios). */
int net_send_state(NetContext *net, const Player *local_player, int shooting);
int net_receive_state(NetContext *net);

/* API multi-jogador em estrela (ate 4 jogadores, Co-op e PvP). */
/* Cliente envia seu estado ao Host; Host aplica localmente e envia snapshot a todos. */
int net_send_match_state(NetContext *net, const Player *local_player, int shooting);
/* Drena pacotes pendentes da partida e atualiza slots, vida, dano PvP e kills. */
int net_receive_match_state(NetContext *net);

/* Fecha o socket e limpa recursos de rede. */
void net_close(NetContext *net);

/* Utilitarios e inicializacao do modo Online (Relay WebSocket via WinHTTP). */
/* Gera codigo curto aleatorio de sala para compartilhar com outros jogadores online. */
void net_generate_room_code(char *out_code, int out_size);
/* Obtém a URL configurada do servidor Relay (padrao ou override por ambiente). */
void net_get_relay_url(char *out_url, int out_size);
/* Abre conexao WebSocket com o servidor Relay usando o codigo de sala informado. */
int net_init_relay(NetContext *net, NetMode mode, const char *relay_url, const char *room_code);
/* Consulta estado atual da conexao Relay e copia eventual mensagem de erro. */
NetRelayStatus net_relay_get_status(const NetContext *net, char *err_out, int err_size);

#endif /* NETWORK_H */
