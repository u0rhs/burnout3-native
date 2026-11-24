// The custom soundtrack's host side (src/native/custom_music.h): the tag parsers on synthetic bytes, the file-name
// fallback, the popup text, and a synthetic WAV tone through the folder scan, the decoder, the resampler and the
// player (no music files: everything is made here).
#include "test.h"

#include "custom_music.h"

#include <cmath>
#include <cstring>
#include <limits>
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
    // musicvol1: gain 4 (CUSTOM MUSIC VOLUME 200 % on a loud stream): the file's 0.5 would be 2.0 of full scale; the
    // limiter holds the song's peaks at its ceiling, so nothing clips
    b3music::Player h;
    CHECK(h.start(tracks[0], 0, &why));
    h.set_gain(4.0f);
    h.play(false);
    int saturated = 0;
    for (int k = 0; k < 70 && !h.finished(); ++k) {
        std::fill(std::begin(block), std::end(block), std::int16_t(0));
        h.mix(block, 800);
        for (std::int16_t v : block) saturated += v == 32767 || v == -32768;
    }
    CHECK(saturated == 0);
    CHECK(double(h.peak()) <= b3music::Limiter::kCeiling * 32767.0 + 1.0);
    CHECK(double(h.peak()) >= b3music::Limiter::kCeiling * 32767.0 * 0.98);
    CHECK(h.limiter().limited() > 40000);
    CHECK_NEAR(double(h.limiter().min_gain()), b3music::Limiter::kCeiling / 2.0, 0.01);
    fs::remove_all(dir, ec);
}

// robust2: a float WAV with NaN / Inf samples: silence there, never a full-scale click (a NaN passed the limiter)
TEST(float_wav_nan_inf_are_silence) {
    namespace fs = std::filesystem;
    const fs::path dir = fs::path("build") / "test_custom_music_nan.d";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    const std::string wav = (dir / "Bad Floats.wav").string();
    const std::uint32_t rate = 48000, frames = 24000;
    Bytes b;
    put(b, "RIFF");
    le32(b, 36 + frames * 8);
    put(b, "WAVEfmt ");
    le32(b, 16);
    le16(b, 3);   // IEEE float
    le16(b, 2);
    le32(b, rate);
    le32(b, rate * 8);
    le16(b, 8);
    le16(b, 32);
    put(b, "data");
    le32(b, frames * 8);
    for (std::uint32_t k = 0; k < frames; ++k) {
        float v = 0.5f * float(std::sin(2.0 * 3.14159265358979 * 440.0 * double(k) / double(rate)));
        if (k % 997 == 0) v = std::numeric_limits<float>::quiet_NaN();
        else if (k % 1499 == 0) v = std::numeric_limits<float>::infinity();
        else if (k % 2003 == 0) v = -std::numeric_limits<float>::infinity();
        std::uint32_t u;
        std::memcpy(&u, &v, 4);
        le32(b, u);
        le32(b, u);
    }
    { std::FILE* f = std::fopen(wav.c_str(), "wb"); if (f) { std::fwrite(b.data(), 1, b.size(), f); std::fclose(f); } }
    std::string why;
    const std::vector<b3music::Track> tracks = b3music::scan_folder(dir.string(), &why);
    CHECK(tracks.size() == 1);
    if (tracks.size() != 1) return;
    b3music::Player p;
    CHECK(p.start(tracks[0], 0, &why));
    p.set_gain(1.0f);
    p.play(false);
    std::int16_t block[2 * 800];
    int loud = 0, sound = 0;
    for (int k = 0; k < 40 && !p.finished(); ++k) {
        std::fill(std::begin(block), std::end(block), std::int16_t(0));
        p.mix(block, 800);
        for (std::int16_t v : block) {
            loud += v > 17000 || v < -17000;   // over the file's 0.5 (16384)
            sound += v != 0;
        }
    }
    CHECK(loud == 0);
    CHECK(sound > 20000);   // the tone itself still plays
    CHECK(double(p.peak()) <= 0.5 * 32767.0 + 2.0);
    fs::remove_all(dir, ec);
}

TEST(music_limiter) {
    using b3music::Limiter;
    constexpr std::size_t D = Limiter::kLookahead - 1;   // the delay
    // below the ceiling: the input exactly, D frames later
    {
        Limiter lim;
        std::vector<float> in(2 * 4000), out;
        for (std::size_t k = 0; k < 4000; ++k) {
            in[2 * k] = 0.8f * float(std::sin(0.05 * double(k)));
            in[2 * k + 1] = -0.5f * float(std::sin(0.031 * double(k)));
        }
        out = in;
        for (std::size_t at = 0; at < 4000; at += 700) lim.run(out.data() + 2 * at, std::min<std::size_t>(700, 4000 - at));   // any block size
        bool exact = true;
        for (std::size_t k = 0; k < D; ++k) exact = exact && out[2 * k] == 0.0f && out[2 * k + 1] == 0.0f;
        for (std::size_t k = D; k < 4000; ++k) exact = exact && out[2 * k] == in[2 * (k - D)] && out[2 * k + 1] == in[2 * (k - D) + 1];
        CHECK(exact);
        CHECK(lim.limited() == 0);
    }
    // a burst at 4x full scale between quiet parts: the output never above the ceiling, the gain smooth (no step larger
    // than a ramp over the look-ahead), back to 1 after the release
    {
        Limiter lim;
        const std::size_t n = 48000;
        std::vector<float> x(2 * n);
        for (std::size_t k = 0; k < n; ++k) {
            const float a = (k >= 10000 && k < 14000) ? 4.0f : 0.25f;
            x[2 * k] = a * float(std::sin(0.07 * double(k)));
            x[2 * k + 1] = a * float(std::cos(0.07 * double(k)));
        }
        std::vector<float> y = x;
        lim.run(y.data(), n);
        float top = 0.0f, worst_step = 0.0f, prev_g = 1.0f;
        for (std::size_t k = D; k < n; ++k) {
            top = std::max(top, std::max(std::fabs(y[2 * k]), std::fabs(y[2 * k + 1])));
            const float in_l = x[2 * (k - D)], in_r = x[2 * (k - D) + 1];
            const float m = std::max(std::fabs(in_l), std::fabs(in_r));
            if (m > 0.1f) {   // the gain this frame (every frame: the quiet part's peak is at least 0.177)
                const float g = std::max(std::fabs(y[2 * k]), std::fabs(y[2 * k + 1])) / m;
                worst_step = std::max(worst_step, std::fabs(g - prev_g));
                prev_g = g;
            }
        }
        CHECK(top <= Limiter::kCeiling * 1.0001f);
        CHECK(top >= Limiter::kCeiling * 0.99f);
        CHECK(worst_step < 0.75f / float(Limiter::kLookahead) + 0.01f);
        CHECK(lim.min_gain() < Limiter::kCeiling / 4.0f + 0.01f && lim.min_gain() > Limiter::kCeiling / 4.0f - 0.05f);
        // the quiet part after the burst: back at the input (within 1 %) a second later
        CHECK_NEAR(double(y[2 * (n - 1)]), double(x[2 * (n - 1 - D)]), 0.0025);
    }
}

TEST(ea_trax_modes_store) {
    namespace fs = std::filesystem;
    CHECK(b3music::mode_from_word("both") == 7 && b3music::mode_from_word("races") == 1 && b3music::mode_from_word("menus") == 4 &&
          b3music::mode_from_word("off") == 0 && b3music::mode_from_word("?") == 7);
    CHECK(std::string(b3music::mode_word(7)) == "both" && std::string(b3music::mode_word(1)) == "races" &&
          std::string(b3music::mode_word(4)) == "menus" && std::string(b3music::mode_word(0)) == "off" &&
          std::string(b3music::mode_word(0xF)) == "both" && std::string(b3music::mode_word(2)) == "off");
    CHECK(b3music::relative_key("/m/dir", "/m/dir/sub/a=b.mp3") == "sub/a=b.mp3");
    CHECK(b3music::relative_key("C:\\Music", "C:\\Music\\x\\y.flac") == "x/y.flac");
    CHECK(b3music::relative_key("/m/dir/", "/m/dir/z.wav") == "z.wav");
    b3music::ModeStore st;
    CHECK(st.get("/m", "a.mp3") == b3music::kModeBoth);   // absent: both
    st.set("/m", "a.mp3", b3music::kModeOff);
    st.set("/m", "[live] b=c.mp3", b3music::kModeRaces);
    st.set("/n", "a.mp3", b3music::kModeMenus);
    st.set("/m", "a.mp3", 0xF);   // the "not heard" bit is not kept
    CHECK(st.get("/m", "a.mp3") == b3music::kModeBoth);
    st.set("/m", "a.mp3", b3music::kModeOff);
    b3music::ModeStore back;
    CHECK(back.parse(st.text()));
    CHECK(back.get("/m", "a.mp3") == b3music::kModeOff);
    CHECK(back.get("/m", "[live] b=c.mp3") == b3music::kModeRaces);
    CHECK(back.get("/n", "a.mp3") == b3music::kModeMenus);
    CHECK(back.get("/n", "[live] b=c.mp3") == b3music::kModeBoth);
    // a file: written through a temporary, read back; a missing file is an empty store
    std::error_code ec;
    const fs::path dir = fs::path("build") / "test_custom_music_modes.d";
    fs::remove_all(dir, ec);
    const std::u8string u = (dir / "sub" / "music_modes.ini").u8string();
    const std::string file(u.begin(), u.end());
    b3music::ModeStore none;
    CHECK(!none.load(file));
    CHECK(none.get("/m", "a.mp3") == b3music::kModeBoth);
    st.set_file(file);
    std::string why;
    CHECK(st.save(&why));
    b3music::ModeStore disk;
    CHECK(disk.load(file));
    CHECK(disk.get("/m", "a.mp3") == b3music::kModeOff && disk.get("/n", "a.mp3") == b3music::kModeMenus);
    CHECK(!fs::exists(dir / "sub" / "music_modes.ini.tmp"));
    fs::remove_all(dir, ec);
}

// musicloud: EBU R128 against EBU Tech 3341's cases (1 kHz stereo sines; +-0.1 LU), the gates, the gain rule, the cache
namespace {
// `seconds` of a 1 kHz sine at `lufs` (a stereo 1 kHz sine of peak A reads 20 log10 A LUFS: the K-weighting is ~0 dB
// there and the -0.691 offsets the two channels' sum)
void sine(std::vector<float>& lr, unsigned rate, double seconds, double lufs) {
    const double a = lufs < -200.0 ? 0.0 : std::pow(10.0, lufs / 20.0), pi = 3.14159265358979323846;
    const std::size_t n = std::size_t(seconds * rate), at = lr.size() / 2;
    for (std::size_t k = 0; k < n; ++k) {
        const float v = float(a * std::sin(2.0 * pi * 1000.0 * double(at + k) / rate));
        lr.push_back(v), lr.push_back(v);
    }
}
double meter(unsigned rate, const std::vector<float>& lr) {
    b3music::LoudnessMeter m(rate);
    m.add(lr.data(), lr.size() / 2);
    return m.integrated();
}
}  // namespace

TEST(loudness_r128) {
    for (unsigned rate : {44100u, 48000u}) {
        std::vector<float> a;   // Tech 3341 case 1: -23 dBFS, 20 s
        sine(a, rate, 20.0, -23.0);
        CHECK(std::fabs(meter(rate, a) + 23.0) < 0.1);
        std::vector<float> b;   // case 2: -33 dBFS
        sine(b, rate, 20.0, -33.0);
        CHECK(std::fabs(meter(rate, b) + 33.0) < 0.1);
        std::vector<float> c;   // case 3: -36, -23, -36 (10 / 60 / 10 s): the relative gate drops the quiet ends
        sine(c, rate, 10.0, -36.0), sine(c, rate, 60.0, -23.0), sine(c, rate, 10.0, -36.0);
        CHECK(std::fabs(meter(rate, c) + 23.0) < 0.1);
        std::vector<float> d;   // case 5: -26, -20, -26 (20 / 20.1 / 20 s)
        sine(d, rate, 20.0, -26.0), sine(d, rate, 20.1, -20.0), sine(d, rate, 20.0, -26.0);
        CHECK(std::fabs(meter(rate, d) + 23.0) < 0.1);
        std::vector<float> e;   // silence around: the absolute gate (the six 400 ms blocks across the edges, partly
        sine(e, rate, 10.0, -300.0), sine(e, rate, 10.0, -23.0), sine(e, rate, 10.0, -300.0);   // the tone, count: -23.13)
        CHECK(std::fabs(meter(rate, e) + 23.13) < 0.05);
        b3music::LoudnessMeter m(rate);   // case 4's tone peak, and nothing gated in
        m.add(e.data(), e.size() / 2);
        CHECK(std::fabs(m.peak() - std::pow(10.0, -23.0 / 20.0)) < 1e-4);
    }
    std::vector<float> z;
    sine(z, 48000, 5.0, -300.0);
    CHECK(std::isinf(meter(48000, z)));
    std::vector<float> s;   // too short for one 400 ms block
    sine(s, 48000, 0.3, -20.0);
    CHECK(std::isinf(meter(48000, s)));
}

TEST(loudness_gain_rule) {
    const auto db = [](float g) { return 20.0 * std::log10(double(g)); };
    const double t = b3music::kLoudnessTarget;
    CHECK(std::fabs(db(b3music::loudness_gain(t, 1.0))) < 1e-4);                 // at the target: as is
    CHECK(std::fabs(db(b3music::loudness_gain(t + 6.0, 1.0)) + 6.0) < 1e-3);     // louder: turned down
    CHECK(std::fabs(db(b3music::loudness_gain(t - 40.0, 0.001)) - 12.0) < 1e-3); // very quiet: at most +12 dB
    CHECK(std::fabs(db(b3music::loudness_gain(t + 40.0, 1.0)) + 20.0) < 1e-3);   // very loud: at most -20 dB
    // quiet with full-scale peaks: raised only to kLimitAllowance dB over the Limiter's ceiling
    const double room = 20.0 * std::log10(double(b3music::Limiter::kCeiling)) + b3music::kLimitAllowance;
    CHECK(std::fabs(db(b3music::loudness_gain(t - 8.0, 1.0)) - room) < 1e-3);
    // peaks low enough: the whole way
    CHECK(std::fabs(db(b3music::loudness_gain(t - 8.0, 0.25)) - 8.0) < 1e-3);
    CHECK(b3music::loudness_gain(-HUGE_VAL, 0.0) == 1.0f);   // nothing measured: as is
}

TEST(loudness_cache_and_service) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path dir = fs::path("build") / "test_custom_music_loud.d";
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    const std::string quiet = (dir / "quiet.wav").string(), loud = (dir / "loud.wav").string();
    write_wav(quiet, 48000, 2, 1000.0, 3.0, 0.05);
    write_wav(loud, 44100, 2, 1000.0, 3.0, 0.9);
    double lq = 0.0, pq = 0.0, ll = 0.0, pl = 0.0;
    CHECK(b3music::measure_loudness(quiet, lq, pq));
    CHECK(b3music::measure_loudness(loud, ll, pl));
    CHECK(std::fabs(lq - 20.0 * std::log10(0.05)) < 0.2 && std::fabs(pq - 0.05) < 1e-3);
    CHECK(std::fabs(ll - 20.0 * std::log10(0.9)) < 0.2 && std::fabs(pl - 0.9) < 1e-3);
    double x = 0.0, y = 0.0;
    CHECK(!b3music::measure_loudness((dir / "none.wav").string(), x, y));

    const std::string file = (dir / "cache" / "music_loudness.txt").string();
    float g1 = 0.0f, g2 = 0.0f, g3 = 0.0f;
    {
        b3music::LoudnessService svc;
        svc.set_cache_file(file);
        float g = 0.0f;
        CHECK(!svc.lookup(quiet, g));
        svc.start({quiet, loud, (dir / "none.wav").string()});
        CHECK(svc.need(quiet, g1));   // measured now or by the worker: the same value
        svc.finish();
        CHECK(svc.lookup(loud, g2));
        CHECK(svc.measured() >= 2);
    }
    CHECK(fs::exists(file) && !fs::exists(file + ".tmp"));
    {   // a new run: from the file, nothing measured, the same gains
        b3music::LoudnessService svc;
        svc.set_cache_file(file);
        CHECK(svc.lookup(quiet, g3));
        CHECK(g3 == g1);
        float g = 0.0f;
        CHECK(svc.lookup(loud, g) && g == g2);
        CHECK(svc.measured() == 0);
        CHECK(g1 > 1.0f && g2 < 1.0f);
    }
    {   // a changed file is measured again
        write_wav(quiet, 48000, 2, 1000.0, 4.0, 0.05);
        b3music::LoudnessCache c;
        c.load(file);
        b3music::LoudnessCache::Entry e;
        CHECK(!c.get(quiet, e));
        CHECK(c.get(loud, e));
    }
    {   // a big file: left to the worker (need() false, gain 1), its gain there once the worker is done
        const std::string big = (dir / "big.wav").string();
        write_wav(big, 48000, 2, 1000.0, 2.0, 0.05);
        b3music::LoudnessService svc;
        svc.set_cache_file(file);
        svc.now_bytes = 1000;   // (x5 for a WAV: still under the file's 384 kB)
        float g = 0.0f;
        CHECK(!svc.need(big, g) && g == 1.0f);   // no worker running: one started for it
        svc.finish();
        CHECK(svc.lookup(big, g) && g > 1.0f);
        std::atomic<bool> stop{true};   // a measure cancelled
        double l = 0.0, pk = 0.0;
        bool stopped = false;
        CHECK(!b3music::measure_loudness(big, l, pk, nullptr, &stop, &stopped) && stopped);
        fs::remove(big, ec);
    }
    {   // a song no longer there is dropped at the load
        b3music::LoudnessCache c;
        c.load(file);
        b3music::LoudnessCache::Entry e;
        CHECK(c.text().find("big.wav") == std::string::npos);
        CHECK(c.get(loud, e));
    }
    b3music::LoudnessCache p;   // the text form round trip, a bad line skipped
    CHECK(!p.parse("# c\n-12.50\t0.50000\t10\t20\t/a b/c.mp3\nbroken line\n-inf\t0.00000\t1\t2\t/s.wav\n"));
    const std::string t = p.text();
    CHECK(t.find("-12.50\t0.50000\t10\t20\t/a b/c.mp3\n") != std::string::npos);
    CHECK(t.find("-inf\t0.00000\t1\t2\t/s.wav\n") != std::string::npos);
    fs::remove_all(dir, ec);
}

TEST_MAIN("custom_music (remaster-features §6: tags, folder, decoders, player)")
