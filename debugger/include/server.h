// SPDX-License-Identifier: GPL-3.0-only

#ifndef _SERVER_H
#define _SERVER_H

#include <ps4.h>
#include "protocol.h"
#include "net.h"

#include "proc.h"
#include "debug.h"
#include "kern.h"
#include "console.h"

#define SERVER_PORT             744
#define SERVER_MAXCLIENTS       8

#define BROADCAST_PORT          1010
#define BROADCAST_MAGIC         0xFFFFAAAA

extern struct server_client servclients[SERVER_MAXCLIENTS];

extern int g_broadcast_fd;

struct server_client *alloc_client();
void free_client(struct server_client *svc);

int handle_version(int fd, struct cmd_packet *packet);
int cmd_handler(int fd, struct cmd_packet *packet, unsigned char client_idx);
int handle_client(struct server_client *svc);

void configure_socket(int fd);
void *broadcast_thread(void *arg);
int start_server();

#endif
