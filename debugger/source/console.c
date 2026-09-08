// SPDX-License-Identifier: GPL-3.0-only

#include "console.h"
#include "debug.h"
#include "kdbg.h"
#include "../../syscalls.h"

int console_print_handle(int fd, struct cmd_packet *packet) {
    uint32_t *lenp;
    uint32_t length;
    void *data;

    lenp = (uint32_t *)packet->data;
    if (!lenp) {
        net_send_int32(fd, CMD_DATA_NULL);
        return 1;
    }

    length = *lenp;
    data = net_alloc_buffer(length);
    if (!data) {
        net_send_int32(fd, CMD_DATA_NULL);
        return 1;
    }

    memset(data, NULL, length);
    net_recv_all(fd, data, length, 1);
    syscall(PS4DEBUG_SYS_CONSOLE_CMD, 2, data);
    net_send_int32(fd, CMD_SUCCESS);
    free(data);
    return 0;
}

int console_notify_handle(int fd, struct cmd_packet *packet) {
    struct cmd_console_notify_packet *np;
    void *data;

    np = (struct cmd_console_notify_packet *)packet->data;
    if (!np) {
        net_send_int32(fd, CMD_DATA_NULL);
        return 1;
    }

    data = net_alloc_buffer(np->length);
    if (!data) {
        net_send_int32(fd, CMD_DATA_NULL);
        return 1;
    }

    memset(data, NULL, np->length);
    net_recv_all(fd, data, np->length, 1);
    sceSysUtilSendSystemNotificationWithText(np->messageType, data);
    net_send_int32(fd, CMD_SUCCESS);
    free(data);
    return 0;
}

static int sfo_read_file(const char *path, uint8_t *buf, size_t bufsz, size_t *out_len) {
    int fdf = open(path, 0, 0);
    if (fdf < 0) return -1;
    size_t total = 0;
    while (total < bufsz) {
        ssize_t n = read(fdf, buf + total, bufsz - total);
        if (n <= 0) break;
        total += (size_t)n;
    }
    close(fdf);
    if (out_len) *out_len = total;
    return 0;
}

static int sfo_get_string(const uint8_t *sfo, size_t sfo_len,
                          const char *want_key, char *out, size_t out_max) {
    if (out_max == 0) return -1;
    out[0] = '\0';
    if (sfo_len < 20) return -1;
    if (sfo[0] != 0 || sfo[1] != 'P' || sfo[2] != 'S' || sfo[3] != 'F') return -1;
    uint32_t key_table  = *(const uint32_t *)(sfo + 8);
    uint32_t data_table = *(const uint32_t *)(sfo + 12);
    uint32_t nentries   = *(const uint32_t *)(sfo + 16);
    if (key_table  > sfo_len) return -1;
    if (data_table > sfo_len) return -1;
    size_t want_len = strlen(want_key);
    for (uint32_t i = 0; i < nentries; i++) {
        size_t eoff = 20 + (size_t)i * 16;
        if (eoff + 16 > sfo_len) return -1;
        const uint8_t *e = sfo + eoff;
        uint16_t key_off  = *(const uint16_t *)(e + 0);

        uint32_t param_len = *(const uint32_t *)(e + 4);
        uint32_t data_off  = *(const uint32_t *)(e + 12);
        size_t key_abs = (size_t)key_table + key_off;
        if (key_abs + want_len + 1 > sfo_len) continue;
        if (memcmp(sfo + key_abs, want_key, want_len) != 0) continue;
        if (sfo[key_abs + want_len] != '\0') continue;
        size_t data_abs = (size_t)data_table + data_off;
        if (data_abs + param_len > sfo_len) return -1;
        size_t copy = param_len;
        if (copy >= out_max) copy = out_max - 1;
        memcpy(out, sfo + data_abs, copy);
        out[copy] = '\0';

        while (copy > 0 && out[copy - 1] == '\0') copy--;
        out[copy] = '\0';
        return 0;
    }
    return -2;
}

struct cmd_console_foreground_app_response {
    uint32_t pid;
    char     titleid[16];
    char     contentid[64];
    char     name[40];
    char     app_ver[8];
} __attribute__((packed));

int console_foreground_app_handle(int fd, struct cmd_packet *packet) {
    (void)packet;
    struct cmd_console_foreground_app_response resp;
    memset(&resp, 0, sizeof(resp));

    uint64_t num = 0;
    sys_proc_list(NULL, &num);
    if (num == 0) {

        net_send_int32(fd, CMD_SUCCESS);
        net_send_all(fd, &resp, sizeof(resp));
        return 0;
    }
    size_t list_bytes = sizeof(struct proc_list_entry) * (size_t)num;
    struct proc_list_entry *list = (struct proc_list_entry *)net_alloc_buffer(list_bytes);
    if (!list) { net_send_int32(fd, CMD_DATA_NULL); return 1; }
    sys_proc_list(list, &num);

    int found_pid = 0;
    for (uint64_t i = 0; i < num; i++) {
        if (strncmp(list[i].p_comm, "eboot.bin", 9) == 0
            && (list[i].p_comm[9] == '\0' || list[i].p_comm[9] == ' ')) {
            found_pid = list[i].pid;
            break;
        }
    }
    free(list);

    if (found_pid == 0) {

        net_send_int32(fd, CMD_SUCCESS);
        net_send_all(fd, &resp, sizeof(resp));
        return 0;
    }

    struct sys_proc_info_args info;
    memset(&info, 0, sizeof(info));
    if (sys_proc_cmd((uint64_t)found_pid, SYS_PROC_INFO, &info) != 0) {
        net_send_int32(fd, CMD_ERROR);
        return 1;
    }

    resp.pid = (uint32_t)found_pid;
    memcpy(resp.titleid,   info.titleid,   sizeof(resp.titleid));
    memcpy(resp.contentid, info.contentid, sizeof(resp.contentid));
    memcpy(resp.name,      info.name,      sizeof(resp.name));

    char titleid_z[17];
    memcpy(titleid_z, info.titleid, 16);
    titleid_z[16] = '\0';
    char sfo_path[96];
    snprintf(sfo_path, sizeof(sfo_path), "/system_data/priv/appmeta/%s/param.sfo", titleid_z);

    uint8_t *sfo_buf = (uint8_t *)net_alloc_buffer(8192);
    if (sfo_buf) {
        size_t sfo_len = 0;
        if (sfo_read_file(sfo_path, sfo_buf, 8192, &sfo_len) == 0 && sfo_len >= 20) {
            char ver_app[8] = {0}, ver_pkg[8] = {0};
            sfo_get_string(sfo_buf, sfo_len, "APP_VER", ver_app, sizeof(ver_app));
            sfo_get_string(sfo_buf, sfo_len, "VERSION", ver_pkg, sizeof(ver_pkg));
            const char *pick = "";
            if (ver_app[0] && ver_pkg[0])  pick = (strcmp(ver_app, ver_pkg) >= 0) ? ver_app : ver_pkg;
            else if (ver_app[0])           pick = ver_app;
            else if (ver_pkg[0])           pick = ver_pkg;
            size_t plen = strlen(pick);
            if (plen >= sizeof(resp.app_ver)) plen = sizeof(resp.app_ver) - 1;
            memcpy(resp.app_ver, pick, plen);
            resp.app_ver[plen] = '\0';
        }
        free(sfo_buf);
    }

    net_send_int32(fd, CMD_SUCCESS);
    net_send_all(fd, &resp, sizeof(resp));
    return 0;
}

int console_reboot_handle(int fd, struct cmd_packet *packet) {
    if (g_debugging) {
        debug_cleanup(curdbgctx);
        sceNetSocketClose(fd);
    }
    syscall(PS4DEBUG_SYS_CONSOLE_CMD, 1, 0);

    return 1;
}

int console_handle(int fd, struct cmd_packet *packet) {
    switch(packet->cmd) {
        case CMD_CONSOLE_REBOOT:
            return console_reboot_handle(fd, packet);
        case CMD_CONSOLE_PRINT:
            return console_print_handle(fd, packet);
        case CMD_CONSOLE_NOTIFY:
            return console_notify_handle(fd, packet);
        case CMD_CONSOLE_INFO:

            net_send_int32(fd, CMD_SUCCESS);
            return 0;
        case 0xBDDD0006u:
            return console_foreground_app_handle(fd, packet);
        case CMD_CONSOLE_END:

            return 1;
    }

    return 1;
}
