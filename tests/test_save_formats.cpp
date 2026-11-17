// save1 (remaster-features §8): the port's .sav, the PS2 / PCSX2 import formats (src/native/save_formats.h), the save
// folder (src/native/save_store.h) and the card's write-back to the profile file (ProfileCard). Synthetic saves only:
// every image here is built by the test (no game save is read or committed).
#include "pcsx2_cards.h"
#include "save_formats.h"
#include "save_store.h"
#include "test.h"

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>

#include <unistd.h>

using namespace b3native::save;
namespace fs = std::filesystem;

namespace {

// A save as the game's would look (names / sizes of BASLUS-21050's three files, synthetic bytes).
CardSaveDirectory game_like_save(std::uint8_t seed) {
    CardSaveDirectory d;
    d.name = kGameSaveDirectory;
    d.created = card_timestamp_from_unix(1173398683);
    d.modified = card_timestamp_from_unix(1173398700);
    auto file = [&](const char* name, std::size_t size, std::uint32_t mul) {
        CardFile f;
        f.name = name;
        f.created = d.created;
        f.modified = card_timestamp_from_unix(1173398700 + size);
        for (std::size_t i = 0; i < size; ++i) f.content.push_back(static_cast<std::uint8_t>((i * mul + seed) ^ (i >> 7)));
        return f;
    };
    d.files = {file("icon.sys", 964, 3), file("view.ico", 15234, 5), file("BASLUS-21050", 61772, 11)};
    return d;
}

CardSaveDirectory other_save() {
    CardSaveDirectory d;
    d.name = "BESLES-00000";
    CardFile f;
    f.name = "data";
    f.content.assign(3000, 0x5A);
    d.files = {f};
    return d;
}

// mkpsu's synthetic save (the one the embedded .max holds): icon.sys 64 bytes, BASLUS-21050 1500 bytes
CardSaveDirectory max_save() {
    CardSaveDirectory d;
    d.name = kGameSaveDirectory;
    CardFile icon;
    icon.name = "icon.sys";
    for (int i = 0; i < 64; ++i) icon.content.push_back(static_cast<std::uint8_t>(i * 7));
    CardFile save;
    save.name = "BASLUS-21050";
    const char* text = "SYNTHETIC SAVE FOR THE LZARI TEST ";
    for (int i = 0; i < 1500; ++i) save.content.push_back(static_cast<std::uint8_t>(i % 97 < 60 ? text[i % 34] : (i * 13) & 0xFF));
    d.files = {icon, save};
    return d;
}

// A .max of max_save() written by mymc+ 3.0 (`mymcplus card.ps2 export -m`, its LZARI encoder; synthetic content).
const std::uint8_t kMax[] = {
    0x50, 0x73, 0x32, 0x50, 0x6F, 0x77, 0x65, 0x72, 0x53, 0x61, 0x76, 0x65, 0x46, 0x88, 0x89, 0x22, 0x42, 0x41, 0x53, 0x4C, 0x55, 0x53, 0x2D, 0x32,
    0x31, 0x30, 0x35, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x21, 0x02, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x68, 0x06, 0x00, 0x00, 0xCB, 0xA6, 0xF8, 0x6C,
    0x1B, 0x06, 0xA2, 0x4A, 0x6E, 0xB8, 0x09, 0x05, 0x96, 0xBD, 0x21, 0x67, 0xF4, 0x4C, 0x7C, 0xFE, 0x62, 0x09, 0x2F, 0x29, 0x55, 0x96, 0x66, 0x5B,
    0x3F, 0xEE, 0x85, 0x9D, 0x86, 0xA9, 0x0D, 0xF1, 0xE4, 0xB6, 0x64, 0x8F, 0xDF, 0xAF, 0x93, 0x1E, 0xEE, 0x68, 0x15, 0x60, 0xF2, 0xE8, 0x1C, 0xF5,
    0x93, 0x04, 0xA6, 0x0B, 0xEE, 0x0D, 0x52, 0xBE, 0x9C, 0x24, 0xC9, 0x69, 0xF2, 0x7B, 0x85, 0x48, 0xEC, 0x4E, 0x2A, 0xAB, 0x2D, 0xFC, 0xD8, 0x2F,
    0x0A, 0xA9, 0x02, 0x10, 0xF0, 0x23, 0xA2, 0x42, 0xA5, 0xF4, 0x3B, 0xCC, 0x36, 0x1B, 0x27, 0x0C, 0x95, 0x4F, 0xC9, 0xA3, 0x36, 0xE6, 0x93, 0xF7,
    0x39, 0x0B, 0xEF, 0xE3, 0x0E, 0x3C, 0xB7, 0xE1, 0x3F, 0xB6, 0xBB, 0x6A, 0x6D, 0x8B, 0xE7, 0xB4, 0x1C, 0xAD, 0x4F, 0x80, 0xD7, 0x2F, 0xA1, 0x64,
    0xF4, 0x55, 0x15, 0xEB, 0x5C, 0x97, 0x42, 0x1A, 0x06, 0x54, 0xC0, 0xEF, 0xEE, 0x9C, 0x4B, 0x55, 0xA4, 0xFB, 0xE1, 0x5A, 0x1D, 0xAF, 0x19, 0xCC,
    0x8C, 0x2C, 0xB8, 0xFE, 0x13, 0x8C, 0x83, 0xE8, 0x3B, 0xFF, 0x6B, 0xB2, 0x50, 0x9A, 0x73, 0xFC, 0x65, 0x90, 0x1D, 0x1B, 0x0F, 0xF4, 0xDF, 0x08,
    0x8C, 0x08, 0x3A, 0xD3, 0x79, 0x48, 0xF9, 0xC1, 0x69, 0x6F, 0xB6, 0xA8, 0xE2, 0x2F, 0x3B, 0x54, 0x96, 0xB0, 0x8B, 0xEB, 0xA7, 0x57, 0xDD, 0x86,
    0x09, 0x8C, 0xD9, 0xF7, 0x9D, 0x4F, 0x96, 0xB4, 0x35, 0x85, 0xBB, 0x45, 0x12, 0x6E, 0x9A, 0x55, 0xB2, 0x1E, 0x02, 0x9E, 0x89, 0x1E, 0x45, 0xD3,
    0x32, 0x5C, 0xE2, 0x8A, 0x1F, 0x8D, 0x09, 0xC5, 0xD2, 0xAE, 0x6C, 0x9A, 0x3B, 0xC6, 0x48, 0x29, 0x96, 0x6D, 0x12, 0x62, 0x19, 0x13, 0x6C, 0x4C,
    0xCA, 0x41, 0x94, 0x9E, 0xD7, 0x7C, 0xE3, 0xB0, 0xEB, 0x88, 0xF2, 0xD9, 0x71, 0xAC, 0x35, 0x91, 0x69, 0xB5, 0xA2, 0x94, 0x92, 0xCD, 0x46, 0x1B,
    0x6F, 0xC4, 0x1E, 0x7F, 0xA5, 0x08, 0x56, 0x16, 0x10, 0x2A, 0xD8, 0x48, 0x2C, 0x6C, 0x5E, 0x5F, 0x70, 0xB9, 0x7C, 0xCA, 0x01, 0x31, 0xA5, 0x2B,
    0x78, 0xCC, 0x43, 0xB8, 0x35, 0x28, 0xBC, 0xC9, 0x60, 0x61, 0x31, 0xC0, 0x61, 0xA0, 0xB7, 0xDF, 0x31, 0xD6, 0x33, 0x5C, 0x98, 0x28, 0x01, 0x82,
    0x57, 0x8E, 0xE2, 0xC6, 0x84, 0xE1, 0xE8, 0xA8, 0x5B, 0x3E, 0x6E, 0x28, 0x6D, 0x9C, 0xE4, 0x2C, 0xBF, 0xDF, 0x93, 0x1D, 0x6A, 0xBA, 0xB8, 0xF9,
    0x1C, 0x89, 0xBD, 0xB6, 0x18, 0x2B, 0x18, 0x81, 0x4C, 0x88, 0xDC, 0xF9, 0x5A, 0x8E, 0x1C, 0x3C, 0xEF, 0x1A, 0x2E, 0x86, 0x72, 0xAD, 0x8E, 0x37,
    0x0B, 0x66, 0xEF, 0x2C, 0x89, 0x4F, 0xF7, 0xD0, 0xB6, 0x85, 0x5F, 0x6F, 0xBC, 0xB4, 0xCF, 0x46, 0xF2, 0xB5, 0xA6, 0x72, 0x1E, 0x95, 0x11, 0xD0,
    0x95, 0xF7, 0x03, 0x77, 0x07, 0x6F, 0xF1, 0x44, 0xB0, 0x50, 0x9D, 0x96, 0x1B, 0xEA, 0x76, 0xF5, 0xC5, 0x93, 0xF7, 0x2E, 0x18, 0x28, 0x4D, 0x38,
    0x58, 0x20, 0xD8, 0xD2, 0x59, 0xEB, 0x74, 0xC3, 0x8D, 0xFA, 0xBB, 0xF0, 0xDD, 0x2A, 0x3E, 0xB8, 0xB1, 0x7A, 0x11, 0xA5, 0x12, 0x0F, 0x96, 0x9D,
    0x64, 0xD9, 0xFE, 0x7A, 0x0E, 0x63, 0xA9, 0xDF, 0x2D, 0xE9, 0x8C, 0xB7, 0x03, 0xB1, 0xA9, 0xD8, 0x06, 0x71, 0xC0, 0xF0, 0x1E, 0x9C, 0xEE, 0x89,
    0x32, 0xEA, 0x4F, 0x87, 0x54, 0x59, 0x4E, 0x12, 0x25, 0xBB, 0x2F, 0x71, 0xC7, 0x1C, 0x59, 0xCB, 0x91, 0x69, 0x31, 0x2E, 0x7F, 0x00, 0x55, 0x2D,
    0xEC, 0x97, 0xD3, 0x78, 0x2B, 0x2E, 0xB0, 0x6B, 0x5F, 0x02, 0x5A, 0x8B, 0x8E, 0x8A, 0xF7, 0x8B, 0x37, 0x3E, 0xE5, 0x12, 0x6C, 0xB8, 0x82, 0x57,
    0x01, 0x36, 0x61, 0x74, 0x7F, 0x5D, 0x2D, 0x61, 0x1C, 0xC3, 0x73, 0x45, 0xC5, 0xF6, 0x84, 0xA9, 0x23, 0xA9, 0x8E, 0xEC, 0x72, 0x24, 0xD7, 0x48,
    0xD8, 0x2E, 0x1E, 0xEA, 0x8D, 0x3A, 0x69, 0x67, 0xED,
};

bool same_files(const CardSaveDirectory& a, const CardSaveDirectory& b) {
    if (a.name != b.name || a.files.size() != b.files.size()) return false;
    for (std::size_t i = 0; i < a.files.size(); ++i)
        if (a.files[i].name != b.files[i].name || a.files[i].content != b.files[i].content) return false;
    return true;
}

std::uint32_t r32(const Bytes& b, std::size_t o) {
    return std::uint32_t{b[o]} | std::uint32_t{b[o + 1]} << 8 | std::uint32_t{b[o + 2]} << 16 | std::uint32_t{b[o + 3]} << 24;
}
void w32(Bytes& b, std::size_t o, std::uint32_t v) { for (int i = 0; i < 4; ++i) b[o + i] = static_cast<std::uint8_t>(v >> (8 * i)); }

// A scratch folder under the temp directory (small files only).
struct Temp {
    fs::path root;
    explicit Temp(const char* tag) {
        root = fs::temp_directory_path() / (std::string("b3_save_formats_") + tag + "_" + std::to_string(::getpid()));
        fs::remove_all(root);
        fs::create_directories(root);
    }
    ~Temp() { std::error_code ec; fs::remove_all(root, ec); }
};

Bytes read_bytes(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return Bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}
void write_bytes(const fs::path& p, const Bytes& b) {
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary).write(reinterpret_cast<const char*>(b.data()), static_cast<std::streamsize>(b.size()));
}

}  // namespace

TEST(crc32_reference) {
    const char* s = "123456789";
    CHECK(crc32(reinterpret_cast<const std::uint8_t*>(s), 9) == 0xCBF43926u);   // the CRC-32 check value
    CHECK(crc32(nullptr, 0) == 0);
}

TEST(sav_round_trip) {
    const Directories dirs = {game_like_save(1), other_save()};
    const Bytes b = write_sav(dirs);
    CHECK(std::memcmp(b.data(), "B3PSAVE1", 8) == 0 && r32(b, 8) == 1 && r32(b, 12) == b.size() - 20);
    std::string err;
    auto back = read_sav(b, &err);
    CHECK(back && *back == dirs);
    // any changed byte is caught by the checksum; a cut file, a wrong version or magic are refused
    for (std::size_t at : {std::size_t{20}, b.size() / 2, b.size() - 5}) {
        Bytes bad = b;
        bad[at] ^= 0x01;
        CHECK(!read_sav(bad, &err));
    }
    CHECK(!read_sav(Bytes(b.begin(), b.end() - 1), &err));
    Bytes v2 = b;
    w32(v2, 8, 2);
    CHECK(!read_sav(v2, &err) && err.find("version 2") != std::string::npos);
    Bytes nomagic = b;
    nomagic[0] = 'X';
    CHECK(!read_sav(nomagic, &err));
    CHECK(read_sav(write_sav({}), &err) && read_sav(write_sav({}), &err)->empty());
}

TEST(card_image_reads_back) {
    const Directories dirs = {game_like_save(2), other_save()};
    std::string err;
    const Bytes img = build_card_image(dirs, &err);
    CHECK(img.size() == kImageSize);
    auto back = read_card_image(img.data(), img.size(), &err);
    CHECK(back && *back == dirs);   // names, modes, times and bytes
    // PCSX2's .ps2: every 512-byte page followed by its 16-byte spare (ECC) area
    Bytes ecc;
    ecc.reserve(kPageCount * 528);
    for (std::size_t p = 0; p < kPageCount; ++p) {
        ecc.insert(ecc.end(), img.begin() + p * 512, img.begin() + (p + 1) * 512);
        ecc.insert(ecc.end(), 16, 0xEE);
    }
    auto back_ecc = read_card_image(ecc.data(), ecc.size(), &err);
    CHECK(back_ecc && *back_ecc == dirs);
    // an empty card: no directories
    const Bytes empty = build_card_image({}, &err);
    auto none = read_card_image(empty.data(), empty.size(), &err);
    CHECK(none && none->empty());
    // not a card
    Bytes junk(4096, 0);
    CHECK(!read_card_image(junk.data(), junk.size(), &err));
    CHECK(!read_card_image(img.data(), img.size() / 2, &err));
}

// What a game's own writes leave: a file whose clusters are not consecutive (moved through the FAT), a deleted
// directory (its entry's exists bit clear).
TEST(card_image_follows_the_fat) {
    const Directories dirs = {other_save(), game_like_save(3)};
    std::string err;
    Bytes img = build_card_image(dirs, &err);
    const std::uint32_t alloc_off = r32(img, 52), ifat0 = r32(img, 80);
    const std::uint32_t fat_cluster = r32(img, std::size_t{ifat0} * kClusterSize);   // FAT cluster 0 (entries 0..255)
    auto fat_at = [&](std::uint32_t c) { return std::size_t{fat_cluster} * kClusterSize + 4 * c; };
    // the root's entry 3 (the second directory) -> its first cluster; its entry 4 is BASLUS-21050 (after icon / view)
    auto entry = [&](std::uint32_t first_cluster, std::uint32_t i) {
        return std::size_t{first_cluster + alloc_off} * kClusterSize + i * 512;
    };
    const std::uint32_t root = r32(img, 60);
    const std::uint32_t dir2 = r32(img, entry(root, 3) + 16);
    const std::size_t save_entry = entry(dir2, 4);
    const std::uint32_t first = r32(img, save_entry + 16);
    // move the file's second cluster to a free one near the card's end, relinking the chain around it
    const std::uint32_t second = r32(img, fat_at(first)) & 0x7FFFFFFFu;
    const std::uint32_t third = r32(img, fat_at(second));
    const std::uint32_t moved = 240;   // still free (the save uses fewer), inside FAT cluster 0
    CHECK(r32(img, fat_at(moved)) == 0x7FFFFFFFu);
    std::memcpy(&img[std::size_t{moved + alloc_off} * kClusterSize], &img[std::size_t{second + alloc_off} * kClusterSize], kClusterSize);
    std::memset(&img[std::size_t{second + alloc_off} * kClusterSize], 0xAB, kClusterSize);
    w32(img, fat_at(first), moved | 0x80000000u);
    w32(img, fat_at(moved), third);
    w32(img, fat_at(second), 0x7FFFFFFFu);
    auto back = read_card_image(img.data(), img.size(), &err);
    CHECK(back && back->size() == 2 && (*back)[1] == dirs[1]);
    // delete the first directory: only the game's save is left
    const std::size_t e2 = entry(root, 2);
    img[e2 + 1] &= 0x7F;   // mode's 0x8000 (exists) clear
    back = read_card_image(img.data(), img.size(), &err);
    CHECK(back && back->size() == 1 && (*back)[0] == dirs[1]);
    // a chain that ends before its file is an error, not a short file
    w32(img, fat_at(moved), 0xFFFFFFFFu);
    CHECK(!read_card_image(img.data(), img.size(), &err) && err.find("chain") != std::string::npos);
}

TEST(psu_round_trip) {
    const CardSaveDirectory d = game_like_save(4);
    const Bytes psu = write_psu(d);
    // directory, ".", "..", then per file a header and its bytes padded to 1 KiB
    CHECK(psu.size() == 512 * 3 + 3 * 512 + 1024 + 15360 + 62464);
    CHECK(r32(psu, 4) == 5 && std::string(reinterpret_cast<const char*>(&psu[512 + 64])) == ".");
    std::string err;
    auto back = read_psu(psu, &err);
    CHECK(back && *back == d);
    CHECK(!read_psu(Bytes(psu.begin(), psu.begin() + 512 * 4 + 100), &err));   // cut inside a file
    CHECK(!read_psu(Bytes(2048, 0), &err));
}

TEST(max_from_mymcplus) {
    std::string err;
    auto d = read_max(Bytes(std::begin(kMax), std::end(kMax)), &err);
    CHECK(d.has_value());
    if (d) CHECK(same_files(*d, max_save()));
    // the packed stream cut short: refused (not padded with garbage)
    CHECK(!read_max(Bytes(std::begin(kMax), std::begin(kMax) + 0x5C + 40), &err));
    CHECK(!read_max(Bytes(100, 0), &err));
}

TEST(read_any_and_game_save) {
    Temp t("any");
    std::string err;
    const CardSaveDirectory save = game_like_save(5);
    // a card image (.ps2, with the ECC area) holding two games' saves
    {
        const Bytes img = build_card_image({other_save(), save}, &err);
        Bytes ecc;
        for (std::size_t p = 0; p < kPageCount; ++p) {
            ecc.insert(ecc.end(), img.begin() + p * 512, img.begin() + (p + 1) * 512);
            ecc.insert(ecc.end(), 16, 0);
        }
        write_bytes(t.root / "Mcd001.ps2", ecc);
        auto all = read_any(t.root / "Mcd001.ps2", &err);
        CHECK(all && all->size() == 2);
        auto game = game_save(*all, &err);
        CHECK(game && game->size() == 1 && (*game)[0] == save);
    }
    write_bytes(t.root / "x.psu", write_psu(save));
    auto psu = read_any(t.root / "x.psu", &err);
    CHECK(psu && psu->size() == 1 && (*psu)[0] == save);
    write_bytes(t.root / "y.max", Bytes(std::begin(kMax), std::end(kMax)));
    auto max = read_any(t.root / "y.max", &err);
    CHECK(max && max->size() == 1 && same_files((*max)[0], max_save()));
    write_bytes(t.root / "z.sav", write_sav({save}));
    auto sav = read_any(t.root / "z.sav", &err);
    CHECK(sav && sav->size() == 1 && (*sav)[0] == save);
    // a PCSX2 folder memory card: its superblock file and one folder per save (each with an _pcsx2_index)
    const fs::path folder = t.root / "Mcd001.ps2.folder";
    write_bytes(folder / "_pcsx2_superblock", Bytes(512, 0));
    for (const auto& f : save.files) write_bytes(folder / save.name / f.name, f.content);
    write_bytes(folder / save.name / "_pcsx2_index", Bytes{'{', '}'});
    write_bytes(folder / "BESLES-00000" / "data", Bytes(10, 1));
    auto card = read_any(folder, &err);
    CHECK(card && card->size() == 2);
    auto game = card ? game_save(*card, &err) : std::nullopt;
    CHECK(game && game->size() == 1 && (*game)[0].files.size() == 3);
    if (game) for (const auto& f : save.files) {
        bool found = false;
        for (const auto& g : (*game)[0].files) found = found || (g.name == f.name && g.content == f.content);
        CHECK(found);
    }
    // picked through a file dialog: the folder card's superblock file stands for the card
    auto picked = read_any(folder / "_pcsx2_superblock", &err);
    CHECK(picked && picked->size() == 2);
    // one save folder alone
    auto one = read_any(folder / save.name, &err);
    CHECK(one && one->size() == 1 && (*one)[0].name == save.name);
    // no Burnout 3 save / not a save
    CHECK(!game_save({other_save()}, &err) && err.find("BESLES-00000") != std::string::npos);
    write_bytes(t.root / "notes.txt", Bytes(3000, 'a'));
    CHECK(!read_any(t.root / "notes.txt", &err));
    CHECK(!read_any(t.root / "missing.psu", &err));
}

TEST(save_backups) {
    // mac5 (proposal 1): the old profile-N.sav is copied to backups/ before it is replaced; the newest 10 per slot stay
    Temp t("backups");
    std::string err;
    CHECK(backup_profile(t.root, 1, 1794211789).empty());          // no file yet: nothing to keep
    CHECK(store_profile(t.root, 1, {game_like_save(1)}, &err));     // the first save: no backup
    CHECK(!fs::exists(t.root / "backups") || fs::is_empty(t.root / "backups"));
    for (int k = 0; k < 13; ++k) CHECK(!backup_profile(t.root, 1, 1794211789 + k).empty());
    std::size_t n = 0;
    for (const auto& e : fs::directory_iterator(t.root / "backups")) n += e.path().filename().string().rfind("profile-1-", 0) == 0;
    CHECK(n == kKeepBackups);
    // a backup reads back as the save it kept; another slot's files are not counted against slot 1
    CHECK(store_profile(t.root, 3, {game_like_save(3)}, &err) && store_profile(t.root, 3, {game_like_save(4)}, &err));
    std::size_t three = 0;
    for (const auto& e : fs::directory_iterator(t.root / "backups"))
        if (e.path().filename().string().rfind("profile-3-", 0) == 0) {
            ++three;
            auto kept = read_sav_file(e.path(), &err);
            CHECK(kept && kept->size() == 1 && (*kept)[0] == game_like_save(3));
        }
    CHECK(three == 1);
}

TEST(store_profiles) {
    Temp t("store");
    std::string err;
    // nothing yet: a fresh card
    auto p = load_profile(t.root, 1, &err);
    CHECK(p && p->source == ProfileSource::kNone && p->dirs.empty());
    CHECK(!load_profile(t.root, 0, &err) && !load_profile(t.root, 6, &err));
    // the older folder layout is read when there is no .sav
    const CardSaveDirectory save = game_like_save(6);
    for (const auto& f : save.files) write_bytes(t.root / "profile-2" / save.name / f.name, f.content);
    p = load_profile(t.root, 2, &err);
    CHECK(p && p->source == ProfileSource::kFolder && p->dirs.size() == 1 && p->dirs[0].files.size() == 3);
    // ... and the .sav wins once there is one
    const CardSaveDirectory newer = game_like_save(7);
    CHECK(store_profile(t.root, 2, {newer}, &err));
    p = load_profile(t.root, 2, &err);
    CHECK(p && p->source == ProfileSource::kSav && p->dirs.size() == 1 && p->dirs[0] == newer);
    CHECK(!fs::exists(t.root / "profile-2.sav.tmp"));
    // import: a .psu into slot 2, the old .sav kept as .bak; a file without the game's save changes nothing
    write_bytes(t.root / "in.psu", write_psu(save));
    CHECK(import_profile(t.root, 2, t.root / "in.psu", 1793762859, &err));
    p = load_profile(t.root, 2, &err);
    CHECK(p && p->dirs.size() == 1 && same_files(p->dirs[0], save));
    auto bak = read_sav_file(t.root / "profile-2.sav.bak", &err);
    CHECK(bak && bak->size() == 1 && (*bak)[0] == newer);
    write_bytes(t.root / "other.psu", write_psu(other_save()));
    CHECK(!import_profile(t.root, 2, t.root / "other.psu", 0, &err));
    p = load_profile(t.root, 2, &err);
    CHECK(p && same_files(p->dirs[0], save));
    // a .max has no times: the import's clock fills them
    write_bytes(t.root / "in.max", Bytes(std::begin(kMax), std::end(kMax)));
    auto imported = import_game_save(t.root / "in.max", 1793762859, &err);
    CHECK(imported && (*imported)[0].created == card_timestamp_from_unix(1793762859) &&
          (*imported)[0].files[0].modified == card_timestamp_from_unix(1793762859));
    // the active slot
    CHECK(read_active_slot(t.root) == 1);
    CHECK(write_active_slot(t.root, 3, &err) && read_active_slot(t.root) == 3);
    // the player's folder (per platform)
#if defined(__APPLE__)
    if (const char* h = std::getenv("HOME"))
        CHECK(default_saves_root() == fs::path(h) / "Library" / "Application Support" / "b3port" / "native" / "saves");
#else   // Linux's XDG rule
    const char* old = std::getenv("XDG_DATA_HOME");
    const std::string keep = old ? old : "";
    setenv("XDG_DATA_HOME", "/xdg", 1);
    CHECK(default_saves_root() == fs::path("/xdg/b3port/native/saves"));
    unsetenv("XDG_DATA_HOME");
    if (const char* h = std::getenv("HOME")) CHECK(default_saves_root() == fs::path(h) / ".local/share/b3port/native/saves");
    if (old) setenv("XDG_DATA_HOME", keep.c_str(), 1);
#endif
}

// The card the game sees: a save written page by page through the device lands in profile-N.sav once the card is quiet,
// byte for byte; the next start presents it again.
TEST(profile_card_writes_back) {
    Temp t("card");
    setenv("B3NATIVE_CARD_PERSIST", "1", 1);
    unsetenv("B3NATIVE_CARD_FILE_TIME");
    unsetenv("B3NATIVE_CARD_CLOCK");
    std::string err;
    const CardSaveDirectory save = game_like_save(8);
    const Bytes written = build_card_image({save}, &err);   // what the game's writes leave on the card
    {
        ProfileCard card(t.root);
        CHECK(card.attach(1, &err) && card.source() == ProfileSource::kNone);
        CHECK(card.device().probe() == SaveCard::kProbeNewCard);
        // the game formats nothing (a fresh card is formatted) and writes its save: pages in 16-page transfers
        for (std::uint32_t pg = 0; pg < kPageCount; pg += 16) {
            if (std::memcmp(&written[std::size_t{pg} * 512], &card.device().image()[std::size_t{pg} * 512], 16 * 512) == 0) continue;
            CHECK(card.device().write_pages(pg, &written[std::size_t{pg} * 512], 16 * 512));
        }
        card.tick(1000);
        CHECK(!fs::exists(profile_file(t.root, 1)));   // not before the card has been quiet for a second
        card.tick(1000 + ProfileCard::kSettleVBlanks - 1);
        CHECK(!fs::exists(profile_file(t.root, 1)));
        card.tick(1000 + ProfileCard::kSettleVBlanks);
        CHECK(fs::exists(profile_file(t.root, 1)));
        auto stored = read_sav_file(profile_file(t.root, 1), &err);
        CHECK(stored && stored->size() == 1 && (*stored)[0] == save);
        // export: the game's save as a .sav anywhere
        card.post(ProfileCard::Request::kExport, t.root / "out" / "mine.sav");
        card.tick(2000);
        CHECK(card.status().serial == 1 && card.status().ok);
        auto exported = read_sav_file(t.root / "out" / "mine.sav", &err);
        CHECK(exported && exported->size() == 1 && (*exported)[0] == save);
    }
    {
        ProfileCard again(t.root);
        CHECK(again.attach(1, &err) && again.source() == ProfileSource::kSav);
        CHECK(again.device().image() == written);   // the same card image the game left
        // import: profile-1.sav replaced; the running game's later saves no longer written (they would overwrite it)
        write_bytes(t.root / "in.psu", write_psu(game_like_save(9)));
        again.post(ProfileCard::Request::kImport, t.root / "in.psu");
        again.tick(10);
        CHECK(again.status().ok && !again.persist());
        CHECK(again.device().write_pages(100, &written[100 * 512], 512));
        again.tick(10 + 2 * ProfileCard::kSettleVBlanks);
        auto now = read_sav_file(profile_file(t.root, 1), &err);
        CHECK(now && same_files((*now)[0], game_like_save(9)));
        // a file without the game's save: the import fails and says so
        write_bytes(t.root / "other.psu", write_psu(other_save()));
        again.post(ProfileCard::Request::kImport, t.root / "other.psu");
        again.tick(500);
        CHECK(again.status().serial == 2 && !again.status().ok && again.status().text.find("IMPORT FAILED") == 0);
    }
    // an unreadable save file: an empty card, nothing written over the file
    {
        Temp u("bad");
        write_bytes(profile_file(u.root, 1), Bytes{'B', '3', 'P', 'S', 'A', 'V', 'E', '1', 9, 9});
        ProfileCard card(u.root);
        CHECK(!card.attach(1, &err) && card.device().attached() && !card.persist());
        CHECK(card.status().text.find("UNREADABLE") != std::string::npos);
        CHECK(card.device().write_pages(0, written.data(), 16 * 512));
        card.tick(0);
        card.tick(1000);
        CHECK(read_bytes(profile_file(u.root, 1)).size() == 10);
    }
    // without B3NATIVE_CARD_PERSIST=1 (proof tools): writes stay in the image
    {
        Temp u("nopersist");
        unsetenv("B3NATIVE_CARD_PERSIST");
        ProfileCard card(u.root);
        CHECK(card.attach(1, &err) && !card.persist());
        CHECK(card.device().write_pages(0, written.data(), 16 * 512));
        card.tick(0);
        card.tick(1000);
        CHECK(card.flush(&err) && !fs::exists(profile_file(u.root, 1)));
    }
}

// 1.1: the save folder chosen in the launcher (launcher.ini saves_dir), and a change of folder's copy: the saves in use
// copied, nothing already there replaced, the old folder untouched
TEST(chosen_save_folder) {
    Temp t("chosen");
    std::string err;
    CHECK(chosen_saves_root(t.root).empty());   // no launcher.ini: the default
    CHECK(chosen_saves_root(fs::path()).empty());
    {
        std::ofstream(t.root / "launcher.ini", std::ios::binary) << "# x\ngame_dir = /g\n";
    }
    CHECK(chosen_saves_root(t.root).empty());   // none chosen
    {
        std::ofstream(t.root / "launcher.ini", std::ios::binary) << "game_dir = /g\r\n  saves_dir =  /cloud/My Drive/B3 caf\xC3\xA9 \r\n";
    }
    CHECK(chosen_saves_root(t.root) == path_from_utf8("/cloud/My Drive/B3 caf\xC3\xA9"));

    const fs::path from = t.root / "old", to = t.root / "new";
    fs::create_directories(from);
    CHECK(!has_saves(from) && !has_saves(to));
    CHECK(copy_saves(from, to, &err) == 0 && fs::is_directory(to));   // nothing to copy: the folder made
    const Directories one{game_like_save(1)}, two{game_like_save(2)};
    CHECK(write_sav_file(profile_file(from, 1), one, &err));
    CHECK(write_sav_file(profile_file(from, 3), two, &err));
    write_bytes(from / "profile-3.sav.bak", Bytes{1, 2, 3});
    write_bytes(from / "profile-2" / "BASLUS-21050" / "icon.sys", Bytes{4, 5});   // an older folder layout
    write_bytes(from / "backups" / "profile-1-20261118-101010.sav", Bytes{6});
    write_bytes(from / "unrelated.txt", Bytes{7});
    CHECK(write_active_slot(from, 3, &err));
    CHECK(has_saves(from));
    // a file already in the new folder is left as it is
    write_bytes(to / "profile-3.sav", Bytes{9, 9});
    int kept = -1;
    CHECK(copy_saves(from, to, &err, &kept) == 5 && kept == 1);
    CHECK(read_bytes(to / "profile-3.sav") == (Bytes{9, 9}));
    CHECK(read_bytes(profile_file(to, 1)) == read_bytes(profile_file(from, 1)));
    CHECK(read_bytes(to / "profile-3.sav.bak") == (Bytes{1, 2, 3}));
    CHECK(read_bytes(to / "profile-2" / "BASLUS-21050" / "icon.sys") == (Bytes{4, 5}));
    CHECK(read_bytes(to / "backups" / "profile-1-20261118-101010.sav") == (Bytes{6}));
    CHECK(!fs::exists(to / "unrelated.txt"));
    CHECK(read_active_slot(to) == 3 && has_saves(to));
    CHECK(fs::exists(profile_file(from, 3)) && read_active_slot(from) == 3);   // the old folder keeps its files
    // again: everything there already
    CHECK(copy_saves(from, to, &err, &kept) == 0 && kept == 6);
}

// 1.1: PCSX2's memory card folders (pcsx2_cards.h) per system, only those there
TEST(pcsx2_card_folders_found) {
    Temp t("pcsx2");
    Pcsx2Homes h;
    h.home = t.root / "home";
    h.documents = t.root / "docs";
    h.program_files = {t.root / "pf", t.root / "pf86"};
    // every place named, most likely first
    auto all = pcsx2_card_folders(h, false, Pcsx2Os::kLinux);
    CHECK(all.size() == 2 && all[0].path == h.home / ".config" / "PCSX2" / "memcards" &&
          all[1].path == h.home / ".var" / "app" / "net.pcsx2.PCSX2" / "config" / "PCSX2" / "memcards" &&
          all[1].label == "PCSX2 memory cards (Flatpak)");
    h.xdg_config = t.root / "xdg";
    all = pcsx2_card_folders(h, false, Pcsx2Os::kLinux);
    CHECK(all.size() == 3 && all[0].path == t.root / "xdg" / "PCSX2" / "memcards");
    all = pcsx2_card_folders(h, false, Pcsx2Os::kWindows);
    CHECK(all.size() == 3 && all[0].path == h.documents / "PCSX2" / "memcards" && all[1].path == t.root / "pf" / "PCSX2" / "memcards" &&
          all[1].label == "PCSX2 memory cards (installed)");
    all = pcsx2_card_folders(h, false, Pcsx2Os::kMac);
    CHECK(all.size() == 1 && all[0].path == h.home / "Library" / "Application Support" / "PCSX2" / "memcards");
    // only the folders that exist (none yet); then the Flatpak's, with its first card
    CHECK(pcsx2_card_folders(h, true, Pcsx2Os::kLinux).empty());
    const fs::path flat = h.home / ".var" / "app" / "net.pcsx2.PCSX2" / "config" / "PCSX2" / "memcards";
    fs::create_directories(flat);
    auto found = pcsx2_card_folders(h, true, Pcsx2Os::kLinux);
    CHECK(found.size() == 1 && found[0].path == flat);
    CHECK(pcsx2_first_card(flat).empty());
    write_bytes(flat / "Mcd001.ps2", Bytes{0});
    CHECK(pcsx2_first_card(flat) == flat / "Mcd001.ps2");
    fs::create_directories(h.documents / "PCSX2" / "memcards");
    found = pcsx2_card_folders(h, true, Pcsx2Os::kWindows);
    CHECK(found.size() == 1 && found[0].path == h.documents / "PCSX2" / "memcards");
}

TEST_MAIN("save formats: .sav, card images, .psu, .max, folder cards, the save folder, the card's write-back")
