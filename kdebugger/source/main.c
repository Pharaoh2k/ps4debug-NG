// SPDX-License-Identifier: GPL-3.0-only

#include <ksdk.h>
#include "hooks.h"

int _main(void) {
    if (init_ksdk()) {
        return 1;
    }

    printf("[ps4debug-ng] kernel base 0x%llX\n", get_kbase());

    if(install_hooks()) {
        printf("[ps4debug-ng] failed to install hooks\n");
        return 1;
    }

    return 0;
}
