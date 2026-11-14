#include "network.h"
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

int net_init(NetContext *net, NetMode mode, const char *ip, int port) {
    if (!net) {
        return 0;
    }

    memset(net, 0, sizeof(*net));
    net->mode = mode;
    net->port = (port > 0) ? port : DEFAULT_PORT;

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
    }

    return 1;
#else
    (void)ip;
    return 0;
#endif
}

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

int net_receive_state(NetContext *net) {
    if (!net || net->mode == NET_MODE_OFFLINE || !net->initialized) {
        return 0;
    }

#ifdef _WIN32
    int received_any = 0;
    SOCKET s = (SOCKET)net->sock_handle;

    for (;;) {
        NetPacket pkt;
        struct sockaddr_in sender;
        int sender_len = (int)sizeof(sender);

        int bytes = recvfrom(
            s,
            (char *)&pkt,
            (int)sizeof(pkt),
            0,
            (struct sockaddr *)&sender,
            &sender_len
        );

        if (bytes == (int)sizeof(pkt)) {
            net->remote_state = pkt;
            net->peer_ip = (unsigned int)sender.sin_addr.s_addr;
            net->peer_port = (unsigned short)sender.sin_port;
            net->has_peer_addr = 1;
            net->connected = 1;
            received_any = 1;
        } else {
            break;
        }
    }

    return received_any;
#else
    return 0;
#endif
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
