/* Test code mods for tests/test_code_mods.cpp (sdk2b): one source, built per MODE into build/testmods/<name>.so with
 * the SDK header alone. Synthetic addresses only (the test's rows), no game data. */
#include "b3mod_api.h"

#include <string.h>

#ifndef MODE
#define MODE 1
#endif
#define ROW_A 0x00100000u    /* the test's rows (tests/test_code_mods.cpp) */
#define ROW_B 0x00100010u
#define ROW_PLAT 0x00100020u
#define NO_ROW 0x00100030u

static const B3ModApi* g_api;
static B3ModSelf* g_self;
static int g_vblanks;
static uint32_t g_mem;

static void replace_a(B3ModHookCtx* ctx, const B3ModRegs* regs, B3ModResult* result, void* user) {
    (void)ctx; (void)user;
    result->v0 = regs->gpr[0] * 10 + (uint64_t)g_api->get_setting(g_self, "flag");   /* a0 * 10 + the setting */
}
static void wrap_b(B3ModHookCtx* ctx, const B3ModRegs* regs, B3ModResult* result, void* user) {
    B3ModRegs r = *regs;
    r.gpr[0] += 1;
    g_api->next(ctx, &r, result);
    result->v0 += (uint64_t)(uintptr_t)user;
}
static void vblank(void* user) {
    (void)user;
    ++g_vblanks;
    if (MODE == 5 && g_vblanks == 1) g_api->wrap(g_self, ROW_B, wrap_b, 0);   /* too late: refused, logged */
}
static void frame(void* user) {
    char t[32];
    (void)user;
    g_api->quad(g_self, 10, 20, 30, 40, B3MOD_RGBA(255, 0, 0, 128), 0);
    t[0] = 'V';
    t[1] = (char)('0' + g_vblanks % 10);
    t[2] = 0;
    g_api->text(g_self, 12, 22, 8, B3MOD_RGBA(255, 255, 255, 255), t);
}

#if MODE != 7
#if MODE == 6
B3MOD_EXPORT unsigned b3mod_built_for(void) { return 99; }
#else
B3MOD_DECLARE_VERSION()
#endif
#endif

B3MOD_EXPORT int b3mod_main(const B3ModApi* api, B3ModSelf* self) {
    static const char* const speeds[] = {"Slow", "Fast", "Faster"};
    B3ModSetting flag = {sizeof(B3ModSetting), B3MOD_SETTING_TOGGLE, "flag", "Test flag", "A toggle.", 0, 0, 0};
    B3ModSetting speed = {sizeof(B3ModSetting), B3MOD_SETTING_CHOICE, "speed", "Speed", 0, speeds, 3, 1};
    g_api = api;
    g_self = self;
    if (api->version != B3MOD_API_VERSION || api->size < sizeof(B3ModApi)) return 1;
    switch (MODE) {
    case 1:   /* good: everything */
        if (!api->replace(self, ROW_A, replace_a, 0) || !api->wrap(self, ROW_B, wrap_b, (void*)(uintptr_t)5)) return 2;
        if (!api->add_setting(self, &flag) || !api->add_setting(self, &speed)) return 3;
        g_mem = api->alloc(self, 64, 16);
        if (!g_mem || !api->write(self, g_mem, "hello", 6)) return 4;
        if (!api->on_vblank(self, vblank, 0) || !api->on_frame(self, frame, 0)) return 5;
        api->log(self, B3MOD_LOG_INFO, "good mod loaded");
        return 0;
    case 2:   /* conflict: replaces A too; its memory and its row must not survive */
        api->alloc(self, 128, 16);
        api->add_setting(self, &flag);
        api->on_vblank(self, vblank, 0);
        api->write(self, 0x000C9000u, "x", 1);
        return api->replace(self, ROW_A, replace_a, 0) ? 0 : 9;
    case 3: return 5;   /* b3mod_main refuses */
    case 4: return api->wrap(self, NO_ROW, wrap_b, 0) ? 0 : 0;   /* the refused registration fails the mod by itself */
    case 5: return api->on_vblank(self, vblank, 0) ? 0 : 1;
    case 8: return api->wrap(self, ROW_PLAT, wrap_b, 0) ? 0 : 1;   /* the platform tier */
    case 9: {   /* memory bounds */
        char buf[8];
        if (api->write(self, 0x00200000u, "x", 1)) return 1;                  /* the program's code */
        if (api->read(self, 0x01FFFFFEu, buf, 4)) return 2;                   /* past the end of EE RAM */
        if (!api->read(self, 0x70000000u, buf, 8)) return 3;                  /* the scratchpad */
        if (api->alloc(self, 0, 16) || api->alloc(self, 16, 3)) return 4;     /* refused (and so the mod fails) */
        return 0;
    }
    case 10: {   /* a setting that is not valid */
        B3ModSetting bad = {sizeof(B3ModSetting), B3MOD_SETTING_CHOICE, "Bad Key", "x", 0, speeds, 3, 0};
        return api->add_setting(self, &bad) ? 0 : 0;
    }
    case 11: {   /* data files: its own folder, and the disc through the host */
        char buf[16];
        memset(buf, 0, sizeof buf);
        if (api->data_size(self, "data/hello.txt") != 5) return 1;
        if (api->read_data(self, "data/hello.txt", 1, buf, 8) != 4 || memcmp(buf, "ello", 4) != 0) return 2;
        if (api->data_size(self, "../outside.txt") != -1) return 3;
        if (api->data_size(self, "disc:DATA/TEST.BIN") != 3) return 4;
        if (api->read_data(self, "disc:DATA/TEST.BIN", 0, buf, 3) != 3 || memcmp(buf, "abc", 3) != 0) return 5;
        return 0;
    }
    }
    return 0;
}
