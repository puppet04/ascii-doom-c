#include "network.h"
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#endif

/*
 * ============================================================================
 * MODULO 4: MULTIPLAYER LAN / UDP NAO-BLOQUEANTE (ATE 4 JOGADORES - ESTRELA)
 * ============================================================================
 * Arquitetura de rede sem travas (FIONBIO):
 *   1. Isolamento: <winsock2.h>/<windows.h> ficam restritos a este arquivo para
 *      evitar conflito de nomes da Win32 API com a biblioteca grafica Raylib.
 *   2. Topologia estrela (1 Host + ate 3 Clientes = 4 jogadores):
 *      - O Host atribui um slot fixo (1..3) para cada cliente pela tupla (IP, porta),
 *        permitindo jogar tanto pela LAN quanto com ate 4 janelas em 127.0.0.1.
 *      - No lobby, o Host difunde NetLobbyRosterPacket (skins, nomes, pronto, modo, fase).
 *      - Na partida, Clientes enviam NetMatchInputPacket e o Host retransmite o
 *        estado global de todos os participantes em NetMatchSnapshotPacket.
 *   3. Co-op & PvP sem perda de acertos:
 *      - Dano PvP e mortes usam contadores cumulativos monotonicos por par
 *        (atacante -> vitima), garantindo entrega mesmo com perda/reordenacao UDP.
 *   4. Compatibilidade 1v1:
 *      - net_send_state / net_receive_state (NetPacket) permanecem intactas para
 *        preservar 100% os testes unitarios legados.
 */

static double net_monotonic_sec(void) {
#ifdef _WIN32
    static LARGE_INTEGER freq = {0};
    if (freq.QuadPart == 0) {
        QueryPerformanceFrequency(&freq);
    }
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return (freq.QuadPart > 0) ? ((double)now.QuadPart / (double)freq.QuadPart) : 0.0;
#else
    return 0.0;
#endif
}

static unsigned int net_next_unique_id(void) {
    static unsigned int seq = 0u;
    seq++;
#ifdef _WIN32
    unsigned int pid = (unsigned int)GetCurrentProcessId();
    unsigned int tick = (unsigned int)GetTickCount();
    LARGE_INTEGER qpc;
    QueryPerformanceCounter(&qpc);
    unsigned int v = (tick ^ (pid * 2654435761u) ^ (unsigned int)qpc.LowPart ^ (seq * 1013904223u));
    return (v != 0u) ? v : (0xA5A50001u ^ seq);
#else
    return 0xA5A50001u ^ seq;
#endif
}

unsigned int net_get_local_client_id(void) {
    static unsigned int cached_id = 0u;
    if (cached_id == 0u) {
        cached_id = net_next_unique_id();
    }
    return cached_id;
}

#ifdef _WIN32
/* Em UDP no Windows, datagramas rejeitados por portas fechadas geram WSAECONNRESET
 * no proximo recvfrom(). Ignoramos esse codigo para drenar os pacotes validos restantes. */
static int net_should_continue_recv_err(void) {
    int err = WSAGetLastError();
    return (err == WSAECONNRESET || err == WSAEMSGSIZE);
}
#endif

static void net_recompute_host_summary(NetContext *net) {
    if (!net) {
        return;
    }
    int count = 0;
    int first_remote = -1;
    for (int s = 0; s < NET_MAX_PLAYERS; s++) {
        if (net->slots[s].active) {
            count++;
            if (s != 0 && first_remote < 0) {
                first_remote = s;
            }
        }
    }
    if (net->mode == NET_MODE_HOST) {
        if (!net->slots[0].active) {
            net->slots[0].active = 1;
            net->slots[0].player_id = 1;
            count++;
        }
        net->active_player_count = (count > 0) ? count : 1;
        net->connected = (net->active_player_count > 1) ? 1 : 0;
        if (first_remote >= 1) {
            net->peer_ip = net->peer_addrs[first_remote].ip;
            net->peer_port = net->peer_addrs[first_remote].port;
            net->has_peer_addr = net->peer_addrs[first_remote].has_addr;
            net->remote_character = net->slots[first_remote].character_idx;
            net->remote_ready = net->slots[first_remote].ready;
            snprintf(net->remote_name, sizeof(net->remote_name), "%s",
                     net->slots[first_remote].name);
        } else {
            net->peer_ip = 0;
            net->peer_port = 0;
            net->has_peer_addr = 0;
            net->remote_ready = 0;
            net->remote_name[0] = '\0';
        }
    }
}

static int net_find_client_slot_by_addr(const NetContext *net, unsigned int ip, unsigned short port) {
    for (int s = 1; s < NET_MAX_PLAYERS; s++) {
        if (net->slots[s].active &&
            net->peer_addrs[s].has_addr &&
            net->peer_addrs[s].ip == ip &&
            net->peer_addrs[s].port == port) {
            return s;
        }
    }
    return -1;
}

static int net_find_or_alloc_client_slot(NetContext *net, unsigned int ip,
                                         unsigned short port, double now) {
    int existing = net_find_client_slot_by_addr(net, ip, port);
    if (existing >= 1) {
        net->peer_addrs[existing].last_seen = now;
        return existing;
    }

    for (int s = 1; s < NET_MAX_PLAYERS; s++) {
        if (!net->slots[s].active) {
            memset(&net->slots[s], 0, sizeof(net->slots[s]));
            net->slots[s].active = 1;
            net->slots[s].player_id = s + 1;
            net->peer_addrs[s].ip = ip;
            net->peer_addrs[s].port = port;
            net->peer_addrs[s].has_addr = 1;
            net->peer_addrs[s].last_seen = now;
            net_recompute_host_summary(net);
            return s;
        }
    }
    return -1;
}

static void net_clear_slot(NetContext *net, int s) {
    if (!net || s < 1 || s >= NET_MAX_PLAYERS) {
        return;
    }
    memset(&net->slots[s], 0, sizeof(net->slots[s]));
    memset(&net->peer_addrs[s], 0, sizeof(net->peer_addrs[s]));
    net_recompute_host_summary(net);
}

static void net_mark_peer_left(NetContext *net, unsigned int ip, unsigned short port, double now) {
    if (!net || ip == 0u) {
        return;
    }
    int free_idx = -1;
    int oldest_idx = 0;
    for (int i = 0; i < NET_MAX_SAVED_PLAYERS; i++) {
        if (net->left_guards[i].ip == ip && net->left_guards[i].port == port) {
            net->left_guards[i].ignore_until = now + 0.8;
            return;
        }
        if (net->left_guards[i].ignore_until <= now && free_idx < 0) {
            free_idx = i;
        }
        if (net->left_guards[i].ignore_until < net->left_guards[oldest_idx].ignore_until) {
            oldest_idx = i;
        }
    }
    int idx = (free_idx >= 0) ? free_idx : oldest_idx;
    net->left_guards[idx].ip = ip;
    net->left_guards[idx].port = port;
    net->left_guards[idx].ignore_until = now + 0.8;
}

static void net_clear_left_guard(NetContext *net, unsigned int ip, unsigned short port) {
    if (!net) {
        return;
    }
    for (int i = 0; i < NET_MAX_SAVED_PLAYERS; i++) {
        if (net->left_guards[i].ip == ip && net->left_guards[i].port == port) {
            memset(&net->left_guards[i], 0, sizeof(net->left_guards[i]));
        }
    }
}

static int net_is_peer_recently_left(const NetContext *net, unsigned int ip, unsigned short port, double now) {
    if (!net) {
        return 0;
    }
    for (int i = 0; i < NET_MAX_SAVED_PLAYERS; i++) {
        if (net->left_guards[i].ip == ip && net->left_guards[i].port == port &&
            net->left_guards[i].ignore_until > now) {
            return 1;
        }
    }
    return 0;
}

static int host_find_saved_idx(const NetContext *net, unsigned int client_id,
                               unsigned int ip, const char *name) {
    if (!net) {
        return -1;
    }
    if (client_id != 0u) {
        for (int i = 0; i < NET_MAX_SAVED_PLAYERS; i++) {
            if (net->saved_players[i].valid && net->saved_players[i].client_id == client_id) {
                return i;
            }
        }
    }
#ifdef _WIN32
    unsigned int loopback_n = htonl(INADDR_LOOPBACK);
    if (ip != 0u && ip != loopback_n && name && name[0] != '\0') {
        for (int i = 0; i < NET_MAX_SAVED_PLAYERS; i++) {
            if (!net->saved_players[i].valid) {
                continue;
            }
            if (net->saved_players[i].last_ip == ip &&
                strncmp(net->saved_players[i].name, name, sizeof(net->saved_players[i].name)) == 0) {
                int already_active = 0;
                for (int s = 1; s < NET_MAX_PLAYERS; s++) {
                    if (net->slots[s].active &&
                        net->slots[s].client_id == net->saved_players[i].client_id &&
                        net->saved_players[i].client_id != 0u) {
                        already_active = 1;
                        break;
                    }
                }
                if (!already_active) {
                    return i;
                }
            }
        }
    }
#else
    (void)ip;
    (void)name;
#endif
    return -1;
}

static void host_upsert_saved_player_from_input(NetContext *net, int cs,
                                                const NetMatchInputPacket *in_pkt,
                                                unsigned int ip) {
    if (!net || !in_pkt || cs < 1 || cs >= NET_MAX_PLAYERS) {
        return;
    }
    int idx = host_find_saved_idx(net, in_pkt->client_id, ip, in_pkt->name);
    if (idx < 0) {
        for (int i = 0; i < NET_MAX_SAVED_PLAYERS; i++) {
            if (!net->saved_players[i].valid) {
                idx = i;
                break;
            }
        }
        if (idx < 0) {
            /* Sobrescreve uma entrada cuja vaga nao esteja ativa no momento */
            idx = 0;
            for (int i = 0; i < NET_MAX_SAVED_PLAYERS; i++) {
                int in_use = 0;
                for (int s = 1; s < NET_MAX_PLAYERS; s++) {
                    if (net->slots[s].active &&
                        net->slots[s].client_id == net->saved_players[i].client_id &&
                        net->saved_players[i].client_id != 0u) {
                        in_use = 1;
                        break;
                    }
                }
                if (!in_use) {
                    idx = i;
                    break;
                }
            }
        }
    }

    NetSavedPlayerState *sv = &net->saved_players[idx];
    sv->valid = 1;
    sv->client_id = in_pkt->client_id;
    sv->last_ip = ip;
    sv->preferred_slot = cs;
    snprintf(sv->name, sizeof(sv->name), "%s", in_pkt->name);
    sv->character_idx = in_pkt->character_idx;
    sv->visual_id = in_pkt->visual_id;
    sv->appearance = in_pkt->appearance;
    sv->x = in_pkt->x;
    sv->y = in_pkt->y;
    sv->angle = in_pkt->angle;
    sv->health = in_pkt->health;
    sv->max_health = (in_pkt->max_health > 0) ? in_pkt->max_health : 100;
    sv->ammo = in_pkt->ammo;
    sv->active_weapon = in_pkt->active_weapon;
    sv->score = in_pkt->score;
    sv->ability_state = in_pkt->ability_state;
    for (int i = 0; i < NET_MAX_PLAYERS; i++) {
        sv->outgoing_pvp_dmg[i] = in_pkt->outgoing_pvp_dmg[i];
        sv->killed_by_counts[i] = in_pkt->killed_by_counts[i];
    }
    sv->applied_pvp_dmg = in_pkt->applied_pvp_dmg;
}

static int host_alloc_slot_for_rejoin(NetContext *net, unsigned int ip,
                                      unsigned short port, int preferred_slot,
                                      double now) {
    if (!net) {
        return -1;
    }
    net_clear_left_guard(net, ip, port);
    int existing = net_find_client_slot_by_addr(net, ip, port);
    if (existing >= 1) {
        net->peer_addrs[existing].last_seen = now;
        return existing;
    }

    int chosen = -1;
    if (preferred_slot >= 1 && preferred_slot < NET_MAX_PLAYERS && !net->slots[preferred_slot].active) {
        chosen = preferred_slot;
    } else {
        for (int s = 1; s < NET_MAX_PLAYERS; s++) {
            if (!net->slots[s].active) {
                chosen = s;
                break;
            }
        }
    }
    if (chosen >= 1) {
        memset(&net->slots[chosen], 0, sizeof(net->slots[chosen]));
        net->slots[chosen].active = 1;
        net->slots[chosen].player_id = chosen + 1;
        net->peer_addrs[chosen].ip = ip;
        net->peer_addrs[chosen].port = port;
        net->peer_addrs[chosen].has_addr = 1;
        net->peer_addrs[chosen].last_seen = now;
        net_recompute_host_summary(net);
    }
    return chosen;
}

#ifdef _WIN32
static void host_send_roster_to_slot(NetContext *net, int s, int level_idx,
                                     const char *map_name, int game_started) {
    if (!net || s < 1 || s >= NET_MAX_PLAYERS ||
        !net->slots[s].active || !net->peer_addrs[s].has_addr) {
        return;
    }

    NetLobbyRosterPacket pkt;
    memset(&pkt, 0, sizeof(pkt));
    pkt.magic = NET_ROSTER_MAGIC;
    pkt.level_idx = level_idx;
    pkt.game_mode = net->game_mode;
    pkt.game_started = game_started;
    pkt.your_slot = s;
    pkt.active_count = net->active_player_count;
    if (map_name) {
        snprintf(pkt.map_name, sizeof(pkt.map_name), "%s", map_name);
    } else {
        snprintf(pkt.map_name, sizeof(pkt.map_name), "%s", net->remote_map_name);
    }
    for (int i = 0; i < NET_MAX_PLAYERS; i++) {
        pkt.slots[i] = net->slots[i];
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = (ULONG)net->peer_addrs[s].ip;
    addr.sin_port = (USHORT)net->peer_addrs[s].port;

    SOCKET sock = (SOCKET)net->sock_handle;
    sendto(sock, (const char *)&pkt, (int)sizeof(pkt), 0,
           (const struct sockaddr *)&addr, (int)sizeof(addr));
}
#endif

static void host_recompute_pvp_totals(NetContext *net) {
    if (!net) {
        return;
    }

    /* Funde eventos locais do Host (slot 0) na matriz global mantendo o maior valor */
    for (int v = 0; v < NET_MAX_PLAYERS; v++) {
        if (v != 0 && net->outgoing_pvp_dmg[v] > net->remote_pvp_dmg_matrix[0][v]) {
            net->remote_pvp_dmg_matrix[0][v] = net->outgoing_pvp_dmg[v];
            net->last_attacker_of_slot[v] = 0;
        }
    }
    for (int a = 0; a < NET_MAX_PLAYERS; a++) {
        if (a != 0 && net->killed_by_counts[a] > net->remote_killed_by_matrix[0][a]) {
            net->remote_killed_by_matrix[0][a] = net->killed_by_counts[a];
        }
    }

    for (int v = 0; v < NET_MAX_PLAYERS; v++) {
        int sum_dmg = 0;
        for (int a = 0; a < NET_MAX_PLAYERS; a++) {
            if (a != v) {
                sum_dmg += net->remote_pvp_dmg_matrix[a][v];
            }
        }
        net->total_dmg_to_slot[v] = sum_dmg;
    }

    for (int a = 0; a < NET_MAX_PLAYERS; a++) {
        int sum_kills = 0;
        for (int v = 0; v < NET_MAX_PLAYERS; v++) {
            if (v != a) {
                sum_kills += net->remote_killed_by_matrix[v][a];
            }
        }
        net->slot_pvp_kills[a] = sum_kills;
        net->slots[a].kills = sum_kills;
    }
}

#ifdef _WIN32
static int host_broadcast_match_snapshot(NetContext *net) {
    if (!net || net->mode != NET_MODE_HOST || !net->initialized) {
        return 0;
    }
    SOCKET s = (SOCKET)net->sock_handle;
    host_recompute_pvp_totals(net);
    net_recompute_host_summary(net);

    NetMatchSnapshotPacket snap;
    memset(&snap, 0, sizeof(snap));
    snap.magic = NET_SNAP_MAGIC;
    snap.game_mode = net->game_mode;
    snap.level_idx = net->remote_level;
    snap.active_count = net->active_player_count;
    for (int i = 0; i < NET_MAX_PLAYERS; i++) {
        snap.slots[i] = net->slots[i];
        snap.total_dmg_to_slot[i] = net->total_dmg_to_slot[i];
        snap.last_attacker_of_slot[i] = net->last_attacker_of_slot[i];
        snap.slot_pvp_kills[i] = net->slot_pvp_kills[i];
    }

    int sent_count = 0;
    for (int peer = 1; peer < NET_MAX_PLAYERS; peer++) {
        if (!net->slots[peer].active || !net->peer_addrs[peer].has_addr) {
            continue;
        }
        snap.your_slot = peer;
        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = (ULONG)net->peer_addrs[peer].ip;
        addr.sin_port = (USHORT)net->peer_addrs[peer].port;
        int sent = sendto(s, (const char *)&snap, (int)sizeof(snap), 0,
                          (const struct sockaddr *)&addr, (int)sizeof(addr));
        if (sent == (int)sizeof(snap)) {
            sent_count++;
        }
    }
    return (sent_count > 0) ? 1 : 0;
}

static void host_send_restore_to_addr(NetContext *net, unsigned int ip, unsigned short port,
                                      int slot, unsigned int client_id,
                                      const NetSavedPlayerState *saved) {
    if (!net || !net->initialized) {
        return;
    }
    NetRestoreStatePacket pkt;
    memset(&pkt, 0, sizeof(pkt));
    pkt.magic = NET_RESTORE_MAGIC;
    pkt.host_session_id = net->host_session_id;
    pkt.client_id = client_id;
    pkt.slot = slot;
    pkt.level_idx = net->remote_level;
    pkt.game_mode = net->game_mode;
    if (saved && saved->valid) {
        pkt.has_saved_state = 1;
        pkt.saved = *saved;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = (ULONG)ip;
    addr.sin_port = (USHORT)port;
    SOCKET s = (SOCKET)net->sock_handle;
    for (int k = 0; k < 3; k++) {
        sendto(s, (const char *)&pkt, (int)sizeof(pkt), 0,
               (const struct sockaddr *)&addr, (int)sizeof(addr));
    }
}
#endif

void net_begin_match(NetContext *net, int level_idx, const char *map_name) {
    if (!net) {
        return;
    }
    double now = net_monotonic_sec();
    net->in_game = 1;
    net->host_disconnected = 0;
    net->remote_level = level_idx;
    net->last_host_rx = now;
    if (map_name && map_name[0] != '\0') {
        snprintf(net->remote_map_name, sizeof(net->remote_map_name), "%s", map_name);
    }
    for (int s = 1; s < NET_MAX_PLAYERS; s++) {
        if (net->slots[s].active) {
            net->peer_addrs[s].last_seen = now;
        }
    }
}

/* Inicializa Winsock2 e cria um socket UDP nao-bloqueante para HOST ou CLIENT.
 * Host faz bind() na porta local para aceitar ate 3 clientes na rede.
 * Client resolve e armazena IPv4/porta do Host para enviar pacotes direto. */
int net_init(NetContext *net, NetMode mode, const char *ip, int port) {
    if (!net) {
        return 0;
    }

    memset(net, 0, sizeof(*net));
    net->mode = mode;
    net->port = (port > 0) ? port : DEFAULT_PORT;
    net->local_slot = (mode == NET_MODE_HOST) ? 0 : -1;
    net->active_player_count = 1;
    net->local_client_id = net_get_local_client_id();
    for (int i = 0; i < NET_MAX_PLAYERS; i++) {
        net->last_attacker_of_slot[i] = -1;
    }

    if (mode == NET_MODE_HOST) {
        net->host_session_id = net_next_unique_id();
        net->slots[0].active = 1;
        net->slots[0].player_id = 1;
        net->slots[0].ready = 1;
        net->slots[0].client_id = net->local_client_id;
    }

    if (mode == NET_MODE_OFFLINE) {
        return 1;
    }

#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        return 0;
    }

    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) {
        WSACleanup();
        return 0;
    }

    u_long non_blocking = 1;
    if (ioctlsocket(s, FIONBIO, &non_blocking) != 0) {
        closesocket(s);
        WSACleanup();
        return 0;
    }

    net->sock_handle = (unsigned long long)s;
    net->initialized = 1;

    if (mode == NET_MODE_HOST) {
        struct sockaddr_in local_addr;
        memset(&local_addr, 0, sizeof(local_addr));
        local_addr.sin_family = AF_INET;
        local_addr.sin_addr.s_addr = htonl(INADDR_ANY);
        local_addr.sin_port = htons((u_short)net->port);

        if (bind(s, (struct sockaddr *)&local_addr, sizeof(local_addr)) == SOCKET_ERROR) {
            net_close(net);
            return 0;
        }
    } else if (mode == NET_MODE_CLIENT) {
        const char *target_ip = (ip && ip[0] != '\0') ? ip : "127.0.0.1";
        struct in_addr addr;
        if (inet_pton(AF_INET, target_ip, &addr) != 1) {
            net_close(net);
            return 0;
        }
        net->peer_ip = (unsigned int)addr.s_addr;
        net->peer_port = (unsigned short)htons((u_short)net->port);
        net->has_peer_addr = 1;
        net->last_host_rx = net_monotonic_sec();
    }

    return 1;
#else
    (void)ip;
    return 0;
#endif
}

/* API classica 1v1 mantida compativel com testes unitarios: envia NetPacket (24 bytes). */
int net_send_state(NetContext *net, const Player *local_player, int shooting) {
    if (!net || !local_player) {
        return 0;
    }
    if (net->mode == NET_MODE_OFFLINE || !net->initialized || !net->has_peer_addr) {
        return 0;
    }

#ifdef _WIN32
    NetPacket pkt;
    pkt.player_id = local_player->id;
    pkt.x = local_player->x;
    pkt.y = local_player->y;
    pkt.angle = local_player->angle;
    pkt.health = local_player->health;
    pkt.shooting = shooting;

    struct sockaddr_in target_addr;
    memset(&target_addr, 0, sizeof(target_addr));
    target_addr.sin_family = AF_INET;
    target_addr.sin_addr.s_addr = (ULONG)net->peer_ip;
    target_addr.sin_port = (USHORT)net->peer_port;

    SOCKET s = (SOCKET)net->sock_handle;
    int sent = sendto(
        s,
        (const char *)&pkt,
        (int)sizeof(pkt),
        0,
        (const struct sockaddr *)&target_addr,
        (int)sizeof(target_addr)
    );

    return (sent == (int)sizeof(pkt)) ? 1 : 0;
#else
    (void)shooting;
    return 0;
#endif
}

/* API classica 1v1: drena pacotes UDP pendentes atualizando remote_state. */
int net_receive_state(NetContext *net) {
    if (!net || net->mode == NET_MODE_OFFLINE || !net->initialized) {
        return 0;
    }

#ifdef _WIN32
    int received_any = 0;
    SOCKET s = (SOCKET)net->sock_handle;

    for (int iter = 0; iter < 64; iter++) {
        union {
            NetPacket pkt;
            NetCtrlPacket ctrl;
            char raw[2048];
        } rx;
        struct sockaddr_in sender;
        int sender_len = (int)sizeof(sender);

        int bytes = recvfrom(
            s,
            rx.raw,
            (int)sizeof(rx.raw),
            0,
            (struct sockaddr *)&sender,
            &sender_len
        );

        if (bytes < 0) {
            if (net_should_continue_recv_err()) {
                continue;
            }
            break;
        }

        if (bytes == (int)sizeof(NetPacket)) {
            net->remote_state = rx.pkt;
            net->peer_ip = (unsigned int)sender.sin_addr.s_addr;
            net->peer_port = (unsigned short)sender.sin_port;
            net->has_peer_addr = 1;
            net->connected = 1;
            received_any = 1;
        } else if (bytes == (int)sizeof(NetCtrlPacket) && rx.ctrl.magic == NET_CTRL_MAGIC) {
            if (rx.ctrl.msg_type == NET_MSG_LOBBY_LEAVE) {
                net->connected = 0;
            }
        }
    }

    return received_any;
#else
    return 0;
#endif
}

/* Envia estado do jogador durante a partida multi-jogador (estrela ate 4 jogadores).
 * Host difunde NetMatchSnapshotPacket para todos os clientes ativos;
 * Cliente envia NetMatchInputPacket direto para o Host. */
int net_send_match_state(NetContext *net, const Player *local_player, int shooting) {
    if (!net || !local_player || net->mode == NET_MODE_OFFLINE || !net->initialized) {
        return 0;
    }

#ifdef _WIN32
    SOCKET s = (SOCKET)net->sock_handle;

    if (net->mode == NET_MODE_HOST) {
        NetPlayerSlot *self = &net->slots[0];
        self->active = 1;
        self->player_id = 1;
        self->visual_id = local_player->visual_id;
        self->appearance = local_player->appearance;
        self->ready = 1;
        snprintf(self->name, sizeof(self->name), "%s", local_player->class_name);
        self->x = local_player->x;
        self->y = local_player->y;
        self->angle = local_player->angle;
        self->health = local_player->health;
        self->max_health = (local_player->max_health > 0) ? local_player->max_health : 100;
        self->shooting = shooting;
        self->has_pos = 1;
        self->client_id = net->local_client_id;
        self->ammo = local_player->ammo;
        self->active_weapon = local_player->active_weapon;
        self->score = local_player->score;
        if (net->game_mode != NET_GAME_PVP) {
            self->kills = local_player->ability_state.kills;
        }

        return host_broadcast_match_snapshot(net);
    }

    if (!net->has_peer_addr) {
        return 0;
    }

    int my_slot = net->local_slot;
    if (my_slot >= 1 && my_slot < NET_MAX_PLAYERS) {
        NetPlayerSlot *ls = &net->slots[my_slot];
        ls->active = 1;
        ls->player_id = my_slot + 1;
        ls->visual_id = local_player->visual_id;
        ls->appearance = local_player->appearance;
        snprintf(ls->name, sizeof(ls->name), "%s", local_player->class_name);
        ls->x = local_player->x;
        ls->y = local_player->y;
        ls->angle = local_player->angle;
        ls->health = local_player->health;
        ls->max_health = (local_player->max_health > 0) ? local_player->max_health : 100;
        ls->shooting = shooting;
        ls->has_pos = 1;
        ls->client_id = net->local_client_id;
        ls->ammo = local_player->ammo;
        ls->active_weapon = local_player->active_weapon;
        ls->score = local_player->score;
        ls->kills = (net->game_mode == NET_GAME_PVP)
            ? net->slot_pvp_kills[my_slot]
            : local_player->ability_state.kills;
    }

    NetMatchInputPacket pkt;
    memset(&pkt, 0, sizeof(pkt));
    pkt.magic = NET_INPUT_MAGIC;
    pkt.slot = my_slot;
    pkt.player_id = (my_slot >= 1) ? (my_slot + 1) : local_player->id;
    pkt.visual_id = local_player->visual_id;
    pkt.appearance = local_player->appearance;
    snprintf(pkt.name, sizeof(pkt.name), "%s", local_player->class_name);
    pkt.x = local_player->x;
    pkt.y = local_player->y;
    pkt.angle = local_player->angle;
    pkt.health = local_player->health;
    pkt.max_health = (local_player->max_health > 0) ? local_player->max_health : 100;
    pkt.shooting = shooting;
    for (int i = 0; i < NET_MAX_PLAYERS; i++) {
        pkt.outgoing_pvp_dmg[i] = net->outgoing_pvp_dmg[i];
        pkt.killed_by_counts[i] = net->killed_by_counts[i];
    }
    pkt.client_id = net->local_client_id;
    pkt.character_idx = (my_slot >= 1 && my_slot < NET_MAX_PLAYERS) ? net->slots[my_slot].character_idx : 0;
    pkt.ammo = local_player->ammo;
    pkt.active_weapon = local_player->active_weapon;
    pkt.score = local_player->score;
    pkt.ability_state = local_player->ability_state;
    pkt.applied_pvp_dmg = net->applied_pvp_dmg;

    struct sockaddr_in target_addr;
    memset(&target_addr, 0, sizeof(target_addr));
    target_addr.sin_family = AF_INET;
    target_addr.sin_addr.s_addr = (ULONG)net->peer_ip;
    target_addr.sin_port = (USHORT)net->peer_port;

    int sent = sendto(s, (const char *)&pkt, (int)sizeof(pkt), 0,
                      (const struct sockaddr *)&target_addr, (int)sizeof(target_addr));
    return (sent == (int)sizeof(pkt)) ? 1 : 0;
#else
    (void)local_player;
    (void)shooting;
    return 0;
#endif
}

/* Recebe e processa todos os pacotes UDP da partida multi-jogador sem bloquear.
 * Host atualiza tabela de slots + eventos PvP + expira clientes mudos (> 5s).
 * Cliente aplica snapshots mantendo suavemente sua propria posicao local. */
int net_receive_match_state(NetContext *net) {
    if (!net || net->mode == NET_MODE_OFFLINE || !net->initialized) {
        return 0;
    }

#ifdef _WIN32
    int received_any = 0;
    SOCKET s = (SOCKET)net->sock_handle;
    double now = net_monotonic_sec();

    for (int iter = 0; iter < 64; iter++) {
        union {
            NetPacket legacy;
            NetCtrlPacket ctrl;
            NetMatchInputPacket input;
            NetMatchSnapshotPacket snap;
            char raw[2048];
        } rx;
        struct sockaddr_in sender;
        int sender_len = (int)sizeof(sender);

        int bytes = recvfrom(s, rx.raw, (int)sizeof(rx.raw), 0,
                             (struct sockaddr *)&sender, &sender_len);
        if (bytes < 0) {
            if (net_should_continue_recv_err()) {
                continue;
            }
            break;
        }

        unsigned int sip = (unsigned int)sender.sin_addr.s_addr;
        unsigned short sport = (unsigned short)sender.sin_port;

        if (bytes == (int)sizeof(NetMatchInputPacket) && rx.input.magic == NET_INPUT_MAGIC) {
            if (net->mode == NET_MODE_HOST) {
                if (net_is_peer_recently_left(net, sip, sport, now)) {
                    continue;
                }
                int cs = net_find_or_alloc_client_slot(net, sip, sport, now);
                if (cs >= 1 && cs < NET_MAX_PLAYERS) {
                    NetPlayerSlot *slot = &net->slots[cs];
                    slot->active = 1;
                    slot->player_id = cs + 1;
                    slot->visual_id = rx.input.visual_id;
                    slot->appearance = rx.input.appearance;
                    if (rx.input.name[0] != '\0') {
                        snprintf(slot->name, sizeof(slot->name), "%s", rx.input.name);
                    }
                    slot->x = rx.input.x;
                    slot->y = rx.input.y;
                    slot->angle = rx.input.angle;
                    slot->health = rx.input.health;
                    slot->max_health = (rx.input.max_health > 0) ? rx.input.max_health : 100;
                    slot->shooting = rx.input.shooting;
                    slot->has_pos = 1;
                    slot->client_id = rx.input.client_id;
                    slot->character_idx = rx.input.character_idx;
                    slot->ammo = rx.input.ammo;
                    slot->active_weapon = rx.input.active_weapon;
                    slot->score = rx.input.score;
                    if (net->game_mode != NET_GAME_PVP) {
                        slot->kills = rx.input.ability_state.kills;
                    }

                    for (int v = 0; v < NET_MAX_PLAYERS; v++) {
                        if (v != cs && rx.input.outgoing_pvp_dmg[v] > net->remote_pvp_dmg_matrix[cs][v]) {
                            net->remote_pvp_dmg_matrix[cs][v] = rx.input.outgoing_pvp_dmg[v];
                            net->last_attacker_of_slot[v] = cs;
                        }
                    }
                    for (int a = 0; a < NET_MAX_PLAYERS; a++) {
                        if (a != cs && rx.input.killed_by_counts[a] > net->remote_killed_by_matrix[cs][a]) {
                            net->remote_killed_by_matrix[cs][a] = rx.input.killed_by_counts[a];
                        }
                    }

                    host_upsert_saved_player_from_input(net, cs, &rx.input, sip);
                    net_recompute_host_summary(net);
                    net->remote_state.player_id = slot->player_id;
                    net->remote_state.x = slot->x;
                    net->remote_state.y = slot->y;
                    net->remote_state.angle = slot->angle;
                    net->remote_state.health = slot->health;
                    net->remote_state.shooting = slot->shooting;
                    received_any = 1;
                }
            }
        } else if (bytes == (int)sizeof(NetMatchSnapshotPacket) && rx.snap.magic == NET_SNAP_MAGIC) {
            if (net->mode == NET_MODE_CLIENT) {
                if (rx.snap.your_slot >= 1 && rx.snap.your_slot < NET_MAX_PLAYERS) {
                    net->local_slot = rx.snap.your_slot;
                }
                int my_slot = net->local_slot;
                NetPlayerSlot preserved = {0};
                int keep_local = (my_slot >= 1 && my_slot < NET_MAX_PLAYERS && net->slots[my_slot].has_pos);
                if (keep_local) {
                    preserved = net->slots[my_slot];
                }

                net->game_mode = rx.snap.game_mode;
                net->remote_level = rx.snap.level_idx;
                net->active_player_count = rx.snap.active_count;
                net->connected = 1;
                net->host_disconnected = 0;
                net->last_host_rx = now;

                for (int i = 0; i < NET_MAX_PLAYERS; i++) {
                    net->slots[i] = rx.snap.slots[i];
                    net->total_dmg_to_slot[i] = rx.snap.total_dmg_to_slot[i];
                    net->last_attacker_of_slot[i] = rx.snap.last_attacker_of_slot[i];
                    net->slot_pvp_kills[i] = rx.snap.slot_pvp_kills[i];
                }
                if (keep_local) {
                    net->slots[my_slot].x = preserved.x;
                    net->slots[my_slot].y = preserved.y;
                    net->slots[my_slot].angle = preserved.angle;
                    net->slots[my_slot].health = preserved.health;
                    net->slots[my_slot].max_health = preserved.max_health;
                    net->slots[my_slot].shooting = preserved.shooting;
                    net->slots[my_slot].has_pos = 1;
                }

                net->remote_state.player_id = net->slots[0].player_id;
                net->remote_state.x = net->slots[0].x;
                net->remote_state.y = net->slots[0].y;
                net->remote_state.angle = net->slots[0].angle;
                net->remote_state.health = net->slots[0].health;
                net->remote_state.shooting = net->slots[0].shooting;
                received_any = 1;
            }
        } else if (bytes == (int)sizeof(NetPacket)) {
            net->remote_state = rx.legacy;
            net->peer_ip = sip;
            net->peer_port = sport;
            net->has_peer_addr = 1;
            net->connected = 1;
            received_any = 1;
        } else if (bytes == (int)sizeof(NetCtrlPacket) && rx.ctrl.magic == NET_CTRL_MAGIC) {
            if (net->mode == NET_MODE_HOST) {
                if (rx.ctrl.msg_type == NET_MSG_DISCOVER_REQ) {
                    NetCtrlPacket reply;
                    memset(&reply, 0, sizeof(reply));
                    reply.magic = NET_CTRL_MAGIC;
                    reply.msg_type = NET_MSG_DISCOVER_OFFER;
                    reply.level_idx = net->remote_level;
                    reply.port = net->port;
                    reply.game_mode = net->game_mode;
                    reply.player_count = net->active_player_count;
                    reply.host_session_id = net->host_session_id;
                    reply.in_game = 1;
                    snprintf(reply.player_name, sizeof(reply.player_name), "%s",
                             net->slots[0].name[0] ? net->slots[0].name : "Anfitriao");
                    snprintf(reply.map_name, sizeof(reply.map_name), "%s",
                             net->remote_map_name[0] ? net->remote_map_name : "PARTIDA");
                    sendto(s, (const char *)&reply, (int)sizeof(reply), 0,
                           (const struct sockaddr *)&sender, sender_len);
                } else if (rx.ctrl.msg_type == NET_MSG_LOBBY_STATE) {
                    int existing = net_find_client_slot_by_addr(net, sip, sport);
                    if (existing >= 1 && net->slots[existing].has_pos) {
                        /* Cliente ainda em transicao do lobby para a partida */
                        host_send_roster_to_slot(net, existing, net->remote_level, NULL, 1);
                    } else {
                        int sv_idx = host_find_saved_idx(net, rx.ctrl.client_id, sip, rx.ctrl.player_name);
                        const NetSavedPlayerState *saved = (sv_idx >= 0) ? &net->saved_players[sv_idx] : NULL;
                        int cs = host_alloc_slot_for_rejoin(net, sip, sport,
                                                            saved ? saved->preferred_slot : -1, now);
                        if (cs >= 1 && cs < NET_MAX_PLAYERS) {
                            NetPlayerSlot *slot = &net->slots[cs];
                            slot->active = 1;
                            slot->player_id = cs + 1;
                            slot->ready = 1;
                            slot->client_id = rx.ctrl.client_id;
                            if (saved) {
                                slot->character_idx = saved->character_idx;
                                slot->visual_id = saved->visual_id;
                                slot->appearance = saved->appearance;
                                snprintf(slot->name, sizeof(slot->name), "%s", saved->name);
                                slot->x = saved->x;
                                slot->y = saved->y;
                                slot->angle = saved->angle;
                                slot->health = (saved->health > 0) ? saved->health : saved->max_health;
                                slot->max_health = (saved->max_health > 0) ? saved->max_health : 100;
                                slot->ammo = saved->ammo;
                                slot->active_weapon = saved->active_weapon;
                                slot->score = saved->score;
                                slot->kills = (net->game_mode == NET_GAME_PVP)
                                    ? net->slot_pvp_kills[cs] : saved->ability_state.kills;
                                slot->has_pos = 1;
                                for (int v = 0; v < NET_MAX_PLAYERS; v++) {
                                    if (v != cs && saved->outgoing_pvp_dmg[v] > net->remote_pvp_dmg_matrix[cs][v]) {
                                        net->remote_pvp_dmg_matrix[cs][v] = saved->outgoing_pvp_dmg[v];
                                    }
                                    if (v != cs && saved->killed_by_counts[v] > net->remote_killed_by_matrix[cs][v]) {
                                        net->remote_killed_by_matrix[cs][v] = saved->killed_by_counts[v];
                                    }
                                }
                            } else {
                                slot->character_idx = rx.ctrl.character_idx;
                                slot->visual_id = rx.ctrl.visual_id;
                                slot->appearance = rx.ctrl.appearance;
                                snprintf(slot->name, sizeof(slot->name), "%s", rx.ctrl.player_name);
                                slot->health = 100;
                                slot->max_health = 100;
                                slot->has_pos = 0;
                                for (int k = 0; k < NET_MAX_PLAYERS; k++) {
                                    net->remote_pvp_dmg_matrix[cs][k] = 0;
                                    net->remote_pvp_dmg_matrix[k][cs] = 0;
                                    net->remote_killed_by_matrix[cs][k] = 0;
                                    net->remote_killed_by_matrix[k][cs] = 0;
                                }
                                net->total_dmg_to_slot[cs] = 0;
                                net->slot_pvp_kills[cs] = 0;
                                net->last_attacker_of_slot[cs] = -1;
                            }
                            host_recompute_pvp_totals(net);
                            net_recompute_host_summary(net);
                            host_send_restore_to_addr(net, sip, sport, cs, rx.ctrl.client_id, saved);
                            received_any = 1;
                        }
                    }
                } else if (rx.ctrl.msg_type == NET_MSG_LOBBY_LEAVE) {
                    int cs = net_find_client_slot_by_addr(net, sip, sport);
                    net_mark_peer_left(net, sip, sport, now);
                    if (cs >= 1) {
                        net_clear_slot(net, cs);
                        host_broadcast_match_snapshot(net);
                        received_any = 1;
                    }
                }
            } else if (rx.ctrl.msg_type == NET_MSG_LOBBY_LEAVE) {
                net->connected = 0;
                net->host_disconnected = 1;
                for (int i = 0; i < NET_MAX_PLAYERS; i++) {
                    if (i != net->local_slot) {
                        net->slots[i].active = 0;
                        net->slots[i].has_pos = 0;
                    }
                }
                received_any = 1;
            }
        }
    }

    if (net->mode == NET_MODE_HOST) {
        int pruned = 0;
        for (int s = 1; s < NET_MAX_PLAYERS; s++) {
            if (net->slots[s].active && (now - net->peer_addrs[s].last_seen) > 3.0) {
                net_clear_slot(net, s);
                pruned = 1;
            }
        }
        host_recompute_pvp_totals(net);
        net_recompute_host_summary(net);
        if (pruned) {
            host_broadcast_match_snapshot(net);
        }
    } else if (net->mode == NET_MODE_CLIENT && net->connected) {
        if ((now - net->last_host_rx) > 3.0) {
            net->connected = 0;
            net->host_disconnected = 1;
        }
    }

    return received_any;
#else
    return 0;
#endif
}

void net_get_local_ipv4(char *out_ip, int out_size) {
    if (!out_ip || out_size <= 0) {
        return;
    }
    snprintf(out_ip, (size_t)out_size, "127.0.0.1");

#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        return;
    }

    int found = 0;
    SOCKET probe = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (probe != INVALID_SOCKET) {
        struct sockaddr_in remote;
        memset(&remote, 0, sizeof(remote));
        remote.sin_family = AF_INET;
        remote.sin_port = htons(53);
        inet_pton(AF_INET, "8.8.8.8", &remote.sin_addr);

        if (connect(probe, (const struct sockaddr *)&remote, sizeof(remote)) == 0) {
            struct sockaddr_in local;
            int local_len = (int)sizeof(local);
            if (getsockname(probe, (struct sockaddr *)&local, &local_len) == 0) {
                char buf[64] = "";
                if (inet_ntop(AF_INET, &local.sin_addr, buf, (socklen_t)sizeof(buf)) != NULL &&
                    strncmp(buf, "127.", 4) != 0) {
                    snprintf(out_ip, (size_t)out_size, "%s", buf);
                    found = 1;
                }
            }
        }
        closesocket(probe);
    }

    if (!found) {
        char hostname[128] = "";
        if (gethostname(hostname, (int)sizeof(hostname)) == 0) {
            struct addrinfo hints;
            struct addrinfo *res = NULL;
            memset(&hints, 0, sizeof(hints));
            hints.ai_family = AF_INET;
            hints.ai_socktype = SOCK_DGRAM;
            if (getaddrinfo(hostname, NULL, &hints, &res) == 0 && res) {
                for (struct addrinfo *it = res; it != NULL; it = it->ai_next) {
                    const struct sockaddr_in *sa = (const struct sockaddr_in *)it->ai_addr;
                    char buf[64] = "";
                    if (sa && inet_ntop(AF_INET, &sa->sin_addr, buf, (socklen_t)sizeof(buf)) != NULL &&
                        strncmp(buf, "127.", 4) != 0) {
                        snprintf(out_ip, (size_t)out_size, "%s", buf);
                        break;
                    }
                }
                freeaddrinfo(res);
            }
        }
    }

    WSACleanup();
#endif
}

void net_host_update_local_slot(NetContext *net, const char *name, int char_idx,
                                int visual_id, const CharacterAppearance *app,
                                int game_mode, int level_idx) {
    if (!net || net->mode != NET_MODE_HOST) {
        return;
    }
    net->slots[0].active = 1;
    net->slots[0].player_id = 1;
    net->slots[0].character_idx = char_idx;
    net->slots[0].visual_id = visual_id;
    if (app) {
        net->slots[0].appearance = *app;
    }
    net->slots[0].ready = 1;
    if (name) {
        snprintf(net->slots[0].name, sizeof(net->slots[0].name), "%s", name);
    }
    net->game_mode = (game_mode == NET_GAME_PVP) ? NET_GAME_PVP : NET_GAME_COOP;
    net->remote_level = level_idx;
    net_recompute_host_summary(net);
}

void net_lobby_prune_stale(NetContext *net, double timeout_sec) {
    if (!net || net->mode != NET_MODE_HOST || timeout_sec <= 0.0) {
        return;
    }
    double now = net_monotonic_sec();
    for (int s = 1; s < NET_MAX_PLAYERS; s++) {
        if (net->slots[s].active && (now - net->peer_addrs[s].last_seen) > timeout_sec) {
            net_clear_slot(net, s);
        }
    }
}

int net_broadcast_lobby_roster(NetContext *net, int level_idx, const char *map_name,
                               int game_started) {
    if (!net || net->mode != NET_MODE_HOST || !net->initialized) {
        return 0;
    }
#ifdef _WIN32
    net->remote_level = level_idx;
    if (map_name) {
        snprintf(net->remote_map_name, sizeof(net->remote_map_name), "%s", map_name);
    }
    net_recompute_host_summary(net);

    int sent_any = 0;
    for (int s = 1; s < NET_MAX_PLAYERS; s++) {
        if (net->slots[s].active && net->peer_addrs[s].has_addr) {
            host_send_roster_to_slot(net, s, level_idx, map_name, game_started);
            sent_any = 1;
        }
    }
    return sent_any;
#else
    (void)level_idx;
    (void)map_name;
    (void)game_started;
    return 0;
#endif
}

int net_send_lobby_state(NetContext *net, const NetCtrlPacket *pkt) {
    if (!net || !pkt || net->mode == NET_MODE_OFFLINE || !net->initialized) {
        return 0;
    }

    if (net->mode == NET_MODE_HOST) {
        return net_broadcast_lobby_roster(net, pkt->level_idx, pkt->map_name, pkt->game_started);
    }

    if (!net->has_peer_addr) {
        return 0;
    }

#ifdef _WIN32
    NetCtrlPacket out = *pkt;
    out.magic = NET_CTRL_MAGIC;
    out.msg_type = NET_MSG_LOBBY_STATE;
    out.slot_id = net->local_slot;
    out.client_id = net->local_client_id;

    struct sockaddr_in target_addr;
    memset(&target_addr, 0, sizeof(target_addr));
    target_addr.sin_family = AF_INET;
    target_addr.sin_addr.s_addr = (ULONG)net->peer_ip;
    target_addr.sin_port = (USHORT)net->peer_port;

    SOCKET s = (SOCKET)net->sock_handle;
    int sent = sendto(s, (const char *)&out, (int)sizeof(out), 0,
                      (const struct sockaddr *)&target_addr, (int)sizeof(target_addr));
    return (sent == (int)sizeof(out)) ? 1 : 0;
#else
    return 0;
#endif
}

int net_send_lobby_leave(NetContext *net) {
    if (!net || net->mode == NET_MODE_OFFLINE || !net->initialized) {
        return 0;
    }

#ifdef _WIN32
    NetCtrlPacket out;
    memset(&out, 0, sizeof(out));
    out.magic = NET_CTRL_MAGIC;
    out.msg_type = NET_MSG_LOBBY_LEAVE;
    out.slot_id = net->local_slot;
    out.client_id = net->local_client_id;
    out.host_session_id = net->host_session_id;

    SOCKET s = (SOCKET)net->sock_handle;

    if (net->mode == NET_MODE_HOST) {
        int sent_any = 0;
        for (int p = 1; p < NET_MAX_PLAYERS; p++) {
            if (!net->slots[p].active || !net->peer_addrs[p].has_addr) {
                continue;
            }
            struct sockaddr_in addr;
            memset(&addr, 0, sizeof(addr));
            addr.sin_family = AF_INET;
            addr.sin_addr.s_addr = (ULONG)net->peer_addrs[p].ip;
            addr.sin_port = (USHORT)net->peer_addrs[p].port;
            for (int k = 0; k < 3; k++) {
                if (sendto(s, (const char *)&out, (int)sizeof(out), 0,
                           (const struct sockaddr *)&addr, (int)sizeof(addr)) == (int)sizeof(out)) {
                    sent_any = 1;
                }
            }
        }
        return sent_any;
    }

    if (!net->has_peer_addr) {
        return 0;
    }

    struct sockaddr_in target_addr;
    memset(&target_addr, 0, sizeof(target_addr));
    target_addr.sin_family = AF_INET;
    target_addr.sin_addr.s_addr = (ULONG)net->peer_ip;
    target_addr.sin_port = (USHORT)net->peer_port;

    int sent_ok = 0;
    for (int k = 0; k < 3; k++) {
        if (sendto(s, (const char *)&out, (int)sizeof(out), 0,
                   (const struct sockaddr *)&target_addr, (int)sizeof(target_addr)) == (int)sizeof(out)) {
            sent_ok = 1;
        }
    }
    return sent_ok;
#else
    return 0;
#endif
}

/* ============================================================================
 * LOBBY MULTIPLAYER (ATE 4 JOGADORES) E DESCOBERTA AUTOMATICA NA LAN
 * ============================================================================ */

int net_poll_lobby(NetContext *net, const NetCtrlPacket *host_offer, int *peer_left_out) {
    if (peer_left_out) {
        *peer_left_out = 0;
    }
    if (!net || net->mode == NET_MODE_OFFLINE || !net->initialized) {
        return 0;
    }

#ifdef _WIN32
    int received_any = 0;
    SOCKET s = (SOCKET)net->sock_handle;
    double now = net_monotonic_sec();

    for (int iter = 0; iter < 64; iter++) {
        union {
            NetPacket state;
            NetCtrlPacket ctrl;
            NetLobbyRosterPacket roster;
            NetMatchSnapshotPacket snap;
            NetRestoreStatePacket restore;
            char raw[2048];
        } rx;
        struct sockaddr_in sender;
        int sender_len = (int)sizeof(sender);

        int bytes = recvfrom(s, rx.raw, (int)sizeof(rx.raw), 0,
                             (struct sockaddr *)&sender, &sender_len);
        if (bytes < 0) {
            if (net_should_continue_recv_err()) {
                continue;
            }
            break;
        }

        unsigned int sip = (unsigned int)sender.sin_addr.s_addr;
        unsigned short sport = (unsigned short)sender.sin_port;

        if (bytes == (int)sizeof(NetLobbyRosterPacket) && rx.roster.magic == NET_ROSTER_MAGIC) {
            if (net->mode == NET_MODE_CLIENT) {
                net->connected = 1;
                net->local_slot = rx.roster.your_slot;
                net->remote_level = rx.roster.level_idx;
                net->game_mode = rx.roster.game_mode;
                net->active_player_count = rx.roster.active_count;
                net->last_host_rx = now;
                if (rx.roster.game_started) {
                    net->remote_started = 1;
                }
                snprintf(net->remote_map_name, sizeof(net->remote_map_name), "%s", rx.roster.map_name);
                for (int i = 0; i < NET_MAX_PLAYERS; i++) {
                    net->slots[i] = rx.roster.slots[i];
                }
                net->remote_character = net->slots[0].character_idx;
                net->remote_ready = net->slots[0].ready;
                snprintf(net->remote_name, sizeof(net->remote_name), "%s", net->slots[0].name);
                received_any = 1;
            }
        } else if (bytes == (int)sizeof(NetMatchSnapshotPacket) && rx.snap.magic == NET_SNAP_MAGIC) {
            if (net->mode == NET_MODE_CLIENT) {
                net->connected = 1;
                if (rx.snap.your_slot >= 1 && rx.snap.your_slot < NET_MAX_PLAYERS) {
                    net->local_slot = rx.snap.your_slot;
                }
                net->remote_level = rx.snap.level_idx;
                net->game_mode = rx.snap.game_mode;
                net->active_player_count = rx.snap.active_count;
                net->last_host_rx = now;
                net->remote_started = 1;
                for (int i = 0; i < NET_MAX_PLAYERS; i++) {
                    net->slots[i] = rx.snap.slots[i];
                    net->total_dmg_to_slot[i] = rx.snap.total_dmg_to_slot[i];
                    net->last_attacker_of_slot[i] = rx.snap.last_attacker_of_slot[i];
                    net->slot_pvp_kills[i] = rx.snap.slot_pvp_kills[i];
                }
                net->remote_state.player_id = net->slots[0].player_id;
                net->remote_state.x = net->slots[0].x;
                net->remote_state.y = net->slots[0].y;
                net->remote_state.angle = net->slots[0].angle;
                net->remote_state.health = net->slots[0].health;
                net->remote_state.shooting = net->slots[0].shooting;
                received_any = 1;
            }
        } else if (bytes == (int)sizeof(NetRestoreStatePacket) && rx.restore.magic == NET_RESTORE_MAGIC) {
            if (net->mode == NET_MODE_CLIENT) {
                net->connected = 1;
                net->host_disconnected = 0;
                if (rx.restore.slot >= 1 && rx.restore.slot < NET_MAX_PLAYERS) {
                    net->local_slot = rx.restore.slot;
                }
                net->remote_level = rx.restore.level_idx;
                net->game_mode = rx.restore.game_mode;
                net->host_session_id = rx.restore.host_session_id;
                net->remote_started = 1;
                net->last_host_rx = now;
                if (rx.restore.has_saved_state && rx.restore.saved.valid) {
                    net->has_restore_state = 1;
                    net->restore_state = rx.restore.saved;
                }
                received_any = 1;
            }
        } else if (bytes == (int)sizeof(NetPacket)) {
            net->remote_state = rx.state;
            net->peer_ip = sip;
            net->peer_port = sport;
            net->has_peer_addr = 1;
            net->connected = 1;
            net->remote_started = 1;
            received_any = 1;
        } else if (bytes == (int)sizeof(NetCtrlPacket) && rx.ctrl.magic == NET_CTRL_MAGIC) {
            if (rx.ctrl.msg_type == NET_MSG_DISCOVER_REQ) {
                if (net->mode == NET_MODE_HOST && host_offer) {
                    NetCtrlPacket reply = *host_offer;
                    reply.magic = NET_CTRL_MAGIC;
                    reply.msg_type = NET_MSG_DISCOVER_OFFER;
                    reply.port = net->port;
                    reply.game_mode = net->game_mode;
                    reply.player_count = net->active_player_count;
                    reply.host_session_id = net->host_session_id;
                    reply.in_game = net->in_game;
                    sendto(s, (const char *)&reply, (int)sizeof(reply), 0,
                           (const struct sockaddr *)&sender, sender_len);
                }
            } else if (rx.ctrl.msg_type == NET_MSG_LOBBY_STATE) {
                if (net->mode == NET_MODE_HOST) {
                    net_clear_left_guard(net, sip, sport);
                    int cs = net_find_or_alloc_client_slot(net, sip, sport, now);
                    if (cs >= 1 && cs < NET_MAX_PLAYERS) {
                        NetPlayerSlot *slot = &net->slots[cs];
                        slot->active = 1;
                        slot->player_id = cs + 1;
                        slot->client_id = rx.ctrl.client_id;
                        slot->character_idx = rx.ctrl.character_idx;
                        slot->visual_id = rx.ctrl.visual_id;
                        slot->appearance = rx.ctrl.appearance;
                        slot->ready = rx.ctrl.ready;
                        snprintf(slot->name, sizeof(slot->name), "%s", rx.ctrl.player_name);
                        net_recompute_host_summary(net);
                        /* Responde imediatamente ao cliente com sua atribuicao de slot + roster */
                        host_send_roster_to_slot(net, cs,
                                                 host_offer ? host_offer->level_idx : net->remote_level,
                                                 host_offer ? host_offer->map_name : net->remote_map_name,
                                                 0);
                        received_any = 1;
                    }
                } else {
                    net->peer_ip = sip;
                    net->peer_port = sport;
                    net->has_peer_addr = 1;
                    net->connected = 1;
                    net->remote_character = rx.ctrl.character_idx;
                    net->remote_ready = rx.ctrl.ready;
                    net->remote_level = rx.ctrl.level_idx;
                    net->game_mode = rx.ctrl.game_mode;
                    if (rx.ctrl.game_started) {
                        net->remote_started = 1;
                    }
                    snprintf(net->remote_name, sizeof(net->remote_name), "%s", rx.ctrl.player_name);
                    snprintf(net->remote_map_name, sizeof(net->remote_map_name), "%s", rx.ctrl.map_name);
                    received_any = 1;
                }
            } else if (rx.ctrl.msg_type == NET_MSG_LOBBY_LEAVE) {
                if (net->mode == NET_MODE_HOST) {
                    int cs = net_find_client_slot_by_addr(net, sip, sport);
                    net_mark_peer_left(net, sip, sport, now);
                    if (cs >= 1) {
                        net_clear_slot(net, cs);
                        net_broadcast_lobby_roster(net,
                                                   host_offer ? host_offer->level_idx : net->remote_level,
                                                   host_offer ? host_offer->map_name : net->remote_map_name,
                                                   0);
                        if (peer_left_out) {
                            *peer_left_out = 1;
                        }
                        received_any = 1;
                    }
                } else {
                    net->connected = 0;
                    net->remote_ready = 0;
                    net->remote_started = 0;
                    net->local_slot = -1;
                    if (peer_left_out) {
                        *peer_left_out = 1;
                    }
                    received_any = 1;
                }
            }
        }
    }

    return received_any;
#else
    (void)host_offer;
    return 0;
#endif
}

/* Abre socket UDP nao-bloqueante habilitado com SO_BROADCAST para varredura LAN. */
int net_discovery_open(unsigned long long *out_sock) {
    if (!out_sock) {
        return 0;
    }
    *out_sock = 0;

#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        return 0;
    }

    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) {
        WSACleanup();
        return 0;
    }

    u_long non_blocking = 1;
    BOOL bcast = TRUE;
    if (ioctlsocket(s, FIONBIO, &non_blocking) != 0 ||
        setsockopt(s, SOL_SOCKET, SO_BROADCAST, (const char *)&bcast, sizeof(bcast)) != 0) {
        closesocket(s);
        WSACleanup();
        return 0;
    }

    *out_sock = (unsigned long long)s;
    return 1;
#else
    return 0;
#endif
}

static void send_probe_to_ip(unsigned long long sock_handle, const char *ip_str, int port) {
#ifdef _WIN32
    SOCKET s = (SOCKET)sock_handle;
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((u_short)port);
    if (inet_pton(AF_INET, ip_str, &addr.sin_addr) != 1) {
        return;
    }

    NetCtrlPacket req;
    memset(&req, 0, sizeof(req));
    req.magic = NET_CTRL_MAGIC;
    req.msg_type = NET_MSG_DISCOVER_REQ;
    req.port = port;
    sendto(s, (const char *)&req, (int)sizeof(req), 0,
           (const struct sockaddr *)&addr, (int)sizeof(addr));
#else
    (void)sock_handle;
    (void)ip_str;
    (void)port;
#endif
}

/* Envia sondas UDP DISCOVER_REQ para loopback (127.0.0.1), broadcast global
 * (255.255.255.255) e broadcast da sub-rede local (ex.: 192.168.X.255). */
void net_discovery_send_probe(unsigned long long sock_handle, int port) {
    if (sock_handle == 0 || port <= 0) {
        return;
    }
#ifdef _WIN32
    send_probe_to_ip(sock_handle, "127.0.0.1", port);
    send_probe_to_ip(sock_handle, "255.255.255.255", port);

    char local_ip[64] = "";
    net_get_local_ipv4(local_ip, (int)sizeof(local_ip));
    if (strncmp(local_ip, "127.", 4) != 0) {
        char *last_dot = strrchr(local_ip, '.');
        if (last_dot) {
            snprintf(last_dot, (size_t)(local_ip + sizeof(local_ip) - last_dot), ".255");
            send_probe_to_ip(sock_handle, local_ip, port);
        }
    }
#else
    (void)sock_handle;
    (void)port;
#endif
}

/* Prioriza rotas de rede ao deduplicar um mesmo Host com multiplas interfaces
 * (127.0.0.1 > LAN privada 192.168/10/172.16-31 > VPN/Radmin ex.: 26.x.x.x). */
static int ip_route_priority(const char *ip) {
    if (!ip) return 0;
    if (strcmp(ip, "127.0.0.1") == 0) return 3;
    if (strncmp(ip, "192.168.", 8) == 0 || strncmp(ip, "10.", 3) == 0) return 2;
    if (strncmp(ip, "172.", 4) == 0) {
        int second = atoi(ip + 4);
        if (second >= 16 && second <= 31) return 2;
    }
    return 1;
}

/* Coleta respostas DISCOVER_OFFER na LAN, deduplica hosts por sessao e expira apos 3s. */
int net_discovery_poll(unsigned long long sock_handle, NetDiscoveredRoom *rooms,
                       int *room_count, int max_rooms, double now) {
    if (sock_handle == 0 || !rooms || !room_count || max_rooms <= 0) {
        return 0;
    }

#ifdef _WIN32
    int added_or_updated = 0;
    SOCKET s = (SOCKET)sock_handle;

    for (int iter = 0; iter < 64; iter++) {
        char raw[2048];
        struct sockaddr_in sender;
        int sender_len = (int)sizeof(sender);
        int bytes = recvfrom(s, raw, (int)sizeof(raw), 0,
                             (struct sockaddr *)&sender, &sender_len);
        if (bytes < 0) {
            if (net_should_continue_recv_err()) {
                continue;
            }
            break;
        }
        if (bytes != (int)sizeof(NetCtrlPacket)) {
            continue;
        }

        NetCtrlPacket pkt;
        memcpy(&pkt, raw, sizeof(pkt));
        if (pkt.magic != NET_CTRL_MAGIC || pkt.msg_type != NET_MSG_DISCOVER_OFFER) {
            continue;
        }

        char ip_str[64] = "127.0.0.1";
        if (inet_ntop(AF_INET, &sender.sin_addr, ip_str, (socklen_t)sizeof(ip_str)) == NULL) {
            continue;
        }

        int idx = -1;
        int is_new = 0;
        for (int i = 0; i < *room_count; i++) {
            if (pkt.host_session_id != 0u && rooms[i].host_session_id == pkt.host_session_id) {
                idx = i;
                break;
            }
            if (pkt.host_session_id == 0u &&
                strcmp(rooms[i].ip, ip_str) == 0 && rooms[i].port == pkt.port) {
                idx = i;
                break;
            }
        }
        if (idx < 0 && *room_count < max_rooms) {
            idx = (*room_count)++;
            is_new = 1;
        }
        if (idx >= 0) {
            if (is_new ||
                ip_route_priority(ip_str) >= ip_route_priority(rooms[idx].ip) ||
                (now - rooms[idx].last_seen) > 1.5) {
                snprintf(rooms[idx].ip, sizeof(rooms[idx].ip), "%s", ip_str);
            }
            snprintf(rooms[idx].host_name, sizeof(rooms[idx].host_name), "%s", pkt.player_name);
            snprintf(rooms[idx].map_name, sizeof(rooms[idx].map_name), "%s", pkt.map_name);
            rooms[idx].level_idx = pkt.level_idx;
            rooms[idx].port = pkt.port;
            rooms[idx].game_mode = pkt.game_mode;
            rooms[idx].player_count = (pkt.player_count >= 1 && pkt.player_count <= NET_MAX_PLAYERS)
                ? pkt.player_count : 1;
            rooms[idx].host_session_id = pkt.host_session_id;
            rooms[idx].in_game = pkt.in_game;
            rooms[idx].last_seen = now;
            added_or_updated = 1;
        }
    }

    int write_i = 0;
    for (int i = 0; i < *room_count; i++) {
        if ((now - rooms[i].last_seen) <= 3.0) {
            if (write_i != i) {
                rooms[write_i] = rooms[i];
            }
            write_i++;
        }
    }
    *room_count = write_i;
    return added_or_updated;
#else
    (void)now;
    return 0;
#endif
}

void net_discovery_close(unsigned long long *sock_handle) {
    if (!sock_handle || *sock_handle == 0) {
        return;
    }
#ifdef _WIN32
    closesocket((SOCKET)(*sock_handle));
    WSACleanup();
#endif
    *sock_handle = 0;
}

void net_close(NetContext *net) {
    if (!net) {
        return;
    }

#ifdef _WIN32
    if (net->initialized) {
        SOCKET s = (SOCKET)net->sock_handle;
        closesocket(s);
        WSACleanup();
    }
#endif

    net->sock_handle = 0;
    net->peer_ip = 0;
    net->peer_port = 0;
    net->initialized = 0;
    net->connected = 0;
    net->has_peer_addr = 0;
}
