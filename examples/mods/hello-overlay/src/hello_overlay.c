/* hello-overlay: a code mod that draws a small box with the virtual VBlank counter over the game
 * (docs/modding/tutorials/05-an-overlay.md). It changes nothing in the game: it only draws. */
#include "b3mod_api.h"

#include <stdio.h>

static const B3ModApi* api;
static B3ModSelf* self;
static uint32_t badge;   /* a texture: an 8 x 8 orange and white checker board */

/* Every game frame: queue this frame's 2D (shown from the next frame on, over the game's HUD). */
static void draw(void* user) {
    char text[40];
    (void)user;
    snprintf(text, sizeof text, "VBLANK %llu", (unsigned long long)api->vblank());
    api->quad(self, 20, 20, 190, 28, B3MOD_RGBA(0, 0, 0, 170), 0);           /* the box */
    api->quad(self, 20, 20, 5, 28, B3MOD_RGBA(255, 150, 0, 255), 0);         /* an orange edge */
    api->text(self, 33, 28, 12, B3MOD_RGBA(255, 255, 255, 255), text);       /* the counter, 12 units high */
    if (badge) api->quad(self, 188, 26, 16, 16, B3MOD_RGBA(255, 255, 255, 255), badge);   /* the texture, as it is */
}

B3MOD_DECLARE_VERSION()

B3MOD_EXPORT int b3mod_main(const B3ModApi* a, B3ModSelf* s) {
    uint8_t px[8 * 8 * 4];
    int x, y;
    api = a;
    self = s;
    for (y = 0; y < 8; ++y)
        for (x = 0; x < 8; ++x) {
            uint8_t* p = px + (y * 8 + x) * 4;
            const int orange = (x + y) % 2;
            p[0] = 255;
            p[1] = orange ? 150 : 255;
            p[2] = orange ? 0 : 255;
            p[3] = 255;
        }
    badge = api->texture(self, 8, 8, px);
    if (!api->on_frame(self, draw, NULL)) return 1;
    api->log(self, B3MOD_LOG_INFO, "drawing the VBlank counter in the top left corner");
    return 0;
}
