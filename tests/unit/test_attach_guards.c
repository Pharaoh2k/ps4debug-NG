// SPDX-License-Identifier: GPL-3.0-only
//
// Source-level checks for the CMD_DEBUG_ATTACH failure handling in debug.c.
// The handler calls PS4 kernel APIs and cannot run on the host, so these
// checks pin how it is wired:
//
//   - PT_CONTINUE is only reached after wait4 reported a stop;
//   - the PT_DETACH result is checked, and a failed detach keeps the pid;
//   - a kept pid is never turned into a debug session, and every session
//     command still requires one;
//   - ATTACH and DETACH work on that state under g_debug_mutex;
//   - debug_full_teardown checks its PT_DETACH result too, and every
//     teardown caller holds g_debug_mutex.
//
// Build and run with: make -C tests/unit (or make -C debugger test)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

#define CHECK(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        failures++; \
    } \
} while (0)

static char *read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)n + 1);
    if (buf && fread(buf, 1, (size_t)n, f) != (size_t)n) { free(buf); buf = NULL; }
    if (buf) buf[n] = '\0';
    fclose(f);
    return buf;
}

// Malloc'd copy of the body of "int <name>(...) { ... }".
static char *body(const char *src, const char *name) {
    char sig[128];
    snprintf(sig, sizeof(sig), "\nint %s(", name);
    const char *p = strstr(src, sig);
    if (!p || !(p = strchr(p, '{'))) return NULL;
    int depth = 0;
    const char *q = p;
    for (; *q; q++) {
        if (*q == '{') depth++;
        else if (*q == '}' && --depth == 0) break;
    }
    if (!*q) return NULL;
    size_t len = (size_t)(q - p + 1);
    char *out = malloc(len + 1);
    memcpy(out, p, len);
    out[len] = '\0';
    return out;
}

static const char *at(const char *hay, const char *needle) {
    return hay ? strstr(hay, needle) : NULL;
}

int main(void) {
    char *src = read_file(DEBUG_C_PATH);
    char *server = read_file(SERVER_C_PATH);
    CHECK(src != NULL && server != NULL);
    if (!src || !server) return 1;

    char *attach = body(src, "debug_attach_handle_svc");
    char *detach = body(src, "debug_detach_handle");
    char *disp = body(src, "debug_handle");
    CHECK(attach && detach && disp);

    // PT_CONTINUE only after a reported stop; timeout is a failure.
    const char *stopped = at(attach, "attach_wait_stopped(attach_status)");
    const char *timeout = at(attach, "wr <= 0 ||");
    const char *cont = at(attach, "kern_ptrace(PT_CONTINUE");
    CHECK(stopped && timeout && cont && timeout < cont && stopped < cont);

    // The detach result is checked and a failed detach keeps the pid.
    CHECK(at(attach, "if (kern_ptrace(PT_DETACH, (int)pid, 0, 0) < 0) {\n"
                     "            g_orphan_pid = (int)pid;"));

    // A kept pid is not a session: only the successful attach sets one up.
    int sets = 0;
    for (const char *p = src; (p = strstr(p, "g_debug_ctx.attached = 1")); p++) sets++;
    CHECK(sets == 1);
    // ...and no line copies the kept pid into the session state.
    for (const char *line = src; line && *line; ) {
        const char *end = strchr(line, '\n');
        size_t len = end ? (size_t)(end - line) : strlen(line);
        char buf[512];
        if (len >= sizeof(buf)) len = sizeof(buf) - 1;
        memcpy(buf, line, len);
        buf[len] = '\0';
        // Forbidden: "g_debug_ctx... = ...g_orphan_pid" (kept pid -> session).
        const char *ctx = strstr(buf, "g_debug_ctx");
        const char *eq = ctx ? strstr(ctx, " = ") : NULL;
        CHECK(!(eq && strstr(eq, "g_orphan_pid")));
        line = end ? end + 1 : NULL;
    }

    // Every session command still requires an attached session.
    int handlers = 0;
    for (const char *p = disp; (p = strstr(p, "return debug_")); ) {
        p += strlen("return ");
        char name[96];
        int i = 0;
        while (p[i] && p[i] != '(' && i < (int)sizeof(name) - 1) { name[i] = p[i]; i++; }
        name[i] = '\0';
        if (!strcmp(name, "debug_process_stop_handle") ||   // plain kill
            !strcmp(name, "debug_attach_handle")) continue; // stub
        char *b = body(src, name);
        if (!b || !strstr(b, "!g_debug_ctx.attached")) {
            fprintf(stderr, "  no attached check in %s\n", name);
            failures++;
        }
        handlers++;
        free(b);
    }
    CHECK(handlers >= 18);

    // ATTACH: the pending detach is retried and PT_ATTACH done under the lock.
    const char *lock = at(attach, "scePthreadMutexLock(&g_debug_mutex)");
    const char *retry = at(attach, "debug_retry_orphan()");
    const char *busy = at(attach, "g_debug_ctx.attached || g_orphan_pid");
    const char *ptattach = at(attach, "kern_ptrace(PT_ATTACH");
    CHECK(lock && retry && busy && ptattach && lock < retry && retry < busy && busy < ptattach);

    // DETACH: retries the pending detach, all under the lock.
    CHECK(at(detach, "scePthreadMutexLock(&g_debug_mutex)") == detach + 6);
    CHECK(at(detach, "debug_retry_orphan()"));

    // The retry passes errno, so only ECHILD drops a failed wait4's pid.
    CHECK(strstr(src, "attach_wait_gone(r, status, errno)"));

    // Teardown: a failed PT_DETACH keeps the pid instead of forgetting it,
    // and the record happens before the session state is cleared.
    const char *td = strstr(src, "\nvoid debug_full_teardown(");
    const char *td_keep = td ? strstr(td, "if (kern_ptrace(PT_DETACH, g_debug_ctx.pid, 0, 0) < 0) {\n"
                                          "            g_orphan_pid = (int)g_debug_ctx.pid;") : NULL;
    const char *td_clear = td ? strstr(td, "memset(&g_debug_ctx, 0, sizeof(g_debug_ctx));") : NULL;
    CHECK(td_keep && td_clear && td_keep < td_clear);

    // Every teardown caller holds g_debug_mutex: DETACH (above), the event
    // dispatcher (called under the lock in server.c) and client disconnect.
    const char *fc = strstr(server, "\nvoid free_client(");
    const char *fc_lock = fc ? strstr(fc, "scePthreadMutexLock(&g_debug_mutex);\n        debug_full_teardown(svc);\n"
                                          "        scePthreadMutexUnlock(&g_debug_mutex);") : NULL;
    CHECK(fc_lock != NULL);
    int dispatch_calls = 0, dispatch_locked = 0;
    for (const char *p = server; (p = strstr(p, "dispatch_debug_events()")); p++) {
        dispatch_calls++;
        const char *line = p;
        while (line > server && line[-1] != '\n') line--;
        const char *prev = line - 1;
        while (prev > server && prev[-1] != '\n') prev--;
        if (strstr(prev, "scePthreadMutexLock(&g_debug_mutex)") &&
            strstr(prev, "scePthreadMutexLock(&g_debug_mutex)") < p) dispatch_locked++;
    }
    CHECK(dispatch_calls == 2 && dispatch_locked == dispatch_calls);

    free(attach);
    free(detach);
    free(disp);
    free(src);
    free(server);

    if (failures) {
        fprintf(stderr, "%d test(s) failed\n", failures);
        return 1;
    }
    printf("test_attach_guards: all tests passed\n");
    return 0;
}
