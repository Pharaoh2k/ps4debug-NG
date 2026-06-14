// SPDX-License-Identifier: GPL-3.0-only

#include <ps4.h>
#include "ptrace.h"
#include "server.h"
#include "debug.h"
#include "protocol.h"
#include "net.h"

extern void run_init_array(void);

static int ps4debug_already_running(void) {
    int fd = sceNetSocket("guard", AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return 0;
    }

    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_len         = sizeof(sa);
    sa.sin_family      = AF_INET;
    sa.sin_port        = sceNetHtons(SERVER_PORT);
    sa.sin_addr.s_addr = 0x0100007Fu;

    int rc = sceNetConnect(fd, (struct sockaddr *)&sa, sizeof(sa));
    sceNetSocketClose(fd);
    return (rc == 0);
}

int _main(void) {

    initKernel();
    initLibc();
    initPthread();
    initNetwork();
    initSysUtil();

    run_init_array();

    sceKernelSleep(2);

    if (ps4debug_already_running()) {
        sceSysUtilSendSystemNotificationWithText(222, "ps4debug-NG is already running - injection skipped");
        return 0;
    }

    sys_console_cmd(SYS_CONSOLE_CMD_JAILBREAK, NULL);

    mkdir("/update/PS4UPDATE.PUP", 0777);
    mkdir("/update/PS4UPDATE.PUP.net.temp", 0777);

    int retry = 0;
    char ip_buf[16];

    while (1) {
        memset(ip_buf, 0, sizeof(ip_buf));
        net_get_ip_address(ip_buf);

        if (strlen(ip_buf) > 4) {

            sceSysUtilSendSystemNotificationWithText(222, PACKET_BRANDING "\nBased on source by golden\n\xC2\xA0\xC2\xA0\xC2\xA0\xC2\xA0\xC2\xA0\xC2\xA0\xC2\xA0\xC2\xA0\xC2\xA0\xC2\xA0\xC2\xA0Inspired by\nCtn, SiSTRo & DeathRGH\n\xC2\xA0\xC2\xA0\xC2\xA0\xC2\xA0\xC2\xA0\xC2\xA0\xC2\xA0\xC2\xA0\xC2\xA0\xC2\xA0\xC2\xA0\xC2\xA0\xE2\x9D\xA4\xE2\x9D\xA4\xE2\x9D\xA4\xE2\x9D\xA4");
            retry = 0;
            start_server();
            continue;
        }

        int next = retry + 1;
        if (retry == 0) {

            sceSysUtilSendSystemNotificationWithText(222, "ps4debug-ng by OpenSourcerer v" PS4DEBUG_NG_VERSION_STR " disconnected.");
            sceKernelSleep(2);
        } else if (next <= 99) {
            sceKernelSleep(2);
        } else {
            sceKernelSleep(1000);
        }
        retry = next;
    }

    return 0;
}
