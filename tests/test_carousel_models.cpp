// paint3 (tools/native/carousel_models.h): the carousel slots whose picture is not their own car's -- the slots read from
// the carousel's 2D objects (picture, class, index, paint), only those the resolver names, and the host record's bytes.
#include "test.h"

#include "carousel_models.h"

#include <cstdint>
#include <cstring>
#include <map>
#include <string>

namespace {

struct Ram {
    std::map<std::uint32_t, std::uint8_t> b;
    void put32(std::uint32_t a, std::uint32_t v) { for (int k = 0; k < 4; ++k) b[a + std::uint32_t(k)] = std::uint8_t(v >> (8 * k)); }
    std::uint32_t load32(std::uint32_t a) const {
        std::uint32_t v = 0;
        for (int k = 0; k < 4; ++k) v |= std::uint32_t(load8(a + std::uint32_t(k))) << (8 * k);
        return v;
    }
    std::uint8_t load8(std::uint32_t a) const { const auto i = b.find(a); return i == b.end() ? 0 : i->second; }
    void object(int slot, std::uint32_t obj, std::uint32_t raster, int cls, int index, int paint) {
        put32(b3native::kCarouselSlots + 4u * std::uint32_t(slot), obj);
        put32(obj + 0x28u, raster);
        b[obj + 0x2Fu] = std::uint8_t(cls);
        b[obj + 0x30u] = std::uint8_t(index);
        b[obj + 0x31u] = std::uint8_t(paint);
    }
};

std::vector<b3native::CarouselSlot> read(const Ram& m, const b3native::CarouselResolver& r) {
    return b3native::carousel_slots([&](std::uint32_t a) { return m.load32(a); }, [&](std::uint32_t a) { return m.load8(a); }, r);
}

const b3native::CarouselResolver kTraffic = [](int cls, int index, bool& paintable) -> std::string {
    if (cls != 7) return std::string();
    paintable = false;
    return "COMPCAR" + std::to_string(12 + index);
};

TEST(addresses) {
    CHECK(b3native::kCarouselSlots == 0x01E8EC30u + 0x1ECu);
}

TEST(slots_the_resolver_names) {
    Ram m;
    m.object(0, 0x01E92F80u, 0x00D362C0u, 6, 4, 1);   // a race car's row: its own picture
    m.object(1, 0x01E92F30u, 0x00D40FC0u, 7, 1, 0);   // traffic rows on their donors' pictures
    m.object(2, 0x01E928F0u, 0x00D362C0u, 7, 6, 2);
    // slot 3: none (0)
    const auto s = read(m, kTraffic);
    CHECK(s.size() == 2);
    CHECK(s[0].slot == 1 && s[0].model == "COMPCAR13" && s[0].raster == 0x00D40FC0u && s[0].cls == 7 && s[0].index == 1);
    CHECK(s[1].slot == 2 && s[1].model == "COMPCAR18" && s[1].paint == 2 && !s[1].paintable);
    CHECK(read(m, b3native::CarouselResolver{}).empty());   // no resolver (dev): none
}

TEST(chained_resolvers) {
    b3native::carousel_resolver() = nullptr;
    b3native::chain_carousel_resolver(kTraffic);   // classes 7: traffic
    b3native::chain_carousel_resolver([](int cls, int index, bool& paintable) -> std::string {   // class 10: a custom series
        if (cls != 10) return std::string();
        paintable = true;
        return "XCUSCAR" + std::to_string(index + 1);
    });
    bool p = true;
    CHECK(b3native::carousel_resolver()(7, 2, p) == "COMPCAR14" && !p);
    p = false;
    CHECK(b3native::carousel_resolver()(10, 0, p) == "XCUSCAR1" && p);
    p = true;
    CHECK(b3native::carousel_resolver()(3, 0, p).empty());   // a race car's own picture: none
    b3native::carousel_resolver() = nullptr;
}

TEST(record_bytes) {
    Ram m;
    m.object(3, 0x01E92D00u, 0x00F4F440u, 7, 9, 3);
    const auto s = read(m, kTraffic);
    const std::vector<std::uint8_t> r = b3native::carousel_record(s, 5);
    CHECK(r.size() == 16 + 32);
    std::uint32_t head[4];
    std::memcpy(head, r.data(), 16);
    CHECK(std::memcmp(r.data(), "HCM1", 4) == 0 && head[1] == 5 && head[2] == 1);
    std::uint32_t raster = 0, slot = 0;
    std::memcpy(&raster, r.data() + 16, 4);
    std::memcpy(&slot, r.data() + 16 + 28, 4);
    CHECK(raster == 0x00F4F440u && slot == 3);
    CHECK(r[20] == 7 && r[21] == 9 && r[22] == 3 && r[23] == 0);
    CHECK(std::string(reinterpret_cast<const char*>(r.data() + 24)) == "COMPCAR21");
    CHECK(b3native::carousel_record({}, 0).size() == 16);
}

}  // namespace

TEST_MAIN("the carousel's cars whose picture is not their own (paint3)")
