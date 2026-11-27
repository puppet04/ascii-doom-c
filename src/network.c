#include "network.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <winhttp.h>
#endif

/*
 * network.c - Multiplayer LAN (UDP nao-bloqueante) e Online (WebSocket relay)
 *
 * - <winsock2.h>/<windows.h>/<winhttp.h> ficam isolados aqui para nao dar
 *   conflito de nomes da Win32 API com a biblioteca grafica Raylib.
 * - Rede em estrela com 1 Host (slot 0) e ate 3 Clientes (slots 1..3).
 *   Na LAN, clientes sao identificados por (IP, porta), o que tambem permite
 *   abrir varias janelas na mesma maquina em 127.0.0.1.
 * - Dano PvP e kills usam contadores cumulativos por par (atacante -> vitima)
 *   para nao perder acertos se algum pacote UDP atrasar ou sumir.
 * - A API antiga 1v1 (net_send_state / net_receive_state) segue intacta para
 *   manter os testes unitarios funcionando.
 */

/* Relogio de alta resolucao (em segundos) que nao sofre saltos se o horario
 * do Windows mudar; usado para timeouts e guarda de reconexao. */
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

/* Mistura PID, tick e contador de alta resolucao para gerar um ID unico
 * por processo/chamada sem depender de rand(). */
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

/* Retorna um identificador estavel para esta instancia do jogo (cacheado na 1a vez)
 * usado pelo host para reconhecer o jogador se ele reconectar no meio da partida. */
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

/* --- Transporte online via WebSocket relay (WinHTTP nativo do Windows) --- */

#define RELAY_QUEUE_CAP  64
#define RELAY_MAX_FRAME  2048

/* Item das filas circulares RX/TX entre as threads do WinHTTP e o loop do jogo. */
typedef struct {
    unsigned int peer_id;
    int len;
    unsigned char data[RELAY_MAX_FRAME];
} RelayQueuedPkt;

/* Estado interno da conexao WebSocket com o servidor relay (protegido por cs). */
typedef struct {
    NetMode mode;
    char url[160];
    char room_code[16];
    char error_msg[128];
    volatile LONG status;
    volatile LONG stop_flag;

    CRITICAL_SECTION cs;
    HANDLE tx_event;      /* acorda a thread de envio assim que entra pacote em tx_q */
    HANDLE worker_thread; /* conecta e faz loop bloqueante de leitura no WebSocket */
    HANDLE tx_thread;     /* drena tx_q enviando frames binarios pelo WebSocket */

    HINTERNET h_session;
    HINTERNET h_connect;
    HINTERNET h_ws;

    RelayQueuedPkt rx_q[RELAY_QUEUE_CAP];
    int rx_head;
    int rx_tail;
    int rx_count;

    RelayQueuedPkt tx_q[RELAY_QUEUE_CAP];
    int tx_head;
    int tx_tail;
    int tx_count;
} NetRelayState;

/* Mapeia peer_id (24 bits) do relay para IP 10.x.x.x e porta ficticios,
 * permitindo reutilizar a mesma logica de slots baseada em (ip, porta) da LAN. */
static unsigned int relay_peer_to_ip(unsigned int peer_id) {
    return htonl(0x0A000000u | (peer_id & 0x00FFFFFFu));
}

static unsigned short relay_peer_to_port(unsigned int peer_id) {
    return htons((u_short)(40400u + (peer_id & 0x0FFFu)));
}

static unsigned int relay_ip_to_peer(unsigned int ip) {
    if (ip == 0u) return 0u;
    return ntohl(ip) & 0x00FFFFFFu;
}

/* Empilha pacote recebido pela thread WebSocket na fila circular RX. */
static int relay_enqueue_rx(NetRelayState *st, unsigned int peer_id, const void *data, int len) {
    if (!st || !data || len <= 0 || len > RELAY_MAX_FRAME) {
        return 0;
    }
    EnterCriticalSection(&st->cs);
    if (st->rx_count >= RELAY_QUEUE_CAP) {
        /* Descarta o pacote mais antigo para priorizar estado mais recente */
        st->rx_head = (st->rx_head + 1) % RELAY_QUEUE_CAP;
        st->rx_count--;
    }
    RelayQueuedPkt *dst = &st->rx_q[st->rx_tail];
    dst->peer_id = peer_id;
    dst->len = len;
    memcpy(dst->data, data, (size_t)len);
    st->rx_tail = (st->rx_tail + 1) % RELAY_QUEUE_CAP;
    st->rx_count++;
    LeaveCriticalSection(&st->cs);
    return 1;
}

/* Retira o proximo pacote da fila RX (nao-bloqueante para o loop principal do jogo). */
static int relay_dequeue_rx(NetRelayState *st, void *buf, int max_len,
                            unsigned int *out_ip, unsigned short *out_port) {
    if (!st || !buf || max_len <= 0) {
        return -1;
    }
    int len = -1;
    EnterCriticalSection(&st->cs);
    if (st->rx_count > 0) {
        RelayQueuedPkt *src = &st->rx_q[st->rx_head];
        len = (src->len <= max_len) ? src->len : max_len;
        memcpy(buf, src->data, (size_t)len);
        if (out_ip) *out_ip = relay_peer_to_ip(src->peer_id);
        if (out_port) *out_port = relay_peer_to_port(src->peer_id);
        st->rx_head = (st->rx_head + 1) % RELAY_QUEUE_CAP;
        st->rx_count--;
    }
    LeaveCriticalSection(&st->cs);
    return len;
}

/* Coloca pacote na fila de envio do relay e acorda a thread TX via evento. */
static int relay_enqueue_tx(NetRelayState *st, unsigned int target_peer, const void *data, int len) {
    if (!st || !data || len <= 0 || len > RELAY_MAX_FRAME) {
        return 0;
    }
    if (InterlockedCompareExchange(&st->status, 0, 0) != NET_RELAY_READY) {
        return 0;
    }
    EnterCriticalSection(&st->cs);
    if (st->tx_count >= RELAY_QUEUE_CAP) {
        st->tx_head = (st->tx_head + 1) % RELAY_QUEUE_CAP;
        st->tx_count--;
    }
    RelayQueuedPkt *dst = &st->tx_q[st->tx_tail];
    dst->peer_id = target_peer;
    dst->len = len;
    memcpy(dst->data, data, (size_t)len);
    st->tx_tail = (st->tx_tail + 1) % RELAY_QUEUE_CAP;
    st->tx_count++;
    LeaveCriticalSection(&st->cs);
    if (st->tx_event) {
        SetEvent(st->tx_event);
    }
    return len;
}

/* Quebra a URL do servidor relay em host, porta, prefixo de rota e flag TLS. */
static int relay_parse_url(const char *url, char *out_host, int host_sz,
                           int *out_port, char *out_prefix, int prefix_sz, int *out_tls) {
    if (!url || !out_host || !out_port || !out_prefix || !out_tls) {
        return 0;
    }
    const char *p = url;
    *out_tls = 1;
    if (strncmp(p, "wss://", 6) == 0) {
        *out_tls = 1;
        p += 6;
    } else if (strncmp(p, "https://", 8) == 0) {
        *out_tls = 1;
        p += 8;
    } else if (strncmp(p, "ws://", 5) == 0) {
        *out_tls = 0;
        p += 5;
    } else if (strncmp(p, "http://", 7) == 0) {
        *out_tls = 0;
        p += 7;
    }
    if (*p == '\0') {
        return 0;
    }
    const char *slash = strchr(p, '/');
    size_t hostport_len = slash ? (size_t)(slash - p) : strlen(p);
    if (hostport_len == 0 || hostport_len >= (size_t)host_sz) {
        return 0;
    }
    char hostport[160];
    memcpy(hostport, p, hostport_len);
    hostport[hostport_len] = '\0';

    char *colon = strchr(hostport, ':');
    if (colon) {
        *colon = '\0';
        *out_port = atoi(colon + 1);
    } else {
        *out_port = *out_tls ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT;
    }
    if (hostport[0] == '\0' || *out_port <= 0 || *out_port > 65535) {
        return 0;
    }
    snprintf(out_host, (size_t)host_sz, "%s", hostport);

    if (slash && slash[0] != '\0') {
        snprintf(out_prefix, (size_t)prefix_sz, "%s", slash);
        size_t plen = strlen(out_prefix);
        while (plen > 0 && out_prefix[plen - 1] == '/') {
            out_prefix[--plen] = '\0';
        }
    } else {
        out_prefix[0] = '\0';
    }
    return 1;
}

/* Atualiza o status do relay e guarda uma mensagem legivel para a interface. */
static void relay_set_error(NetRelayState *st, NetRelayStatus status, const char *msg) {
    if (!st) return;
    EnterCriticalSection(&st->cs);
    if (msg) {
        snprintf(st->error_msg, sizeof(st->error_msg), "%s", msg);
    }
    LeaveCriticalSection(&st->cs);
    InterlockedExchange(&st->status, (LONG)status);
}

/* Thread dedicada a esvaziar tx_q enviando frames binarios pelo WebSocket.
 * Cada frame leva 4 bytes little-endian com o peer_id de destino antes dos dados. */
static DWORD WINAPI relay_tx_thread_proc(LPVOID param) {
    NetRelayState *st = (NetRelayState *)param;
    unsigned char wire_buf[4 + RELAY_MAX_FRAME];

    while (InterlockedCompareExchange(&st->stop_flag, 0, 0) == 0) {
        WaitForSingleObject(st->tx_event, 200);
        for (;;) {
            unsigned int peer_id = 0;
            int plen = 0;
            EnterCriticalSection(&st->cs);
            if (st->tx_count > 0) {
                RelayQueuedPkt *src = &st->tx_q[st->tx_head];
                peer_id = src->peer_id;
                plen = src->len;
                memcpy(wire_buf + 4, src->data, (size_t)plen);
                st->tx_head = (st->tx_head + 1) % RELAY_QUEUE_CAP;
                st->tx_count--;
            }
            LeaveCriticalSection(&st->cs);

            if (plen <= 0) {
                break;
            }
            wire_buf[0] = (unsigned char)(peer_id & 0xFFu);
            wire_buf[1] = (unsigned char)((peer_id >> 8) & 0xFFu);
            wire_buf[2] = (unsigned char)((peer_id >> 16) & 0xFFu);
            wire_buf[3] = (unsigned char)((peer_id >> 24) & 0xFFu);

            if (!st->h_ws || InterlockedCompareExchange(&st->stop_flag, 0, 0) != 0) {
                break;
            }
            DWORD err = WinHttpWebSocketSend(st->h_ws,
                                             WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE,
                                             wire_buf, (DWORD)(4 + plen));
            if (err != NO_ERROR) {
                relay_set_error(st, NET_RELAY_CLOSED, "Conexao online encerrada.");
                return 0;
            }
        }
    }
    return 0;
}

/* Thread principal do relay: abre sessao WinHTTP, faz handshake HTTP -> WebSocket,
 * sobe a thread de envio e fica lendo mensagens binarias ate desconectar. */
static DWORD WINAPI relay_worker_thread_proc(LPVOID param) {
    NetRelayState *st = (NetRelayState *)param;
    char host[160] = "";
    char prefix[128] = "";
    int port = 443;
    int use_tls = 1;

    if (!relay_parse_url(st->url, host, (int)sizeof(host), &port, prefix, (int)sizeof(prefix), &use_tls)) {
        relay_set_error(st, NET_RELAY_FAILED, "Endereco do servidor online invalido.");
        return 0;
    }

    char full_path[256];
    snprintf(full_path, sizeof(full_path), "%s/ws?room=%s&role=%s",
             prefix, st->room_code, (st->mode == NET_MODE_HOST) ? "host" : "client");

    WCHAR whost[160];
    WCHAR wpath[256];
    if (MultiByteToWideChar(CP_UTF8, 0, host, -1, whost, 160) == 0 ||
        MultiByteToWideChar(CP_UTF8, 0, full_path, -1, wpath, 256) == 0) {
        relay_set_error(st, NET_RELAY_FAILED, "Erro ao preparar endereco do servidor.");
        return 0;
    }

    /* Tenta automatic proxy primeiro e cai para default proxy em versoes antigas do Windows */
    HINTERNET hSession = WinHttpOpen(L"SECTOR404/1.0",
                                     WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                     WINHTTP_NO_PROXY_NAME,
                                     WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) {
        hSession = WinHttpOpen(L"SECTOR404/1.0",
                               WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                               WINHTTP_NO_PROXY_NAME,
                               WINHTTP_NO_PROXY_BYPASS, 0);
    }
    if (!hSession) {
        relay_set_error(st, NET_RELAY_FAILED, "Falha ao iniciar WinHTTP do Windows.");
        return 0;
    }
    st->h_session = hSession;

    /* Timeout generoso para suportar eventual cold start (~25s) no Render gratuito */
    WinHttpSetTimeouts(hSession, 15000, 15000, 15000, 45000);

    HINTERNET hConnect = WinHttpConnect(hSession, whost, (INTERNET_PORT)port, 0);
    if (!hConnect) {
        relay_set_error(st, NET_RELAY_FAILED, "Nao foi possivel conectar ao servidor online.");
        return 0;
    }
    st->h_connect = hConnect;

    DWORD req_flags = use_tls ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", wpath, NULL,
                                            WINHTTP_NO_REFERER,
                                            WINHTTP_DEFAULT_ACCEPT_TYPES,
                                            req_flags);
    if (!hRequest) {
        relay_set_error(st, NET_RELAY_FAILED, "Falha ao abrir requisicao WebSocket.");
        return 0;
    }

    if (!WinHttpSetOption(hRequest, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, NULL, 0) ||
        !WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, NULL, 0, 0, 0) ||
        !WinHttpReceiveResponse(hRequest, NULL)) {
        WinHttpCloseHandle(hRequest);
        relay_set_error(st, NET_RELAY_FAILED, "Servidor online inacessivel ou sem internet.");
        return 0;
    }

    DWORD status_code = 0;
    DWORD sc_size = sizeof(status_code);
    WinHttpQueryHeaders(hRequest,
                        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX,
                        &status_code, &sc_size, WINHTTP_NO_HEADER_INDEX);

    /* WebSocket exige HTTP 101 Switching Protocols; 404/409 indicam sala inexistente ou cheia */
    if (status_code != 101) {
        WinHttpCloseHandle(hRequest);
        if (status_code == 404) {
            relay_set_error(st, NET_RELAY_FAILED, "Sala nao encontrada (verifique o codigo).");
        } else if (status_code == 409) {
            relay_set_error(st, NET_RELAY_FAILED, "Sala online ja esta lotada (4/4).");
        } else {
            char msg[128];
            snprintf(msg, sizeof(msg), "Servidor respondeu HTTP %lu.", (unsigned long)status_code);
            relay_set_error(st, NET_RELAY_FAILED, msg);
        }
        return 0;
    }

    HINTERNET hWs = WinHttpWebSocketCompleteUpgrade(hRequest, 0);
    WinHttpCloseHandle(hRequest);
    if (!hWs) {
        relay_set_error(st, NET_RELAY_FAILED, "Falha ao concluir tunel WebSocket.");
        return 0;
    }
    st->h_ws = hWs;

    /* Volta ao timeout de recebimento infinito para idle normal no WebSocket */
    DWORD inf_timeout = 0xFFFFFFFFu;
    WinHttpSetOption(hWs, WINHTTP_OPTION_RECEIVE_TIMEOUT, &inf_timeout, sizeof(inf_timeout));

    InterlockedExchange(&st->status, (LONG)NET_RELAY_READY);
    st->tx_thread = CreateThread(NULL, 0, relay_tx_thread_proc, st, 0, NULL);

    /* Loop de leitura RX: junta fragmentos se precisar e extrai peer_id (4 bytes) + payload */
    unsigned char msg_buf[4 + RELAY_MAX_FRAME];
    int msg_len = 0;
    while (InterlockedCompareExchange(&st->stop_flag, 0, 0) == 0) {
        DWORD bytes_read = 0;
        WINHTTP_WEB_SOCKET_BUFFER_TYPE buf_type = WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE;
        DWORD space = (DWORD)(sizeof(msg_buf) - (size_t)msg_len);
        if (space == 0) {
            msg_len = 0;
            space = (DWORD)sizeof(msg_buf);
        }

        DWORD err = WinHttpWebSocketReceive(hWs, msg_buf + msg_len, space, &bytes_read, &buf_type);
        if (err != NO_ERROR || buf_type == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE) {
            break;
        }
        msg_len += (int)bytes_read;

        if (buf_type == WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE) {
            if (msg_len >= 5) {
                unsigned int peer_id = ((unsigned int)msg_buf[0]) |
                                       ((unsigned int)msg_buf[1] << 8) |
                                       ((unsigned int)msg_buf[2] << 16) |
                                       ((unsigned int)msg_buf[3] << 24);
                relay_enqueue_rx(st, peer_id, msg_buf + 4, msg_len - 4);
            }
            msg_len = 0;
        } else if (buf_type == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE) {
            msg_len = 0;
        }
    }

    /* Se caiu sem stop_flag e somos cliente, injeta um LOBBY_LEAVE local para avisar o jogo */
    if (InterlockedCompareExchange(&st->stop_flag, 0, 0) == 0) {
        if (st->mode == NET_MODE_CLIENT) {
            NetCtrlPacket leave_pkt;
            memset(&leave_pkt, 0, sizeof(leave_pkt));
            leave_pkt.magic = NET_CTRL_MAGIC;
            leave_pkt.msg_type = NET_MSG_LOBBY_LEAVE;
            relay_enqueue_rx(st, 0u, &leave_pkt, (int)sizeof(leave_pkt));
        }
        relay_set_error(st, NET_RELAY_CLOSED, "Anfitriao encerrou a sala online.");
    }
    return 0;
}

/* Camada unica de envio: manda pela fila do WebSocket relay ou via sendto UDP. */
static int net_transport_send(NetContext *net, unsigned int ip, unsigned short port,
                              const void *data, int len) {
    if (!net || !net->initialized || !data || len <= 0) {
        return 0;
    }
    if (net->transport == NET_TRANSPORT_RELAY) {
        NetRelayState *st = (NetRelayState *)(uintptr_t)net->relay_handle;
        if (!st) return 0;
        /* Cliente sempre envia para peer 0 (host); host extrai peer_id do IP ficticio */
        unsigned int target_peer = (net->mode == NET_MODE_HOST) ? relay_ip_to_peer(ip) : 0u;
        return (relay_enqueue_tx(st, target_peer, data, len) == len) ? 1 : 0;
    }

    SOCKET s = (SOCKET)net->sock_handle;
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = (ULONG)ip;
    addr.sin_port = (USHORT)port;
    int sent = sendto(s, (const char *)data, len, 0,
                      (const struct sockaddr *)&addr, (int)sizeof(addr));
    return (sent == len) ? 1 : 0;
}

/* Camada unica de leitura nao-bloqueante: tira da fila RX do relay ou faz recvfrom UDP. */
static int net_transport_recv(NetContext *net, void *buf, int max_len,
                              unsigned int *out_ip, unsigned short *out_port) {
    if (!net || !net->initialized || !buf || max_len <= 0) {
        return -1;
    }
    if (net->transport == NET_TRANSPORT_RELAY) {
        NetRelayState *st = (NetRelayState *)(uintptr_t)net->relay_handle;
        return relay_dequeue_rx(st, buf, max_len, out_ip, out_port);
    }

    SOCKET s = (SOCKET)net->sock_handle;
    /* Tenta ate 8 vezes caso chegue algum WSAECONNRESET de porta fechada */
    for (int tries = 0; tries < 8; tries++) {
        struct sockaddr_in sender;
        int sender_len = (int)sizeof(sender);
        int bytes = recvfrom(s, (char *)buf, max_len, 0,
                             (struct sockaddr *)&sender, &sender_len);
        if (bytes < 0) {
            if (net_should_continue_recv_err()) {
                continue;
            }
            return -1;
        }
        if (out_ip) *out_ip = (unsigned int)sender.sin_addr.s_addr;
        if (out_port) *out_port = (unsigned short)sender.sin_port;
        return bytes;
    }
    return -1;
}
#endif

/* Recalcula quantos jogadores estao ativos e espelha o 1o cliente nos campos
 * antigos (peer_ip, remote_name, etc.) para manter compatibilidade com 1v1. */
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

/* Procura o slot (1..3) associado a um endereco (ip, porta) ja conhecido. */
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

/* Retorna o slot existente do peer (atualizando last_seen) ou aloca o proximo vago. */
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

/* Libera o slot de um cliente remoto que saiu ou deu timeout. */
static void net_clear_slot(NetContext *net, int s) {
    if (!net || s < 1 || s >= NET_MAX_PLAYERS) {
        return;
    }
    memset(&net->slots[s], 0, sizeof(net->slots[s]));
    memset(&net->peer_addrs[s], 0, sizeof(net->peer_addrs[s]));
    net_recompute_host_summary(net);
}

/* Guarda o endereco de quem acabou de sair por 0.8s para ignorar pacotes atrasados
 * que ainda estavam no buffer UDP e evitar recriar o slot sem querer. */
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

/* Remove o bloqueio temporario quando o cliente volta explicitamente pelo lobby/rejoin. */
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

/* Verifica se esse (ip, porta) pediu para sair ha menos de 0.8s. */
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

/* Procura o estado salvo de um jogador para restaurar se ele cair e voltar:
 * tenta primeiro por client_id e depois por (IP + nome) fora de 127.0.0.1. */
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

/* Salva ou atualiza o ultimo estado conhecido de um cliente (posicao, vida,
 * municao, pontos e contadores PvP) para eventual reconexao na partida. */
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

/* Aloca slot para cliente que reconectou, dando preferencia ao slot antigo dele. */
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
/* Envia a lista atual de jogadores do lobby para um slot especifico,
 * informando your_slot para o cliente saber quem ele e na tabela. */
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

    net_transport_send(net, net->peer_addrs[s].ip, net->peer_addrs[s].port, &pkt, (int)sizeof(pkt));
}
#endif

/* Consolida a matriz de dano PvP e kills sempre mantendo o maior valor cumulativo
 * por par (atacante -> vitima), assim pacotes atrasados ou fora de ordem nao zeram hits. */
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

    /* Soma quanto de dano cada vitima v ja recebeu de todos os outros jogadores */
    for (int v = 0; v < NET_MAX_PLAYERS; v++) {
        int sum_dmg = 0;
        for (int a = 0; a < NET_MAX_PLAYERS; a++) {
            if (a != v) {
                sum_dmg += net->remote_pvp_dmg_matrix[a][v];
            }
        }
        net->total_dmg_to_slot[v] = sum_dmg;
    }

    /* Soma quantas kills cada atacante a acumulou contra todas as vitimas */
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
/* Monta o snapshot global da partida (todos os slots + totais PvP) e envia
 * uma copia com your_slot preenchido para cada cliente ativo. */
static int host_broadcast_match_snapshot(NetContext *net) {
    if (!net || net->mode != NET_MODE_HOST || !net->initialized) {
        return 0;
    }
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
        if (net_transport_send(net, net->peer_addrs[peer].ip, net->peer_addrs[peer].port,
                               &snap, (int)sizeof(snap))) {
            sent_count++;
        }
    }
    return (sent_count > 0) ? 1 : 0;
}

/* Envia pacote de restauracao para quem reconectou com a partida em andamento.
 * No UDP manda 3 vezes para diminuir risco de perda; no relay basta 1. */
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

    int count = (net->transport == NET_TRANSPORT_RELAY) ? 1 : 3;
    for (int k = 0; k < count; k++) {
        net_transport_send(net, ip, port, &pkt, (int)sizeof(pkt));
    }
}
#endif

/* Marca a transicao do lobby para a partida e reseta os timers de timeout dos peers. */
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

    /* Coloca o socket em modo nao-bloqueante para o loop do jogo nunca travar esperando rede */
    u_long non_blocking = 1;
    if (ioctlsocket(s, FIONBIO, &non_blocking) != 0) {
        closesocket(s);
        WSACleanup();
        return 0;
    }

    net->sock_handle = (unsigned long long)s;
    net->initialized = 1;

    if (mode == NET_MODE_HOST) {
        /* Escuta em todas as interfaces locais (LAN, loopback e VPN) na porta configurada */
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
    if (net->mode == NET_MODE_HOST) {
        /* Host preenche o proprio slot 0 e ja envia o snapshot para todos os clientes */
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
        /* No PvP as kills vem da matriz global; no Co-op vem das kills de monstros */
        if (net->game_mode != NET_GAME_PVP) {
            self->kills = local_player->ability_state.kills;
        }

        return host_broadcast_match_snapshot(net);
    }

    if (!net->has_peer_addr) {
        return 0;
    }

    /* Cliente espelha seu estado no slot local antes de mandar o pacote ao host */
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

    return net_transport_send(net, net->peer_ip, net->peer_port, &pkt, (int)sizeof(pkt));
#else
    (void)local_player;
    (void)shooting;
    return 0;
#endif
}

/* Recebe e processa todos os pacotes da partida sem bloquear (ate 64 por frame).
 * Host atualiza tabela de slots + eventos PvP + expira clientes mudos (3s UDP / 6s relay).
 * Cliente aplica snapshots mantendo sua propria posicao e vida locais sem rollback. */
int net_receive_match_state(NetContext *net) {
    if (!net || net->mode == NET_MODE_OFFLINE || !net->initialized) {
        return 0;
    }

#ifdef _WIN32
    int received_any = 0;
    double now = net_monotonic_sec();

    for (int iter = 0; iter < 64; iter++) {
        union {
            NetPacket legacy;
            NetCtrlPacket ctrl;
            NetMatchInputPacket input;
            NetMatchSnapshotPacket snap;
            char raw[2048];
        } rx;
        unsigned int sip = 0;
        unsigned short sport = 0;

        int bytes = net_transport_recv(net, rx.raw, (int)sizeof(rx.raw), &sip, &sport);
        if (bytes < 0) {
            break;
        }

        if (bytes == (int)sizeof(NetMatchInputPacket) && rx.input.magic == NET_INPUT_MAGIC) {
            if (net->mode == NET_MODE_HOST) {
                /* Ignora pacotes atrasados de quem acabou de sair */
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

                    /* Atualiza matriz cumulativa de dano PvP e kills sempre pegando o maior valor */
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

                    /* Salva estado do cliente para permitir rejoin no mesmo ponto se ele cair */
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
                /* Preserva posicao/vida locais para nao dar puxao na tela com snapshot antigo */
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
                    /* Responde descoberta mesmo com a partida ja rodando (in_game = 1) */
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
                    net_transport_send(net, sip, sport, &reply, (int)sizeof(reply));
                } else if (rx.ctrl.msg_type == NET_MSG_LOBBY_STATE) {
                    int existing = net_find_client_slot_by_addr(net, sip, sport);
                    if (existing >= 1 && net->slots[existing].has_pos) {
                        /* Cliente ainda em transicao do lobby para a partida */
                        host_send_roster_to_slot(net, existing, net->remote_level, NULL, 1);
                    } else {
                        /* Novo jogador entrando no meio ou ex-jogador voltando: restaura se houver save */
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
                                /* Zera historico PvP do slot para o recem-chegado nao herdar dano velho */
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
                /* Host avisou que encerrou a partida */
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

    /* No relay online o timeout e maior (6s) para aguentar oscilacao da internet */
    double match_timeout = (net->transport == NET_TRANSPORT_RELAY) ? 6.0 : 3.0;
    if (net->mode == NET_MODE_HOST) {
        int pruned = 0;
        for (int s = 1; s < NET_MAX_PLAYERS; s++) {
            if (net->slots[s].active && (now - net->peer_addrs[s].last_seen) > match_timeout) {
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
        if ((now - net->last_host_rx) > match_timeout) {
            net->connected = 0;
            net->host_disconnected = 1;
        }
    }

    return received_any;
#else
    return 0;
#endif
}

/* Descobre o IPv4 local principal conectando um socket UDP para 8.8.8.8:53
 * (nao envia nada na rede, apenas faz o SO escolher a rota padrao). */
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
        /* Fallback se nao houver rota padrao para internet (ex.: LAN isolada): consulta hostname */
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

/* Prioriza rotas de rede ao listar/deduplicar um mesmo Host com multiplas interfaces
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

/* Lista todos os IPv4 ativos do PC (LAN + VPN tipo Radmin) separados por " | "
 * usando SIO_GET_INTERFACE_LIST e ignorando loopback e 169.254.x.x. */
void net_get_all_local_ipv4s(char *out_buf, int out_size) {
    if (!out_buf || out_size <= 0) {
        return;
    }
    net_get_local_ipv4(out_buf, out_size);

#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        return;
    }
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s != INVALID_SOCKET) {
        INTERFACE_INFO ifaces[16];
        DWORD bytes_ret = 0;
        if (WSAIoctl(s, SIO_GET_INTERFACE_LIST, NULL, 0,
                     ifaces, sizeof(ifaces), &bytes_ret, NULL, NULL) == 0) {
            int count = (int)(bytes_ret / sizeof(INTERFACE_INFO));
            char ips[16][64];
            int prios[16];
            int n = 0;
            for (int i = 0; i < count && n < 16; i++) {
                u_long flags = ifaces[i].iiFlags;
                if (!(flags & IFF_UP) || (flags & IFF_LOOPBACK)) {
                    continue;
                }
                const struct sockaddr_in *sa = &ifaces[i].iiAddress.AddressIn;
                if (sa->sin_family != AF_INET) {
                    continue;
                }
                char ip_s[64] = "";
                if (inet_ntop(AF_INET, &sa->sin_addr, ip_s, (socklen_t)sizeof(ip_s)) == NULL) {
                    continue;
                }
                if (strncmp(ip_s, "127.", 4) == 0 || strncmp(ip_s, "169.254.", 8) == 0) {
                    continue;
                }
                int dup = 0;
                for (int k = 0; k < n; k++) {
                    if (strcmp(ips[k], ip_s) == 0) {
                        dup = 1;
                        break;
                    }
                }
                if (!dup) {
                    snprintf(ips[n], sizeof(ips[n]), "%s", ip_s);
                    prios[n] = ip_route_priority(ip_s);
                    n++;
                }
            }
            if (n > 0) {
                /* Ordena LAN privada (prio 2) antes de VPN/Radmin (prio 1) */
                out_buf[0] = '\0';
                for (int pass = 2; pass >= 1; pass--) {
                    for (int i = 0; i < n; i++) {
                        if (prios[i] != pass) continue;
                        if (out_buf[0] != '\0') {
                            strncat(out_buf, " | ", (size_t)out_size - strlen(out_buf) - 1);
                        }
                        strncat(out_buf, ips[i], (size_t)out_size - strlen(out_buf) - 1);
                    }
                }
            }
        }
        closesocket(s);
    }
    WSACleanup();
#endif
}

/* Atualiza o slot 0 com nome, skin, aparencia, modo de jogo e fase escolhidos pelo host. */
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

/* Remove do lobby clientes que pararam de responder alem do tempo limite. */
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

/* Envia a lista atual de jogadores do lobby para todos os clientes conectados. */
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

/* No host faz broadcast do roster; no cliente manda seu estado de lobby para o host. */
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

    return net_transport_send(net, net->peer_ip, net->peer_port, &out, (int)sizeof(out));
#else
    return 0;
#endif
}

/* Envia aviso explicito de saida da sala ou partida (repete 3x no UDP para nao perder). */
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

    int repeat = (net->transport == NET_TRANSPORT_RELAY) ? 1 : 3;

    if (net->mode == NET_MODE_HOST) {
        int sent_any = 0;
        for (int p = 1; p < NET_MAX_PLAYERS; p++) {
            if (!net->slots[p].active || !net->peer_addrs[p].has_addr) {
                continue;
            }
            for (int k = 0; k < repeat; k++) {
                if (net_transport_send(net, net->peer_addrs[p].ip, net->peer_addrs[p].port,
                                       &out, (int)sizeof(out))) {
                    sent_any = 1;
                }
            }
        }
        return sent_any;
    }

    if (!net->has_peer_addr) {
        return 0;
    }

    int sent_ok = 0;
    for (int k = 0; k < repeat; k++) {
        if (net_transport_send(net, net->peer_ip, net->peer_port, &out, (int)sizeof(out))) {
            sent_ok = 1;
        }
    }
    return sent_ok;
#else
    return 0;
#endif
}

/* --- Lobby multiplayer (ate 4 jogadores) e descoberta automatica na LAN --- */

/* Drena pacotes pendentes enquanto o jogador esta no lobby:
 * trata roster, inicio antecipado da partida, restore de rejoin, discover e leave. */
int net_poll_lobby(NetContext *net, const NetCtrlPacket *host_offer, int *peer_left_out) {
    if (peer_left_out) {
        *peer_left_out = 0;
    }
    if (!net || net->mode == NET_MODE_OFFLINE || !net->initialized) {
        return 0;
    }

#ifdef _WIN32
    int received_any = 0;
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
        unsigned int sip = 0;
        unsigned short sport = 0;

        int bytes = net_transport_recv(net, rx.raw, (int)sizeof(rx.raw), &sip, &sport);
        if (bytes < 0) {
            break;
        }

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
            /* Se o host ja comecou a partida e mandou snapshot, o cliente entra direto */
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
            /* Resposta do host ao entrar em uma partida em andamento (com ou sem estado salvo) */
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
                    net_transport_send(net, sip, sport, &reply, (int)sizeof(reply));
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

/* Envia um pacote DISCOVER_REQ avulso para um IP/broadcast especifico. */
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
 * (255.255.255.255) e broadcast dirigido com a mascara real de cada interface ativa
 * (incluindo sub-redes /21 como 255.255.248.0 e Radmin VPN 26.255.255.255). */
void net_discovery_send_probe(unsigned long long sock_handle, int port) {
    if (sock_handle == 0 || port <= 0) {
        return;
    }
#ifdef _WIN32
    send_probe_to_ip(sock_handle, "127.0.0.1", port);
    send_probe_to_ip(sock_handle, "255.255.255.255", port);

    /* Consulta todas as interfaces ativas e calcula o broadcast exato da sub-rede */
    SOCKET s = (SOCKET)sock_handle;
    INTERFACE_INFO ifaces[16];
    DWORD bytes_ret = 0;
    int probed_via_ioctl = 0;
    if (WSAIoctl(s, SIO_GET_INTERFACE_LIST, NULL, 0,
                 ifaces, sizeof(ifaces), &bytes_ret, NULL, NULL) == 0) {
        int count = (int)(bytes_ret / sizeof(INTERFACE_INFO));
        for (int i = 0; i < count; i++) {
            u_long flags = ifaces[i].iiFlags;
            if (!(flags & IFF_UP) || (flags & IFF_LOOPBACK)) {
                continue;
            }
            const struct sockaddr_in *sa = &ifaces[i].iiAddress.AddressIn;
            const struct sockaddr_in *nm = &ifaces[i].iiNetmask.AddressIn;
            if (sa->sin_family != AF_INET) {
                continue;
            }
            ULONG ip_n = sa->sin_addr.s_addr;
            ULONG mask_n = nm->sin_addr.s_addr;
            if (ip_n == 0u || mask_n == 0u) {
                continue;
            }
            ULONG bcast_n = (ip_n & mask_n) | (~mask_n);
            if (bcast_n == 0u || bcast_n == 0xFFFFFFFFu) {
                continue;
            }
            struct in_addr ba;
            ba.s_addr = bcast_n;
            char bcast_str[64] = "";
            if (inet_ntop(AF_INET, &ba, bcast_str, (socklen_t)sizeof(bcast_str)) != NULL &&
                strncmp(bcast_str, "127.", 4) != 0) {
                send_probe_to_ip(sock_handle, bcast_str, port);
                probed_via_ioctl = 1;
            }
        }
    }

    /* Se o WSAIoctl falhar por algum motivo, chuta um /24 no IP local (.255) */
    if (!probed_via_ioctl) {
        char local_ip[64] = "";
        net_get_local_ipv4(local_ip, (int)sizeof(local_ip));
        if (strncmp(local_ip, "127.", 4) != 0) {
            char *last_dot = strrchr(local_ip, '.');
            if (last_dot) {
                snprintf(last_dot, (size_t)(local_ip + sizeof(local_ip) - last_dot), ".255");
                send_probe_to_ip(sock_handle, local_ip, port);
            }
        }
    }
#else
    (void)sock_handle;
    (void)port;
#endif
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

        /* Agrupa respostas pelo host_session_id para nao duplicar host com varias placas de rede */
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
            /* Prefere ficar com o IP de maior prioridade (127.0.0.1 > LAN > VPN) */
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

    /* Remove salas que nao responderam nos ultimos 3 segundos */
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

/* Fecha o socket de descoberta LAN e libera a contagem do Winsock. */
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

/* Encerra a conexao ativa (fecha socket UDP ou para as threads e handles WinHTTP do relay). */
void net_close(NetContext *net) {
    if (!net) {
        return;
    }

#ifdef _WIN32
    if (net->transport == NET_TRANSPORT_RELAY) {
        NetRelayState *st = (NetRelayState *)(uintptr_t)net->relay_handle;
        if (st) {
            /* Aguarda brevemente para enviar um eventual LOBBY_LEAVE antes de encerrar */
            for (int w = 0; w < 6; w++) {
                int pending = 0;
                EnterCriticalSection(&st->cs);
                pending = st->tx_count;
                LeaveCriticalSection(&st->cs);
                if (pending == 0 || InterlockedCompareExchange(&st->status, 0, 0) != NET_RELAY_READY) {
                    break;
                }
                Sleep(10);
            }
            InterlockedExchange(&st->stop_flag, 1);
            if (st->tx_event) {
                SetEvent(st->tx_event);
            }
            /* Fecha handles WinHTTP antes do WaitForSingleObject para destravar WinHttpWebSocketReceive */
            if (st->h_ws) {
                WinHttpCloseHandle(st->h_ws);
                st->h_ws = NULL;
            }
            if (st->h_connect) {
                WinHttpCloseHandle(st->h_connect);
                st->h_connect = NULL;
            }
            if (st->h_session) {
                WinHttpCloseHandle(st->h_session);
                st->h_session = NULL;
            }
            if (st->tx_thread) {
                WaitForSingleObject(st->tx_thread, 1500);
                CloseHandle(st->tx_thread);
            }
            if (st->worker_thread) {
                WaitForSingleObject(st->worker_thread, 1500);
                CloseHandle(st->worker_thread);
            }
            if (st->tx_event) {
                CloseHandle(st->tx_event);
            }
            DeleteCriticalSection(&st->cs);
            free(st);
        }
        net->relay_handle = 0;
    } else if (net->initialized) {
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

/* Gera codigo curto de 4 caracteres sem letras/numeros confusos (sem O/0/I/1). */
void net_generate_room_code(char *out_code, int out_size) {
    if (!out_code || out_size <= 1) {
        return;
    }
    static const char ALPH[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
    unsigned int seed = net_next_unique_id();
    int len = (out_size >= 5) ? 4 : (out_size - 1);
    for (int i = 0; i < len; i++) {
        seed = seed * 1664525u + 1013904223u;
        out_code[i] = ALPH[(seed >> 16) % (sizeof(ALPH) - 1)];
    }
    out_code[len] = '\0';
}

/* Resolve a URL do relay na ordem: SECTOR404_RELAY_URL, relay_url.txt e URL padrao. */
void net_get_relay_url(char *out_url, int out_size) {
    if (!out_url || out_size <= 0) {
        return;
    }
    const char *env = getenv("SECTOR404_RELAY_URL");
    if (env && env[0] != '\0') {
        snprintf(out_url, (size_t)out_size, "%s", env);
        return;
    }
    const char *cfg_paths[2] = {"relay_url.txt", "../relay_url.txt"};
    for (int i = 0; i < 2; i++) {
        FILE *fp = fopen(cfg_paths[i], "r");
        if (fp) {
            char buf[160] = "";
            if (fgets(buf, (int)sizeof(buf), fp) != NULL) {
                size_t l = strlen(buf);
                while (l > 0 && (buf[l - 1] == '\n' || buf[l - 1] == '\r' ||
                                 buf[l - 1] == ' ' || buf[l - 1] == '\t')) {
                    buf[--l] = '\0';
                }
                if (buf[0] != '\0') {
                    snprintf(out_url, (size_t)out_size, "%s", buf);
                    fclose(fp);
                    return;
                }
            }
            fclose(fp);
        }
    }
    snprintf(out_url, (size_t)out_size, "%s", NET_DEFAULT_RELAY_URL);
}

/* Inicia uma sessao online via WebSocket relay, limpando o codigo da sala
 * e disparando a thread em background para conectar sem travar o menu. */
int net_init_relay(NetContext *net, NetMode mode, const char *relay_url, const char *room_code) {
    if (!net || mode == NET_MODE_OFFLINE) {
        return 0;
    }

    /* Mantem apenas caracteres alfanumericos em maiuscula para o codigo da sala */
    char clean_code[16] = "";
    if (room_code) {
        int out_i = 0;
        for (int i = 0; room_code[i] != '\0' && out_i < 8; i++) {
            unsigned char ch = (unsigned char)room_code[i];
            if (isalnum(ch)) {
                clean_code[out_i++] = (char)toupper(ch);
            }
        }
        clean_code[out_i] = '\0';
    }
    if (strlen(clean_code) < 3) {
        return 0;
    }

    memset(net, 0, sizeof(*net));
    net->mode = mode;
    net->transport = NET_TRANSPORT_RELAY;
    net->port = 443;
    net->local_slot = (mode == NET_MODE_HOST) ? 0 : -1;
    net->active_player_count = 1;
    net->local_client_id = net_get_local_client_id();
    for (int i = 0; i < NET_MAX_PLAYERS; i++) {
        net->last_attacker_of_slot[i] = -1;
    }
    snprintf(net->room_code, sizeof(net->room_code), "%s", clean_code);
    if (relay_url && relay_url[0] != '\0') {
        snprintf(net->relay_url, sizeof(net->relay_url), "%s", relay_url);
    } else {
        net_get_relay_url(net->relay_url, (int)sizeof(net->relay_url));
    }

    if (mode == NET_MODE_HOST) {
        net->host_session_id = net_next_unique_id();
        net->slots[0].active = 1;
        net->slots[0].player_id = 1;
        net->slots[0].ready = 1;
        net->slots[0].client_id = net->local_client_id;
    } else {
        /* Cliente aponta para o peer ficticio 0 (que representa o host no servidor relay) */
        net->has_peer_addr = 1;
        net->peer_ip = relay_peer_to_ip(0u);
        net->peer_port = relay_peer_to_port(0u);
        net->last_host_rx = net_monotonic_sec();
    }

#ifdef _WIN32
    NetRelayState *st = (NetRelayState *)calloc(1, sizeof(NetRelayState));
    if (!st) {
        return 0;
    }
    st->mode = mode;
    snprintf(st->url, sizeof(st->url), "%s", net->relay_url);
    snprintf(st->room_code, sizeof(st->room_code), "%s", clean_code);
    st->status = (LONG)NET_RELAY_CONNECTING;
    InitializeCriticalSection(&st->cs);
    st->tx_event = CreateEvent(NULL, FALSE, FALSE, NULL);
    st->worker_thread = CreateThread(NULL, 0, relay_worker_thread_proc, st, 0, NULL);
    if (!st->tx_event || !st->worker_thread) {
        if (st->tx_event) CloseHandle(st->tx_event);
        DeleteCriticalSection(&st->cs);
        free(st);
        return 0;
    }
    net->relay_handle = (unsigned long long)(uintptr_t)st;
    net->initialized = 1;
    return 1;
#else
    return 0;
#endif
}

/* Consulta de forma segura o estado atual do tunel WebSocket e eventual mensagem de erro. */
NetRelayStatus net_relay_get_status(const NetContext *net, char *err_out, int err_size) {
    if (err_out && err_size > 0) {
        err_out[0] = '\0';
    }
    if (!net || net->transport != NET_TRANSPORT_RELAY) {
        return NET_RELAY_IDLE;
    }
#ifdef _WIN32
    NetRelayState *st = (NetRelayState *)(uintptr_t)net->relay_handle;
    if (!st) {
        return NET_RELAY_CLOSED;
    }
    NetRelayStatus s = (NetRelayStatus)InterlockedCompareExchange(&st->status, 0, 0);
    if (err_out && err_size > 0) {
        EnterCriticalSection(&st->cs);
        snprintf(err_out, (size_t)err_size, "%s", st->error_msg);
        LeaveCriticalSection(&st->cs);
    }
    return s;
#else
    return NET_RELAY_IDLE;
#endif
}
