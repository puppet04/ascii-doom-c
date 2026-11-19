#include "network.h"
#include <stdio.h>
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
        union {
            NetPacket pkt;
            NetCtrlPacket ctrl;
            char raw[128];
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
        } else {
            break;
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

int net_send_lobby_state(NetContext *net, const NetCtrlPacket *pkt) {
    if (!net || !pkt || net->mode == NET_MODE_OFFLINE || !net->initialized || !net->has_peer_addr) {
        return 0;
    }

#ifdef _WIN32
    NetCtrlPacket out = *pkt;
    out.magic = NET_CTRL_MAGIC;
    out.msg_type = NET_MSG_LOBBY_STATE;

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
    (void)pkt;
    return 0;
#endif
}

int net_send_lobby_leave(NetContext *net) {
    if (!net || net->mode == NET_MODE_OFFLINE || !net->initialized || !net->has_peer_addr) {
        return 0;
    }

#ifdef _WIN32
    NetCtrlPacket out;
    memset(&out, 0, sizeof(out));
    out.magic = NET_CTRL_MAGIC;
    out.msg_type = NET_MSG_LOBBY_LEAVE;

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

    for (;;) {
        union {
            NetPacket state;
            NetCtrlPacket ctrl;
            char raw[128];
        } rx;
        struct sockaddr_in sender;
        int sender_len = (int)sizeof(sender);

        int bytes = recvfrom(s, rx.raw, (int)sizeof(rx.raw), 0,
                             (struct sockaddr *)&sender, &sender_len);
        if (bytes == (int)sizeof(NetPacket)) {
            net->remote_state = rx.state;
            net->peer_ip = (unsigned int)sender.sin_addr.s_addr;
            net->peer_port = (unsigned short)sender.sin_port;
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
                    sendto(s, (const char *)&reply, (int)sizeof(reply), 0,
                           (const struct sockaddr *)&sender, sender_len);
                }
            } else if (rx.ctrl.msg_type == NET_MSG_LOBBY_STATE) {
                net->peer_ip = (unsigned int)sender.sin_addr.s_addr;
                net->peer_port = (unsigned short)sender.sin_port;
                net->has_peer_addr = 1;
                net->connected = 1;
                net->remote_character = rx.ctrl.character_idx;
                net->remote_ready = rx.ctrl.ready;
                net->remote_level = rx.ctrl.level_idx;
                if (rx.ctrl.game_started) {
                    net->remote_started = 1;
                }
                snprintf(net->remote_name, sizeof(net->remote_name), "%s", rx.ctrl.player_name);
                snprintf(net->remote_map_name, sizeof(net->remote_map_name), "%s", rx.ctrl.map_name);
                received_any = 1;
            } else if (rx.ctrl.msg_type == NET_MSG_LOBBY_LEAVE) {
                net->connected = 0;
                net->remote_ready = 0;
                net->remote_started = 0;
                if (net->mode == NET_MODE_HOST) {
                    net->has_peer_addr = 0;
                }
                if (peer_left_out) {
                    *peer_left_out = 1;
                }
            }
        } else {
            break;
        }
    }

    return received_any;
#else
    (void)host_offer;
    return 0;
#endif
}

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

int net_discovery_poll(unsigned long long sock_handle, NetDiscoveredRoom *rooms,
                       int *room_count, int max_rooms, double now) {
    if (sock_handle == 0 || !rooms || !room_count || max_rooms <= 0) {
        return 0;
    }

#ifdef _WIN32
    int added_or_updated = 0;
    SOCKET s = (SOCKET)sock_handle;

    for (;;) {
        NetCtrlPacket pkt;
        struct sockaddr_in sender;
        int sender_len = (int)sizeof(sender);
        int bytes = recvfrom(s, (char *)&pkt, (int)sizeof(pkt), 0,
                             (struct sockaddr *)&sender, &sender_len);
        if (bytes != (int)sizeof(NetCtrlPacket) ||
            pkt.magic != NET_CTRL_MAGIC ||
            pkt.msg_type != NET_MSG_DISCOVER_OFFER) {
            break;
        }

        char ip_str[64] = "127.0.0.1";
        if (inet_ntop(AF_INET, &sender.sin_addr, ip_str, (socklen_t)sizeof(ip_str)) == NULL) {
            continue;
        }

        int idx = -1;
        for (int i = 0; i < *room_count; i++) {
            if (strcmp(rooms[i].ip, ip_str) == 0) {
                idx = i;
                break;
            }
        }
        if (idx < 0 && *room_count < max_rooms) {
            idx = (*room_count)++;
        }
        if (idx >= 0) {
            snprintf(rooms[idx].ip, sizeof(rooms[idx].ip), "%s", ip_str);
            snprintf(rooms[idx].host_name, sizeof(rooms[idx].host_name), "%s", pkt.player_name);
            snprintf(rooms[idx].map_name, sizeof(rooms[idx].map_name), "%s", pkt.map_name);
            rooms[idx].level_idx = pkt.level_idx;
            rooms[idx].port = pkt.port;
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
