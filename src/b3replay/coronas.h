// b3replay coronas (corona1, docs/native-renderer.md §16): the game's three corona arrays rebuilt from a capture's
// corona inputs, and drawn the way the corona VU1 program 0x478260 draws them (b3asset/coronashade.h), at the displayed
// time. Shared by the offline replay and the live renderer through View.
//
// What fills the arrays, per frame, in this order [C asm / matched units; L drafts where marked]:
//   CB3TrafficSystem::Draw 0x3078F0 [L draft]: each drawn traffic vehicle, then each drawn crashed vehicle (reverse),
//     RenderTrafficImmediateEffects 0x2621A0 -> RenderLightCoronas 0x256690 when its +0x58 light bits are nonzero; then
//     the traffic lights' coronas and the unassigned ones (AddTrafficCorona 0x256FD0)                    -> array 0
//   CB3World::RenderRaceCars 0x199A20 (far to near) -> CB3RaceCar::Draw 0x151A40 -> ActuallyRenderRaceCarImmediateEffects
//     0x2621D0: RenderLightCoronas (rear indicators red per +0x18F0: func_00257080)                     -> array 0
//     and func_00257230, the boost glow: per exhaust item three slots (w 800 / 400 / 200)       -> array 1, or 2 (red)
// Slots past an array's capacity (126 / 72 / 72) are dropped, as the game's cursor test drops them.
#ifndef B3REPLAY_CORONAS_H
#define B3REPLAY_CORONAS_H

#include <cstdint>
#include <string>
#include <vector>

#include "b3gfx/b3gfx.h"
#include "capture.h"

namespace b3replay {

// Who wrote a slot (for --info and the check).
enum class CoronaSource : std::uint8_t { kTrafficLight, kCrashedLight, kSignal, kUnassigned, kRaceLight, kBoost };

struct CoronaArrays {
    std::vector<CoronaSlot> slot[3];
    std::vector<CoronaSource> src[3];
    int dropped[3] = {};          // slots the game would have dropped (array full)
    int boost_rotations = 0;      // gEffectsRandomiserScales rotations of the frame (3 per exhaust item of a boosting car)
};

// The arrays for one frame. `vehicles` / `cam_point` / `cam_pos`: the displayed (interpolated) vehicles and camera; the
// rest (light states, boost intensity, the seed) from the tick. `cam_point` = the CGtCamera position - 2 x its at axis
// (func_002622A0), `cam_pos` = the CGtCamera position (traffic lights measure from it). `fades` (traffic-popin,
// optional): per vehicle, the fade its car is drawn with (view.h traffic_fade); a traffic / crashed vehicle's lights
// are scaled by it, so they come and go with the car.
void build_coronas(const CoronaInputs& in, const std::vector<Vehicle>& vehicles, const float cam_point[3], const float cam_pos[3],
                   CoronaArrays& out, const std::vector<float>* fades = nullptr, const std::int64_t* flame = nullptr);
// (paint2: `flame`, optional, two entries: player 1's and player 2's boost glow colour, 0xRRGGBB, or -1 the game's; the
// glow keeps the game's brightness in that colour)

// The VU program's sprite for a slot seen from `cam`, as a world-space quad the GPU projects with the frame's camera
// (float model of b3asset::corona_sprites; §16 compares the two): false when the program drops the slot.
struct CoronaQuad {
    float corner[4][3] = {};      // top left, top right, bottom right, bottom left (UV (0,0), (1,0), (1,1), (0,1))
    std::uint8_t rgb[3] = {};     // FTOI0 of the faded colour, low 8 bits
    float depth = 0;              // view depth of the slot
    std::uint8_t alpha = 0x80;    // weather (rain.h QuadLayer's mist: the spray): the blend's As (0x80 = 1)
};
// `aspect` > 0: the render's width / height when it is wider than the game's view window (Hor+): the program's window
// test (the screen plus 32 of 640 pixels each side) then spans the wider screen; K (the fade distance scale) stays the
// game's 1 / half_w.
bool corona_quad(const Camera& cam, const CoronaSlot& s, CoronaQuad& q, float aspect = 0);

struct CoronaStats { int slots[3] = {}, sprites[3] = {}; };

// GPU side: the three arrays' sprites as additive post-fog effect draws ("coronaglow", "coronaboost",
// "coronaboostred" from GLOBAL.TXD; ALPHA_1 0x68 with FIX 0x80, TEST_1 0x53001 (Z GEQUAL, RGB only), no Z write, no fog).
class Coronas {
public:
    void clear() { for (auto& b : vb_) b = b3gfx::kNone; for (auto& b : ib_) b = b3gfx::kNone; }   // (draw also drops them after a clear_resources)
    void draw(b3gfx::Device& dev, const Camera& cam, const CoronaArrays& arrays, const b3gfx::TextureId tex[3],
              std::vector<b3gfx::DrawItem>& out, CoronaStats* stats = nullptr, float aspect = 0);
    // nightfix (night only): more sprites drawn as array 0's are (the same texture and state, night_glow), from quads
    // made by the caller (View::night_lamp_coronas: the street lamps' heads, the billboards' floodlights)
    void draw_quads(b3gfx::Device& dev, const std::vector<CoronaQuad>& quads, b3gfx::TextureId tex, std::vector<b3gfx::DrawItem>& out);

private:
    void emit(b3gfx::Device& dev, int slot, b3gfx::TextureId tex, std::vector<b3gfx::DrawItem>& out);
    // [3]: draw_quads' buffers
    b3gfx::BufferId vb_[4] = {b3gfx::kNone, b3gfx::kNone, b3gfx::kNone, b3gfx::kNone}, ib_[4] = {b3gfx::kNone, b3gfx::kNone, b3gfx::kNone, b3gfx::kNone};
    unsigned epoch_ = 0;   // Device::resource_epoch the buffer ids belong to
    std::vector<b3gfx::Vertex> verts_;
    std::vector<std::uint32_t> idx_;
};

}  // namespace b3replay

#endif
