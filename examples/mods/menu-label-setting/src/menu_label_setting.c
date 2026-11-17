/* menu-label-setting: a code mod with a settings row (docs/modding/tutorials/03-a-settings-row.md). PC SETTINGS > MODS
 * gets a row QUICK RACE LABEL (Off / On); while it is On, the main menu's SINGLE EVENT reads QUICK RACE.
 *
 * How: the game asks the string lookup func_00271A00(string table, id) for every text it shows and draws the UTF-16
 * text at the address it returns. This mod wraps that function: for string 230 (SINGLE EVENT, as `b3modtool strings
 * export` lists GLOBALUS.BIN) with the row On it returns its own text, kept in guest memory it allocated; every other
 * call goes on to the game's lookup unchanged. The same technique as the port's own date format setting. */
#include "b3mod_api.h"

#include <stdint.h>

#define STRING_LOOKUP 0x00271A00u   /* func_00271A00 (a0: the string table, a1: the string id) -> v0: UTF-16 text */
#define SINGLE_EVENT 230u           /* GLOBALUS.BIN string 230 "SINGLE EVENT" */

static const B3ModApi* api;
static B3ModSelf* self;
static uint32_t text_addr;          /* our "QUICK RACE" in guest memory (UTF-16) */

static void lookup(B3ModHookCtx* ctx, const B3ModRegs* regs, B3ModResult* result, void* user) {
    (void)user;
    if ((uint32_t)regs->gpr[1] == SINGLE_EVENT && api->get_setting(self, "quick_race") == 1) {
        result->v0 = text_addr;     /* the game draws our text instead */
        return;
    }
    api->next(ctx, regs, result);   /* everything else: the game's own lookup */
}

B3MOD_DECLARE_VERSION()

B3MOD_EXPORT int b3mod_main(const B3ModApi* a, B3ModSelf* s) {
    static const char text[] = "QUICK RACE";
    uint16_t utf16[sizeof text];
    unsigned i;
    B3ModSetting row = {sizeof(B3ModSetting), B3MOD_SETTING_TOGGLE, "quick_race", "Quick race label",
                        "SINGLE EVENT on the main menu reads QUICK RACE.", 0, 0, 0};
    api = a;
    self = s;
    if (!api->add_setting(self, &row)) return 1;
    text_addr = api->alloc(self, sizeof utf16, 16);
    if (!text_addr) return 2;
    for (i = 0; i < sizeof text; ++i) utf16[i] = (uint16_t)(unsigned char)text[i];   /* ASCII to UTF-16, with the 0 */
    if (!api->write(self, text_addr, utf16, sizeof utf16)) return 3;
    if (!api->wrap(self, STRING_LOOKUP, lookup, 0)) return 4;
    return 0;
}
