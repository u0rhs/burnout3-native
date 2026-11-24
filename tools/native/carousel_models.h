// carousel_models.h -- paint3 (2026-11-22): which model each visible car of the car pages' carousel stands for, when its
// picture is not its own. Read only: the carousel's slot objects.
//
// What the game does [C the decomp; car_select.h has the page]: the carousel's draw func_0042DAE0 builds, for up to four
// visible rows (from row +0x240 - 1), one 2D object each and stores it at CB3CarouselComponent 0x01E8EC30 +0x1EC + 4 x
// slot (0 for a slot past the rows); func_00427080 prepares it with the row's picture and keeps, in the object's inline
// data at +0x20, the picture (+0x28: the raster GetCarTexture gave), the class (+0x2F, EB3MenuCarClasses), the car
// index in its class (+0x30) and the paint (+0x31). The slots run down the carousel in row order.
//
// The renderer draws a car page's thumbnail from the model the picture's name gives ("COMPCAR3": PVEH/COMP/CAR3.BGV,
// view.cpp render_car_picture). A row whose picture stands in for another car's (a series of traffic vehicles shows its
// donor race car's picture: branch trafficseries) needs the model said apart: a resolver gives the model's name for a
// (class, index) ("" for a race car's row: its picture is its own), and the slots it names are published (host record
// 'HCM1', gameview_native.h set_carousel_models): the renderer pairs each with a draw of its picture by their order down
// the screen. No resolver (dev): nothing is published.
#ifndef B3NATIVE_CAROUSEL_MODELS_H
#define B3NATIVE_CAROUSEL_MODELS_H

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "car_select.h"

namespace b3native {

constexpr std::uint32_t kCarouselSlots = kCarousel + 0x1ECu;   // [C] four 2D objects, the visible rows' (0: none)
constexpr int kCarouselSlotCount = 4;

struct CarouselSlot {
    std::uint32_t raster = 0;     // the picture the row draws (obj +0x28)
    int cls = 0, index = 0, paint = 0;
    int slot = 0;                 // 0..3, down the carousel
    std::string model;            // the resolver's name for it ("COMPCAR18")
    bool paintable = false;       // the player's paint applies (as on the car on show and in the race: a series' driven cars)
};

// The resolver: the model of a row's car, "" when its picture is its own (a race car). `paintable`: the player's paint
// applies to it.
using CarouselResolver = std::function<std::string(int cls, int index, bool& paintable)>;
inline CarouselResolver& carousel_resolver() {
    static CarouselResolver r;
    return r;
}
// A series' resolver added in front of those already set (each series answers for its own classes and returns "" for the
// others, which then go on to the earlier resolvers): the TRAFFIC series (traffic_cars.h), CUSTOM / BETA (extra_series.h)
inline void chain_carousel_resolver(CarouselResolver r) {
    if (!r) return;
    CarouselResolver prev = std::move(carousel_resolver());
    carousel_resolver() = [r = std::move(r), prev = std::move(prev)](int cls, int index, bool& paintable) -> std::string {
        bool p = false;
        std::string m = r(cls, index, p);
        if (!m.empty()) {
            paintable = p;
            return m;
        }
        return prev ? prev(cls, index, paintable) : std::string();
    };
}

// The visible slots the resolver names, in slot order. `load32` / `load8` read guest memory.
template <class Load32, class Load8>
std::vector<CarouselSlot> carousel_slots(Load32 load32, Load8 load8, const CarouselResolver& resolve) {
    std::vector<CarouselSlot> out;
    if (!resolve) return out;
    for (int k = 0; k < kCarouselSlotCount; ++k) {
        const std::uint32_t obj = load32(kCarouselSlots + 4u * std::uint32_t(k));
        if (obj == 0 || obj >= 0x02000000u) continue;
        CarouselSlot s;
        s.slot = k;
        s.raster = load32(obj + 0x28u);
        s.cls = int(load8(obj + 0x2Fu));
        s.index = int(load8(obj + 0x30u));
        s.paint = int(load8(obj + 0x31u));
        s.model = resolve(s.cls, s.index, s.paintable);
        if (!s.model.empty() && s.raster) out.push_back(std::move(s));
    }
    return out;
}

// The host record's bytes ('HCM1': u32 magic, serial, count, 0; then per slot 32 bytes: u32 raster, u8 class, index,
// paint, flags (bit 0 paintable), char model[20], u32 slot)
inline std::vector<std::uint8_t> carousel_record(const std::vector<CarouselSlot>& slots, std::uint32_t serial) {
    std::vector<std::uint8_t> b(16 + 32 * slots.size(), 0);
    const std::uint32_t head[4] = {0x314D4348u, serial, std::uint32_t(slots.size()), 0u};   // 'HCM1'
    std::memcpy(b.data(), head, 16);
    for (std::size_t i = 0; i < slots.size(); ++i) {
        std::uint8_t* e = b.data() + 16 + 32 * i;
        std::memcpy(e, &slots[i].raster, 4);
        e[4] = std::uint8_t(slots[i].cls);
        e[5] = std::uint8_t(slots[i].index);
        e[6] = std::uint8_t(slots[i].paint);
        e[7] = slots[i].paintable ? 1 : 0;
        std::memcpy(e + 8, slots[i].model.data(), std::min<std::size_t>(slots[i].model.size(), 19));
        const std::uint32_t sl = std::uint32_t(slots[i].slot);
        std::memcpy(e + 28, &sl, 4);
    }
    return b;
}

}  // namespace b3native

#endif
