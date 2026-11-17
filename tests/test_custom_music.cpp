// The custom soundtrack's host side (src/native/custom_music.h): the tag parsers on synthetic bytes, the file-name
// fallback, the popup text, and a synthetic WAV tone through the folder scan, the decoder, the resampler and the
// player (no music files: everything is made here).
#include "test.h"

#include "custom_music.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace {

using Bytes = std::vector<std::uint8_t>;

void put(Bytes& b, const std::string& s) { b.insert(b.end(), s.begin(), s.end()); }
void be32(Bytes& b, std::uint32_t v) { for (int k = 3; k >= 0; --k) b.push_back(std::uint8_t(v >> (8 * k))); }
void le32(Bytes& b, std::uint32_t v) { for (int k = 0; k < 4; ++k) b.push_back(std::uint8_t(v >> (8 * k))); }
void le16(Bytes& b, std::uint16_t v) { b.push_back(std::uint8_t(v)); b.push_back(std::uint8_t(v >> 8)); }
void syncsafe(Bytes& b, std::uint32_t v) { for (int k = 3; k >= 0; --k) b.push_back(std::uint8_t((v >> (7 * k)) & 0x7F)); }

// an ID3v2.3 / 2.4 frame
Bytes frame(int ver, const std::string& id, const Bytes& data, std::uint16_t flags = 0) {
    Bytes f;
    put(f, id);
    if (ver == 4) syncsafe(f, std::uint32_t(data.size()));
    else be32(f, std::uint32_t(data.size()));
    f.push_back(std::uint8_t(flags >> 8));
    f.push_back(std::uint8_t(flags));
    f.insert(f.end(), data.begin(), data.end());
    return f;
}
Bytes text(std::uint8_t enc, const Bytes& s) { Bytes b{enc}; b.insert(b.end(), s.begin(), s.end()); return b; }
Bytes ascii(const std::string& s) { return Bytes(s.begin(), s.end()); }
Bytes utf16le_bom(const std::u16string& s) {
    Bytes b{0xFF, 0xFE};
    for (char16_t c : s) le16(b, std::uint16_t(c));
    b.push_back(0), b.push_back(0);
    return b;
}
Bytes tag(int ver, std::uint8_t flags, const Bytes& frames) {
    Bytes t;
    put(t, "ID3");
    t.push_back(std::uint8_t(ver));
    t.push_back(0);
    t.push_back(flags);
    syncsafe(t, std::uint32_t(frames.size() + 16));   // with padding
    t.insert(t.end(), frames.begin(), frames.end());
    t.resize(t.size() + 16, 0);
    return t;
}

}  // namespace

TEST(id3v23_latin1_and_utf16) {
    Bytes fr;
    const Bytes title = frame(3, "TIT2", text(0, ascii("Caf\xE9 Racer")));   // Latin-1 e acute
    const Bytes artist = frame(3, "TPE1", text(1, utf16le_bom(u"The \u00C9xample")));
    const Bytes album = frame(3, "TALB", text(0, ascii(std::string("Greatest\0Ignored", 16))));
    const Bytes cover = frame(3, "APIC", Bytes(300, 0xAB));
    for (const Bytes* f : {&cover, &title, &artist, &album}) fr.insert(fr.end(), f->begin(), f->end());
    const Bytes t = tag(3, 0, fr);
    b3music::Tags g;
    std::size_t whole = 0;
    CHECK(b3music::parse_id3v2(t.data(), t.size(), g, &whole));
    CHECK(whole == t.size());
    CHECK(g.title == "Caf\xC3\xA9 Racer");
    CHECK(g.artist == "The \xC3\x89xample");
    CHECK(g.album == "Greatest");
}

TEST(id3v24_utf8_syncsafe_and_data_length) {
    Bytes fr;
    Bytes v = text(3, ascii("Ni\xC3\xB1o"));
    Bytes dl;   // a data length indicator (flag 0x0001) before the data
    syncsafe(dl, std::uint32_t(v.size()));
    dl.insert(dl.end(), v.begin(), v.end());
    const Bytes title = frame(4, "TIT2", dl, 0x0001);
    // a 200-byte frame: its syncsafe size differs from a plain one
    Bytes longtext = text(3, ascii(std::string(199, 'x')));
    const Bytes junk = frame(4, "TXXX", longtext);
    const Bytes artist = frame(4, "TPE1", text(3, ascii(std::string("Ar\0Second", 9))));
    const Bytes utf16be = frame(4, "TALB", text(2, Bytes{0x00, 'A', 0x00, 'l', 0x00, 'b', 0x00, 0x00}));
    for (const Bytes* f : {&junk, &title, &artist, &utf16be}) fr.insert(fr.end(), f->begin(), f->end());
    const Bytes t = tag(4, 0, fr);
    b3music::Tags g;
    CHECK(b3music::parse_id3v2(t.data(), t.size(), g));
    CHECK(g.title == "Ni\xC3\xB1o");
    CHECK(g.artist == "Ar");
    CHECK(g.album == "Alb");
}

TEST(id3v22_and_unsynchronisation) {
    Bytes f22;
    put(f22, "TT2");
    const Bytes v = text(0, ascii("Old"));
    f22.push_back(0), f22.push_back(0), f22.push_back(std::uint8_t(v.size()));
    f22.insert(f22.end(), v.begin(), v.end());
    put(f22, "TP1");
    const Bytes a = text(0, ascii("Band"));
    f22.push_back(0), f22.push_back(0), f22.push_back(std::uint8_t(a.size()));
    f22.insert(f22.end(), a.begin(), a.end());
    b3music::Tags g;
    const Bytes t22 = tag(2, 0, f22);
    CHECK(b3music::parse_id3v2(t22.data(), t22.size(), g));
    CHECK(g.title == "Old" && g.artist == "Band" && g.album.empty());

    // v2.3 with the whole tag unsynchronised: a 0xFF byte in the text is stored FF 00
    Bytes fr = frame(3, "TIT2", text(0, Bytes{'y', 0xFF, 'z'}));
    Bytes un;
    for (std::size_t k = 0; k < fr.size(); ++k) { un.push_back(fr[k]); if (fr[k] == 0xFF) un.push_back(0x00); }
    const Bytes t = tag(3, 0x80, un);
    b3music::Tags h;
    CHECK(b3music::parse_id3v2(t.data(), t.size(), h));
    CHECK(h.title == "y\xC3\xBFz");
    // not a tag
    const Bytes no = ascii("ID4xxxxxxx");
    CHECK(!b3music::parse_id3v2(no.data(), no.size(), h));
}

TEST(id3v1_fills_only_empty_fields) {
    Bytes v1 = ascii("TAG");
    auto field = [&](const std::string& s, std::size_t n) { Bytes f = ascii(s); f.resize(n, 0); v1.insert(v1.end(), f.begin(), f.end()); };
    field("Title One", 30);
    field("Artist One", 30);
    field("Album One", 30);
    v1.resize(128, 0);
    b3music::Tags g;
    g.title = "From ID3v2";
    CHECK(b3music::parse_id3v1(v1.data(), v1.size(), g));
    CHECK(g.title == "From ID3v2");
    CHECK(g.artist == "Artist One");
    CHECK(g.album == "Album One");
}

TEST(vorbis_comments_and_flac) {
    Bytes vc;
    le32(vc, 6);
    put(vc, "vendor");
    const std::vector<std::string> entries = {"tracknumber=3", "ALBUMARTIST=Various", "title=Song", "Artist=Singer", "ALBUM=Record", "TITLE=Second"};
    le32(vc, std::uint32_t(entries.size()));
    for (const auto& e : entries) { le32(vc, std::uint32_t(e.size())); put(vc, e); }
    b3music::Tags g;
    CHECK(b3music::parse_vorbis_comment(vc.data(), vc.size(), g));
    CHECK(g.title == "Song");
    CHECK(g.artist == "Singer");   // ARTIST before ALBUMARTIST
    CHECK(g.album == "Record");

    Bytes flac = ascii("fLaC");
    flac.push_back(0x00);   // STREAMINFO, not last
    flac.push_back(0), flac.push_back(0), flac.push_back(34);
    flac.resize(flac.size() + 34, 0);
    flac.push_back(0x84);   // VORBIS_COMMENT, last
    flac.push_back(std::uint8_t(vc.size() >> 16)), flac.push_back(std::uint8_t(vc.size() >> 8)), flac.push_back(std::uint8_t(vc.size()));
    flac.insert(flac.end(), vc.begin(), vc.end());
    b3music::Tags f;
    CHECK(b3music::parse_flac_tags(flac.data(), flac.size(), f));
    CHECK(f.title == "Song" && f.artist == "Singer" && f.album == "Record");
    // a truncated block is refused, not read past the end
    b3music::Tags bad;
    CHECK(!b3music::parse_flac_tags(flac.data(), flac.size() - 5, bad));
}

TEST(file_name_fallback) {
    b3music::Tags a;
    b3music::tags_from_filename("/music/08 Ramones - I Wanna Be Sedated.mp3", a);
    CHECK(a.artist == "Ramones");
    CHECK(a.title == "I Wanna Be Sedated");
    b3music::Tags b;
    b3music::tags_from_filename("C:\\Music\\Just A Title.flac", b);
#ifdef _WIN32
    CHECK(b.title == "Just A Title");
#else
    CHECK(b.title.find("Just A Title") != std::string::npos);
#endif
    b3music::Tags c;
    b3music::tags_from_filename("x/00 Intro.ogg", c);
    CHECK(c.title == "Intro" && c.artist.empty());
    b3music::Tags d;
    d.title = "Tagged";
    b3music::tags_from_filename("x/02 - Other.wav", d);
    CHECK(d.title == "Tagged");
}

TEST(popup_text) {
    const std::u16string a = b3music::popup_text("Caf\xC3\xA9 \xE2\x80\x99" "n\xE2\x80\x99 \xF0\x9F\x8E\xB8", 48);   // é, ’, ’, an emoji
    CHECK(a == u"Caf\u00E9 'n' ?");
    const std::u16string b = b3music::popup_text(std::string(60, 'a'), 20);
    CHECK(b.size() == 20);
    CHECK(b.substr(17) == u"...");
    CHECK(b3music::popup_text("x\xE2\x80\xA6", 10) == u"x...");
}

namespace {
// a 16-bit PCM WAV of a sine tone
void write_wav(const std::string& path, unsigned rate, unsigned channels, double hz, double seconds, double amp) {
    const std::uint32_t frames = std::uint32_t(rate * seconds);
    Bytes b;
    put(b, "RIFF");
    le32(b, 36 + frames * channels * 2);
    put(b, "WAVEfmt ");
    le32(b, 16);
    le16(b, 1);
    le16(b, std::uint16_t(channels));
    le32(b, rate);
    le32(b, rate * channels * 2);
    le16(b, std::uint16_t(channels * 2));
    le16(b, 16);
    put(b, "data");
    le32(b, frames * channels * 2);
    for (std::uint32_t k = 0; k < frames; ++k) {
        const double v = amp * std::sin(2.0 * 3.14159265358979 * hz * double(k) / double(rate));
        for (unsigned c = 0; c < channels; ++c) le16(b, std::uint16_t(std::int16_t(std::lround(v * 32767.0))));
    }
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (f) { std::fwrite(b.data(), 1, b.size(), f); std::fclose(f); }
}
}  // namespace

TEST(wav_folder_decode_resample_play) {
    namespace fs = std::filesystem;
    const fs::path dir = fs::path("build") / "test_custom_music.d";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir / "Sub Album", ec);
    const std::string wav = (dir / "Sub Album" / "03 Test Band - Tone.wav").string();
    write_wav(wav, 32000, 1, 1000.0, 1.0, 0.5);
    { std::FILE* f = std::fopen((dir / "notes.txt").string().c_str(), "wb"); if (f) std::fclose(f); }

    std::string why;
    const std::vector<b3music::Track> tracks = b3music::scan_folder(dir.string(), &why);
    CHECK(tracks.size() == 1);
    if (tracks.size() != 1) return;
    CHECK(tracks[0].tags.artist == "Test Band");
    CHECK(tracks[0].tags.title == "Tone");
    CHECK(tracks[0].tags.album == "Sub Album");

    auto dec = b3music::Decoder::open(wav, &why);
    CHECK(dec != nullptr);
    if (!dec) return;
    CHECK(dec->rate() == 32000);
    CHECK(dec->channels() == 1);
    std::vector<float> lr(2 * 40000);
    const std::size_t got = dec->read(lr.data(), 40000);
    CHECK(got == 32000);
    CHECK(lr[2 * 8] == lr[2 * 8 + 1]);   // mono doubled

    // the player at 48 kHz: 1 s of 32 kHz source is 48000 output frames, the tone still 1 kHz, the gain applied
    b3music::Player p;
    CHECK(p.start(tracks[0], 0, &why));
    p.set_gain(0.5f);
    p.play(false);
    std::vector<std::int16_t> out;
    std::int16_t block[2 * 800];
    for (int k = 0; k < 70 && !p.finished(); ++k) {
        std::fill(std::begin(block), std::end(block), std::int16_t(0));
        p.mix(block, 800);
        out.insert(out.end(), block, block + 2 * 800);
    }
    CHECK(p.finished());
    const std::uint64_t mixed = p.frames_mixed();
    CHECK(mixed >= 47990 && mixed <= 48010);
    CHECK_NEAR(double(p.position_ms()), 1000.0, 2.0);
    // the peak: 0.5 (file) x 0.5 (gain) of full scale
    CHECK_NEAR(double(p.peak()), 0.25 * 32767.0, 0.01 * 32767.0);
    int crossings = 0;
    for (std::size_t k = 1; k < 48000; ++k)
        if ((out[2 * (k - 1)] < 0) != (out[2 * k] < 0)) ++crossings;
    CHECK(crossings >= 1990 && crossings <= 2010);   // 1000 Hz: 2000 sign changes a second

    // paused: nothing mixed, the position kept; a start half way plays the rest
    b3music::Player q;
    CHECK(q.start(tracks[0], 16000, &why));
    q.set_gain(1.0f);
    q.play(false);
    q.pause(true);
    std::fill(std::begin(block), std::end(block), std::int16_t(0));
    q.mix(block, 800);
    CHECK(q.frames_mixed() == 0);
    CHECK(block[100] == 0);
    q.pause(false);
    for (int k = 0; k < 70 && !q.finished(); ++k) q.mix(block, 800);
    CHECK(q.frames_mixed() >= 23990 && q.frames_mixed() <= 24010);
    // pitch 2: twice as fast
    b3music::Player r;
    CHECK(r.start(tracks[0], 0, &why));
    r.set_gain(1.0f);
    r.set_pitch(2.0f);
    r.play(false);
    for (int k = 0; k < 70 && !r.finished(); ++k) r.mix(block, 800);
    CHECK(r.frames_mixed() >= 23990 && r.frames_mixed() <= 24010);
    fs::remove_all(dir, ec);
}

TEST_MAIN("custom_music (remaster-features §6: tags, folder, decoders, player)")
