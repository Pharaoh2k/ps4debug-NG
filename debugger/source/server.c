// SPDX-License-Identifier: GPL-3.0-only

#include "server.h"

struct server_client servclients[SERVER_MAXCLIENTS];

int g_broadcast_fd;

struct server_client *alloc_client() {
    for(int i = 0; i < SERVER_MAXCLIENTS; i++) {
        if(servclients[i].id == 0) {
            servclients[i].id = i + 1;
            return &servclients[i];
        }
    }

    return NULL;
}

void free_client(struct server_client *svc) {
    svc->id = 0;
    sceNetSocketClose(svc->fd);

    if (svc->debugging) {
        debug_full_teardown(svc);
    }

    memset(svc, NULL, sizeof(struct server_client));
}

int handle_version(int fd, struct cmd_packet *packet) {
    uint32_t len = strlen(PACKET_VERSION);
    net_send_all(fd, &len, sizeof(uint32_t));
    net_send_all(fd, PACKET_VERSION, len);
    return 0;
}

extern uint16_t get_fw_version(void);

int cmd_handler(int fd, struct cmd_packet *packet) {
    uint16_t w;
    uint32_t len;

    if (!VALID_CMD(packet->cmd)) {
        return 1;
    }

    switch (packet->cmd) {
        case CMD_VERSION:
            return handle_version(fd, packet);
        case CMD_FW_VERSION:
            w = get_fw_version();
            net_send_all(fd, &w, sizeof(w));
            return 0;
        case CMD_BRANDING: {

            static const char brand[] = PACKET_BRANDING "\0" "1.0";
            len = (uint32_t)(sizeof(brand) - 1);
            net_send_all(fd, &len, sizeof(uint32_t));
            net_send_all(fd, brand, len);
            return 0;
        }
        case CMD_PLATFORM_ID:

            w = 4;
            net_send_all(fd, &w, sizeof(w));
            return 0;
        case CMD_PROC_NOP:
            net_send_int32(fd, CMD_SUCCESS);
            return 0;
    }

    if (VALID_PROC_CMD(packet->cmd)) {
        return proc_handle(fd, packet);
    } else if (VALID_DEBUG_CMD(packet->cmd)) {
        return debug_handle(fd, packet);
    } else if (VALID_KERN_CMD(packet->cmd)) {
        return kern_handle(fd, packet);
    } else if (VALID_CONSOLE_CMD(packet->cmd)) {
        return console_handle(fd, packet);
    }

    return 0;
}

int handle_client(struct server_client *svc) {
    struct cmd_packet packet;
    uint32_t rsize;
    uint32_t length;
    void *data;
    int fd;
    int r;

    fd = svc->fd;

    struct timeval tv;
    memset(&tv, NULL, sizeof(tv));
    tv.tv_usec = 1000;

    while(1) {

        fd_set sfd;
        FD_ZERO(&sfd);
        FD_SET(fd, &sfd);
        errno = NULL;
        net_select(FD_SETSIZE, &sfd, NULL, NULL, &tv);

        if (errno == 9) {
            goto error;
        }

        if(FD_ISSET(fd, &sfd)) {

            memset(&packet, NULL, CMD_PACKET_SIZE);

            rsize = net_recv_all(fd, &packet, CMD_PACKET_SIZE, 0);

            if (rsize <= 0) {
                goto error;
            }

            if (errno == 54 || errno == 58 || errno == 9) {
                goto error;
            }
        } else {

            if (svc->debugging) {

                scePthreadMutexLock(&g_debug_mutex);
                int dde_r = dispatch_debug_events();
                if (dde_r != 0) {
                    scePthreadMutexUnlock(&g_debug_mutex);
                    goto error;
                }
                scePthreadMutexUnlock(&g_debug_mutex);
            }

            if (errno == 54 || errno == 58 || errno == 9) {
                goto error;
            }

            sceKernelUsleep(250);
            continue;
        }

        if (packet.magic != PACKET_MAGIC) {
            continue;
        }

        if (rsize != CMD_PACKET_SIZE) {
            continue;
        }

        length = packet.datalen;
        if (length) {

            data = net_alloc_buffer(length);
            if (!data) {
                goto error;
            }

            r = net_recv_all(fd, data, length, 1);
            if (!r) {
                goto error;
            }

            packet.data = data;
        } else {
            packet.data = NULL;
        }

        if (packet.cmd == CMD_DEBUG_ATTACH) {
            r = debug_attach_handle_svc(svc, &packet);
        } else {
            r = cmd_handler(fd, &packet);
        }

        if (data) {
            free(data);
            data = NULL;
        }

        if (r) {
            goto error;
        }

        if (svc->debugging && g_pending_sig_pid != 0) {
            scePthreadMutexLock(&g_debug_mutex);
            int dde_r = dispatch_debug_events();
            scePthreadMutexUnlock(&g_debug_mutex);
            if (dde_r != 0) goto error;
        }
    }

error:
    free_client(svc);

    return 0;
}

void configure_socket(int fd) {
    int flag;

    flag = 1;
    sceNetSetsockopt(fd, SOL_SOCKET, SO_NBIO, (char *)&flag, sizeof(flag));

    flag = 1;
    sceNetSetsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (char *)&flag, sizeof(flag));

    flag = 1;
    sceNetSetsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, (char *)&flag, sizeof(flag));

    flag = 1;
    sceNetSetsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, (char *)&flag, sizeof(flag));

    flag = 1;
    sceNetSetsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (char *)&flag, sizeof(flag));

    flag = 65537;
    sceNetSetsockopt(fd, SOL_SOCKET, SO_SNDBUF, (char *)&flag, sizeof(flag));
    sceNetSetsockopt(fd, SOL_SOCKET, SO_RCVBUF, (char *)&flag, sizeof(flag));
}

void *broadcast_thread(void *arg) {
    struct sockaddr_in server;
    struct sockaddr_in client;
    unsigned int clisize;
    int serv;
    int flag;
    int r;
    uint32_t magic;

    server.sin_len = sizeof(server);
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = IN_ADDR_ANY;
    server.sin_port = sceNetHtons(BROADCAST_PORT);
    memset(server.sin_zero, NULL, sizeof(server.sin_zero));

    serv = sceNetSocket("broadsock", AF_INET, SOCK_DGRAM, 0);
    if(serv < 0) {
        return NULL;
    }

    g_broadcast_fd = serv;

    flag = 1;
    sceNetSetsockopt(serv, SOL_SOCKET, SO_BROADCAST, (char *)&flag, sizeof(flag));

    flag = 1;
    sceNetSetsockopt(serv, SOL_SOCKET, SO_REUSEADDR, (char *)&flag, sizeof(flag));

    flag = 1;
    sceNetSetsockopt(serv, IPPROTO_TCP, TCP_NODELAY, (char *)&flag, sizeof(flag));

    r = sceNetBind(serv, (struct sockaddr *)&server, sizeof(server));
    if(r) {
        g_broadcast_fd = 0;
        sceNetSocketClose(serv);
        return NULL;
    }

    int libNet = sceKernelLoadStartModule("libSceNet.sprx", 0, NULL, 0, 0, 0);
    int (*sceNetRecvfrom)(int s, void *buf, unsigned int len, int flags, struct sockaddr *from, unsigned int *fromlen);
    int (*sceNetSendto)(int s, void *msg, unsigned int len, int flags, struct sockaddr *to, unsigned int tolen);
    RESOLVE(libNet, sceNetRecvfrom);
    RESOLVE(libNet, sceNetSendto);

    while(1) {
        scePthreadYield();

        magic = 0;
        clisize = sizeof(client);
        r = sceNetRecvfrom(serv, &magic, sizeof(uint32_t), 0, (struct sockaddr *)&client, &clisize);

        if (r < 0) {

            break;
        }

        if (magic == BROADCAST_MAGIC) {
            sceNetSendto(serv, &magic, sizeof(uint32_t), 0, (struct sockaddr *)&client, clisize);
        }

        sceKernelSleep(1);
    }

    g_broadcast_fd = 0;
    return NULL;
}

int start_server() {
    struct sockaddr_in server;
    struct sockaddr_in client;
    struct server_client *svc;
    unsigned int len = sizeof(client);
    int serv, fd;
    int r;

    ScePthread broadcast;
    scePthreadCreate(&broadcast, NULL, broadcast_thread, NULL, "broadcast");

    server.sin_len = sizeof(server);
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = IN_ADDR_ANY;
    server.sin_port = sceNetHtons(SERVER_PORT);
    memset(server.sin_zero, NULL, sizeof(server.sin_zero));

    serv = sceNetSocket("debugserver", AF_INET, SOCK_STREAM, 0);
    if(serv < 0) {
        return 1;
    }

    configure_socket(serv);

    r = sceNetBind(serv, (struct sockaddr *)&server, sizeof(server));
    if(r) {
        return 1;
    }

    r = sceNetListen(serv, SERVER_MAXCLIENTS * 2);
    if(r) {
        return 1;
    }

    memset(servclients, NULL, sizeof(struct server_client) * SERVER_MAXCLIENTS);

    g_proc_auth_bits  = 0;
    g_debugging       = 0;
    g_pending_signal  = 0;
    g_pending_sig_pid = 0;
    curdbgctx         = NULL;
    curdbgcli         = NULL;

    scePthreadMutexInit(&g_debug_mutex, NULL, NULL);

    g_stepping_lwpid = 0;

    while(1) {
        scePthreadYield();

        errno = NULL;
        fd = sceNetAccept(serv, (struct sockaddr *)&client, &len);
        if (fd < 0) {
            if (errno == 163) {

                break;
            }
            sceKernelUsleep(1000 * 250);
            continue;
        }
        if (!errno) {
            svc = alloc_client();
            if (!svc) {
                sceNetSocketClose(fd);
                sceKernelUsleep(1000 * 250);
                continue;
            }

            configure_socket(fd);

            svc->fd = fd;
            svc->debugging = 0;
            memcpy(&svc->client, &client, sizeof(svc->client));
            memset(&svc->dbgctx, NULL, sizeof(svc->dbgctx));

            ScePthread thread;
            scePthreadCreate(&thread, NULL, (void * (*)(void *))handle_client, (void *)svc, "clienthandler");
        }

        sceKernelUsleep(1000 * 250);
    }

    for (int i = 0; i < SERVER_MAXCLIENTS; i++) {
        if (servclients[i].id) {
            servclients[i].id = 0;
            sceNetSocketClose(servclients[i].fd);
        }
    }
    sceNetSocketClose(serv);
    if (g_broadcast_fd > 0) {
        sceNetSocketAbort(g_broadcast_fd, 3);
        sceNetSocketClose(g_broadcast_fd);
        g_broadcast_fd = 0;
    }

    return 0;
}
