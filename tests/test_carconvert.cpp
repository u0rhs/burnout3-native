// test_carconvert -- betaimport: b3asset::convert_prototype_car (include/b3asset/carconvert.h) on synthetic files, no
// game data. The prototype file is the synthetic game car (tests/synthetic_car.h) written in the old layout: 16 bytes
// fewer before the first LOD block (0x16E0), version 0x15 (LOD block list offsets from the block) or 0x14 (from the
// file's start). The donor is the synthetic car with two engine-sound structures appended (entries and sub-entries at
// signed offsets from themselves, as func_002FB240 fixes them up). The converted file must read as the game car does
// (every vertex, index and texel), carry the donor's sound bytes with +0x16DC / +0x16E0 at them, and every cut or damaged
// input must be refused with a reason, never read past (built with AddressSanitizer).
#include "b3asset/car.h"
#include "b3asset/carconvert.h"
#include "b3asset/gs_memory.h"
#include "synthetic_car.h"
#include "test.h"

#include <cstring>
#include <string>

using b3asset::Bytes;
using namespace synth;

namespace {

bool same_model(const Buf& a, const Buf& b) {
    for (int lod = 3; lod < 4; ++lod)   // (the synthetic car has LOD 3 only)
        for (int pal = 0; pal < 2; ++pal) {
            b3asset::GsMemory gs1, gs2;
            b3asset::Model m1, m2;
            b3asset::CarOptions o;
            o.lod = lod;
            o.palette = pal;
            std::string w1, w2;
            if (!b3asset::read_car(Bytes{a.data(), a.size()}, o, gs1, m1, &w1)) return std::printf("  a: %s\n", w1.c_str()), false;
            if (!b3asset::read_car(Bytes{b.data(), b.size()}, o, gs2, m2, &w2)) return std::printf("  b: %s\n", w2.c_str()), false;
            if (m1.vertices.size() != m2.vertices.size() || m1.indices != m2.indices) return std::printf("  geometry\n"), false;
            for (std::size_t i = 0; i < m1.vertices.size(); ++i)
                if (std::memcmp(&m1.vertices[i], &m2.vertices[i], sizeof m1.vertices[i]) != 0) return false;
            if (m1.textures.size() != m2.textures.size() || m1.textures.empty()) return false;
            if (m1.textures[0].levels.size() != m2.textures[0].levels.size()) return false;
            for (std::size_t l = 0; l < m1.textures[0].levels.size(); ++l)
                if (m1.textures[0].levels[l].rgba != m2.textures[0].levels[l].rgba) return false;
        }
    return true;
}

std::string conv(const Buf& old, const Buf& donor, Buf& out, b3asset::CarConvertInfo* info = nullptr) {
    return b3asset::convert_prototype_car(Bytes{old.data(), old.size()}, Bytes{donor.data(), donor.size()}, out, info);
}

}  // namespace

TEST(both_versions_convert_to_the_game_car) {
    const Buf game = synthetic_car(), donor = donor_car();
    for (std::uint32_t v : {0x14u, 0x15u}) {
        const Buf old = old_car(v);
        CHECK(b3asset::car_is_prototype(Bytes{old.data(), old.size()}));
        CHECK(!b3asset::car_file_error(Bytes{old.data(), old.size()}).empty());   // the readers refuse it as it is
        Buf out;
        b3asset::CarConvertInfo inf;
        const std::string why = conv(old, donor, out, &inf);
        CHECK(why.empty());
        if (!why.empty()) {
            std::printf("  0x%X: %s\n", v, why.c_str());
            continue;
        }
        CHECK(b3asset::car_file_error(Bytes{out.data(), out.size()}).empty());
        CHECK(!b3asset::car_is_prototype(Bytes{out.data(), out.size()}));
        CHECK(g32(out, 0) == 0x17 && g32(out, 8) == out.size() && out.size() % 0x800 == 0);
        CHECK(g32(out, 0x4C + 4 * 3) == 0x16E0 + b3asset::kCarConvertShift);
        CHECK(g32(out, 0x16E0 + b3asset::kCarConvertShift) == 0x48);   // the list offsets from the block (0x14 too)
        CHECK(inf.version == v && inf.lod_blocks == 1 && inf.lists == 1 && inf.packets == 1 && inf.dropped_entries == 0);
        CHECK(same_model(game, out));
        // the donor's sound structures, byte for byte, with +0x16DC / +0x16E0 at them
        const std::size_t from = g32(donor, 0x16DC);
        CHECK(inf.esm_at % 16 == 0 && inf.esm_size == donor.size() - from);
        CHECK(std::memcmp(&out[inf.esm_at], &donor[from], inf.esm_size) == 0);
        CHECK(g32(out, 0x16DC) == inf.esm_at && g32(out, 0x16E0) == inf.esm_at + 0xD0);
        // the padding after the data is not carried over (the result is smaller than the padded prototype file + sound)
        CHECK(inf.data_end < old.size() && out.size() < old.size());
    }
}

TEST(other_files_refused) {
    const Buf donor = donor_car(), game = synthetic_car();
    Buf out;
    CHECK(conv(game, donor, out).find("not a prototype vehicle file") != std::string::npos);   // already the game's
    Buf o = old_car(0x15);
    p32(o, 0, 0x16);
    CHECK(conv(o, donor, out).find("version 0x16") != std::string::npos);
    o = old_car(0x15);
    p32(o, 0x4C + 4 * 3, 0x1700);   // not the prototype layout
    CHECK(conv(o, donor, out).find("not the prototype layout") != std::string::npos);
    CHECK(out.empty());
    // a donor without engine sound (a traffic file), a prototype as donor
    Buf d = donor;
    p32(d, 0x16DC, 0);
    p32(d, 0x16E0, 0);
    CHECK(conv(old_car(0x15), d, out).find("no engine sound") != std::string::npos);
    const Buf old = old_car(0x15);
    CHECK(conv(old, old, out).find("the donor") != std::string::npos);
    // a donor's sub-entry pointing before its sound data
    d = donor;
    const std::size_t ents = g32(d, 0x16DC) + 0x90;
    p32(d, ents + 8, std::uint32_t(-0x1000));
    CHECK(conv(old, d, out).find("sub-entries") != std::string::npos);
}

TEST(stray_list_entry_left_out) {   // a +0x64 list counting a second "entry" of stray bytes (one prototype file's)
    Buf o = old_car(0x15);
    // the +0x64 list: a copy of the L0 list's header and entry at the end, with a second entry of junk
    const std::size_t at = 0x7000;
    const std::size_t l0 = 0x16E0 + g32(o, 0x16E0);
    std::memcpy(&o[at], &o[l0], 12);
    o[at] = 2;
    const std::size_t e0 = l0 + g32(o, l0 + 4), e = at + 12;
    p32(o, at + 4, 12);
    p32(o, e, std::uint32_t(e0 + g32(o, e0) - e));   // the same packet
    p32(o, e + 16, 0xFF38C480);                      // junk
    p32(o, 0x64, std::uint32_t(at));
    Buf out;
    b3asset::CarConvertInfo inf;
    CHECK(conv(o, donor_car(), out, &inf).empty());
    CHECK(inf.dropped_entries == 1);
    CHECK(g32(out, 0x64) == at + b3asset::kCarConvertShift);
    CHECK(out.size() > at + b3asset::kCarConvertShift && out[at + b3asset::kCarConvertShift] == 1);
    // in a LOD block's list the same junk fails the file
    o = old_car(0x15);
    p32(o, e0, 0x7FFF0000);
    CHECK(conv(o, donor_car(), out).find("outside the file") != std::string::npos);
}

TEST(a_length_not_a_multiple_of_16) {   // a file cut a few bytes after its last structure: the rest read as zeros
    Buf o = old_car(0x15);
    std::size_t end = o.size();
    while (end > 0 && !o[end - 1]) --end;
    for (std::size_t extra : {1u, 7u, 15u}) {
        Buf cut(o.begin(), o.begin() + std::ptrdiff_t(end + extra));
        Buf out;
        b3asset::CarConvertInfo inf;
        CHECK(conv(cut, donor_car(), out, &inf).empty());
        CHECK(inf.data_end % 16 == 0 && inf.data_end >= end);
        CHECK(b3asset::car_file_error(Bytes{out.data(), out.size()}).empty());
    }
}

TEST(too_big_for_the_car_memory) {
    Buf o = old_car(0x15);
    o.resize(b3asset::kCarMemoryBlock, 0);
    o.back() = 1;   // data up to the end (kept), so the sound no longer fits
    p32(o, 0x08, std::uint32_t(o.size()));
    Buf out;
    CHECK(conv(o, donor_car(), out).find("too big") != std::string::npos);
    CHECK(out.empty());
}

TEST(cut_inputs_refused) {   // every length of either file: a reason, never a read past it (AddressSanitizer)
    const Buf old = old_car(0x14), donor = donor_car();
    Buf out;
    int refused = 0, n = 0;
    for (std::size_t len = 0; len <= old.size(); len += (len < 0x1A00 ? 1 : 64)) {
        const Buf cut(old.begin(), old.begin() + std::ptrdiff_t(len));
        Buf exact(cut);   // a heap block of exactly that size
        const std::string why = conv(exact, donor, out);
        ++n;
        if (!why.empty()) ++refused;
        else CHECK(b3asset::car_file_error(Bytes{out.data(), out.size()}).empty());
    }
    CHECK(refused > 0 && n > refused);   // short ones refused, those that keep all the data convert
    const std::size_t sound_end = g32(donor, 0x16E0) + 0x44;   // structure B's end (the last of the sound data)
    for (std::size_t len = 0; len < donor.size(); ++len) {
        Buf cut(donor.begin(), donor.begin() + std::ptrdiff_t(len));
        const std::string why = conv(old, cut, out);
        if (len < sound_end) CHECK(!why.empty());   // the sound's end cut off: refused
        else CHECK(why.empty() && b3asset::car_file_error(Bytes{out.data(), out.size()}).empty());
    }
}

TEST_MAIN("betaimport: prototype vehicle files converted to the game's layout (carconvert)")
