// sign.cpp -- see sign.h
#include "sign.h"

#include <cstring>

namespace b3launcher::sign {

// ---- SHA-512 (FIPS 180-4) ----

namespace {
inline std::uint64_t rotr(std::uint64_t x, int n) { return (x >> n) | (x << (64 - n)); }
}  // namespace

std::array<std::uint8_t, 64> sha512(const void* data, std::size_t len) {
    // the first 64 bits of the fractional parts of the cube roots of the first 80 primes (k) and of the square roots of
    // the first 8 (h), computed exactly with integer roots
    static constexpr std::uint64_t k[80] = {
        0x428a2f98d728ae22ull, 0x7137449123ef65cdull, 0xb5c0fbcfec4d3b2full, 0xe9b5dba58189dbbcull,
        0x3956c25bf348b538ull, 0x59f111f1b605d019ull, 0x923f82a4af194f9bull, 0xab1c5ed5da6d8118ull,
        0xd807aa98a3030242ull, 0x12835b0145706fbeull, 0x243185be4ee4b28cull, 0x550c7dc3d5ffb4e2ull,
        0x72be5d74f27b896full, 0x80deb1fe3b1696b1ull, 0x9bdc06a725c71235ull, 0xc19bf174cf692694ull,
        0xe49b69c19ef14ad2ull, 0xefbe4786384f25e3ull, 0x0fc19dc68b8cd5b5ull, 0x240ca1cc77ac9c65ull,
        0x2de92c6f592b0275ull, 0x4a7484aa6ea6e483ull, 0x5cb0a9dcbd41fbd4ull, 0x76f988da831153b5ull,
        0x983e5152ee66dfabull, 0xa831c66d2db43210ull, 0xb00327c898fb213full, 0xbf597fc7beef0ee4ull,
        0xc6e00bf33da88fc2ull, 0xd5a79147930aa725ull, 0x06ca6351e003826full, 0x142929670a0e6e70ull,
        0x27b70a8546d22ffcull, 0x2e1b21385c26c926ull, 0x4d2c6dfc5ac42aedull, 0x53380d139d95b3dfull,
        0x650a73548baf63deull, 0x766a0abb3c77b2a8ull, 0x81c2c92e47edaee6ull, 0x92722c851482353bull,
        0xa2bfe8a14cf10364ull, 0xa81a664bbc423001ull, 0xc24b8b70d0f89791ull, 0xc76c51a30654be30ull,
        0xd192e819d6ef5218ull, 0xd69906245565a910ull, 0xf40e35855771202aull, 0x106aa07032bbd1b8ull,
        0x19a4c116b8d2d0c8ull, 0x1e376c085141ab53ull, 0x2748774cdf8eeb99ull, 0x34b0bcb5e19b48a8ull,
        0x391c0cb3c5c95a63ull, 0x4ed8aa4ae3418acbull, 0x5b9cca4f7763e373ull, 0x682e6ff3d6b2b8a3ull,
        0x748f82ee5defb2fcull, 0x78a5636f43172f60ull, 0x84c87814a1f0ab72ull, 0x8cc702081a6439ecull,
        0x90befffa23631e28ull, 0xa4506cebde82bde9ull, 0xbef9a3f7b2c67915ull, 0xc67178f2e372532bull,
        0xca273eceea26619cull, 0xd186b8c721c0c207ull, 0xeada7dd6cde0eb1eull, 0xf57d4f7fee6ed178ull,
        0x06f067aa72176fbaull, 0x0a637dc5a2c898a6ull, 0x113f9804bef90daeull, 0x1b710b35131c471bull,
        0x28db77f523047d84ull, 0x32caab7b40c72493ull, 0x3c9ebe0a15c9bebcull, 0x431d67c49c100d4cull,
        0x4cc5d4becb3e42b6ull, 0x597f299cfc657e2aull, 0x5fcb6fab3ad6faecull, 0x6c44198c4a475817ull,
    };
    std::uint64_t h[8] = {0x6a09e667f3bcc908ull, 0xbb67ae8584caa73bull, 0x3c6ef372fe94f82bull, 0xa54ff53a5f1d36f1ull,
                          0x510e527fade682d1ull, 0x9b05688c2b3e6c1full, 0x1f83d9abfb41bd6bull, 0x5be0cd19137e2179ull};
    const auto* p = static_cast<const std::uint8_t*>(data);
    // the message, a 1 bit, zeros, and its length in bits (128-bit big-endian) to a multiple of 128 bytes
    const std::size_t total = ((len + 17 + 127) / 128) * 128;
    std::vector<std::uint8_t> m(total, 0);
    if (len) std::memcpy(m.data(), p, len);
    m[len] = 0x80;
    const std::uint64_t bits = std::uint64_t(len) * 8;
    for (int i = 0; i < 8; ++i) m[total - 1 - i] = std::uint8_t(bits >> (8 * i));
    for (std::size_t off = 0; off < total; off += 128) {
        std::uint64_t w[80];
        for (int t = 0; t < 16; ++t) {
            std::uint64_t v = 0;
            for (int b = 0; b < 8; ++b) v = (v << 8) | m[off + std::size_t(8 * t + b)];
            w[t] = v;
        }
        for (int t = 16; t < 80; ++t) {
            const std::uint64_t s0 = rotr(w[t - 15], 1) ^ rotr(w[t - 15], 8) ^ (w[t - 15] >> 7);
            const std::uint64_t s1 = rotr(w[t - 2], 19) ^ rotr(w[t - 2], 61) ^ (w[t - 2] >> 6);
            w[t] = w[t - 16] + s0 + w[t - 7] + s1;
        }
        std::uint64_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int t = 0; t < 80; ++t) {
            const std::uint64_t S1 = rotr(e, 14) ^ rotr(e, 18) ^ rotr(e, 41);
            const std::uint64_t ch = (e & f) ^ (~e & g);
            const std::uint64_t t1 = hh + S1 + ch + k[t] + w[t];
            const std::uint64_t S0 = rotr(a, 28) ^ rotr(a, 34) ^ rotr(a, 39);
            const std::uint64_t maj = (a & b) ^ (a & c) ^ (b & c);
            const std::uint64_t t2 = S0 + maj;
            hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }
    std::array<std::uint8_t, 64> out{};
    for (int i = 0; i < 8; ++i)
        for (int b = 0; b < 8; ++b) out[std::size_t(8 * i + b)] = std::uint8_t(h[i] >> (56 - 8 * b));
    return out;
}

// ---- Ed25519 (RFC 8032) ----

namespace {

using u128 = unsigned __int128;

// An element of GF(2^255 - 19): five 51-bit limbs (each may run a little over 51 bits between reductions)
struct Fe { std::uint64_t v[5]; };
constexpr std::uint64_t kMask51 = (std::uint64_t(1) << 51) - 1;

Fe fe(std::uint64_t x) { return Fe{{x & kMask51, x >> 51, 0, 0, 0}}; }
Fe carry(Fe a) {
    for (int r = 0; r < 2; ++r) {
        for (int i = 0; i < 4; ++i) { a.v[i + 1] += a.v[i] >> 51; a.v[i] &= kMask51; }
        a.v[0] += 19 * (a.v[4] >> 51);
        a.v[4] &= kMask51;
    }
    return a;
}
Fe add(const Fe& a, const Fe& b) {
    Fe r;
    for (int i = 0; i < 5; ++i) r.v[i] = a.v[i] + b.v[i];
    return carry(r);
}
Fe sub(const Fe& a, const Fe& b) {   // a + 4p - b: no limb goes negative
    static constexpr std::uint64_t four_p[5] = {4 * (kMask51 - 18), 4 * kMask51, 4 * kMask51, 4 * kMask51, 4 * kMask51};
    Fe r;
    for (int i = 0; i < 5; ++i) r.v[i] = a.v[i] + four_p[i] - b.v[i];
    return carry(r);
}
Fe mul(const Fe& a, const Fe& b) {
    u128 t[5] = {0, 0, 0, 0, 0};
    for (int i = 0; i < 5; ++i)
        for (int j = 0; j < 5; ++j) {
            const u128 pr = u128(a.v[i]) * b.v[j];
            if (i + j < 5) t[i + j] += pr;
            else t[i + j - 5] += pr * 19;   // 2^255 = 19
        }
    Fe r;
    u128 c = 0;
    for (int i = 0; i < 5; ++i) {
        t[i] += c;
        r.v[i] = std::uint64_t(t[i]) & kMask51;
        c = t[i] >> 51;
    }
    r.v[0] += std::uint64_t(c) * 19;
    return carry(r);
}
Fe sq(const Fe& a) { return mul(a, a); }
// a^e for an exponent given as 32 little-endian bytes
Fe pow_bytes(const Fe& a, const std::uint8_t e[32]) {
    Fe r = fe(1);
    for (int i = 255; i >= 0; --i) {
        r = sq(r);
        if ((e[i / 8] >> (i % 8)) & 1) r = mul(r, a);
    }
    return r;
}
// the canonical 32 bytes (fully reduced below p)
void fe_bytes(Fe a, std::uint8_t out[32]) {
    a = carry(a);
    // subtract p once if a >= p: a + 19 overflows 2^255 exactly then
    std::uint64_t q = (a.v[0] + 19) >> 51;
    for (int i = 1; i < 5; ++i) q = (a.v[i] + q) >> 51;
    a.v[0] += 19 * q;
    for (int i = 0; i < 4; ++i) { a.v[i + 1] += a.v[i] >> 51; a.v[i] &= kMask51; }
    a.v[4] &= kMask51;
    std::memset(out, 0, 32);
    int bit = 0;
    for (int i = 0; i < 5; ++i)
        for (int b = 0; b < 51; ++b, ++bit)
            if (bit < 256 && ((a.v[i] >> b) & 1)) out[bit / 8] |= std::uint8_t(1u << (bit % 8));
}
Fe fe_from_bytes(const std::uint8_t in[32]) {   // the low 255 bits
    Fe r{{0, 0, 0, 0, 0}};
    for (int bit = 0; bit < 255; ++bit)
        if ((in[bit / 8] >> (bit % 8)) & 1) r.v[bit / 51] |= std::uint64_t(1) << (bit % 51);
    return r;
}
bool fe_eq(const Fe& a, const Fe& b) {
    std::uint8_t x[32], y[32];
    fe_bytes(a, x);
    fe_bytes(b, y);
    return std::memcmp(x, y, 32) == 0;
}
bool fe_odd(const Fe& a) {
    std::uint8_t x[32];
    fe_bytes(a, x);
    return x[0] & 1;
}
// p - 2 (inverse), (p - 5) / 8 (square root candidate), (p - 1) / 4 (sqrt(-1) = 2^((p-1)/4)), as little-endian bytes
void exp_bytes(int which, std::uint8_t e[32]) {
    // p = 2^255 - 19 = 0x7fff...ffed
    std::uint8_t p[32];
    std::memset(p, 0xff, 32);
    p[0] = 0xed;
    p[31] = 0x7f;
    std::memcpy(e, p, 32);
    auto sub_small = [&](unsigned v) {
        for (int i = 0; i < 32 && v; ++i) {
            const unsigned cur = e[i];
            e[i] = std::uint8_t(cur - (v & 0xff));
            v = (v >> 8) + (cur < (v & 0xff) ? 1u : 0u);
        }
    };
    auto shr = [&](int n) {
        for (int k = 0; k < n; ++k) {
            for (int i = 0; i < 32; ++i) e[i] = std::uint8_t((e[i] >> 1) | (i + 1 < 32 ? (e[i + 1] & 1) << 7 : 0));
        }
    };
    if (which == 0) sub_small(2);
    else if (which == 1) { sub_small(5); shr(3); }
    else { sub_small(1); shr(2); }
}
Fe inv(const Fe& a) {
    std::uint8_t e[32];
    exp_bytes(0, e);
    return pow_bytes(a, e);
}

struct Point { Fe x, y, z, t; };   // extended coordinates: x = X/Z, y = Y/Z, x y = T/Z

struct Curve {
    Fe d, d2, sqrtm1;
    Point base;
    bool ok = false;
};
bool decode_point(const Curve& c, const std::uint8_t in[32], Point& out);
const Curve& curve() {
    static const Curve c = [] {
        Curve k;
        k.d = mul(sub(fe(0), fe(121665)), inv(fe(121666)));   // -121665 / 121666
        k.d2 = add(k.d, k.d);
        std::uint8_t e[32];
        exp_bytes(2, e);
        k.sqrtm1 = pow_bytes(fe(2), e);
        // B: y = 4 / 5, x even
        std::uint8_t yb[32];
        fe_bytes(mul(fe(4), inv(fe(5))), yb);
        k.ok = decode_point(k, yb, k.base);
        return k;
    }();
    return c;
}
bool decode_point(const Curve& c, const std::uint8_t in[32], Point& out) {
    std::uint8_t yb[32];
    std::memcpy(yb, in, 32);
    const bool sign = yb[31] >> 7;
    yb[31] &= 0x7f;
    // y must be below p
    std::uint8_t pb[32];
    std::memset(pb, 0xff, 32);
    pb[0] = 0xed;
    pb[31] = 0x7f;
    for (int i = 31; i >= 0; --i) {
        if (yb[i] < pb[i]) break;
        if (yb[i] > pb[i]) return false;
        if (i == 0) return false;   // y == p
    }
    const Fe y = fe_from_bytes(yb);
    const Fe y2 = sq(y);
    const Fe u = sub(y2, fe(1)), v = add(mul(c.d, y2), fe(1));
    // x = u v^3 (u v^7)^((p-5)/8)
    const Fe v3 = mul(sq(v), v), v7 = mul(sq(v3), v);
    std::uint8_t e[32];
    exp_bytes(1, e);
    Fe x = mul(mul(u, v3), pow_bytes(mul(u, v7), e));
    const Fe vx2 = mul(v, sq(x));
    if (!fe_eq(vx2, u)) {
        if (!fe_eq(vx2, sub(fe(0), u))) return false;
        x = mul(x, c.sqrtm1);
    }
    const bool zero = fe_eq(x, fe(0));
    if (zero && sign) return false;
    if (fe_odd(x) != sign) x = sub(fe(0), x);
    out = Point{x, y, fe(1), mul(x, y)};
    return true;
}
Point padd(const Curve& c, const Point& p, const Point& q) {
    const Fe a = mul(sub(p.y, p.x), sub(q.y, q.x));
    const Fe b = mul(add(p.y, p.x), add(q.y, q.x));
    const Fe cc = mul(mul(p.t, c.d2), q.t);
    const Fe dd = mul(add(p.z, p.z), q.z);
    const Fe e = sub(b, a), f = sub(dd, cc), g = add(dd, cc), h = add(b, a);
    return Point{mul(e, f), mul(g, h), mul(f, g), mul(e, h)};
}
Point scalar_mul(const Curve& c, const Point& p, const std::uint8_t s[32]) {
    Point r{fe(0), fe(1), fe(1), fe(0)};   // the neutral element
    for (int i = 255; i >= 0; --i) {
        r = padd(c, r, r);
        if ((s[i / 8] >> (i % 8)) & 1) r = padd(c, r, p);
    }
    return r;
}
void encode_point(const Point& p, std::uint8_t out[32]) {
    const Fe zi = inv(p.z);
    const Fe x = mul(p.x, zi), y = mul(p.y, zi);
    fe_bytes(y, out);
    if (fe_odd(x)) out[31] |= 0x80;
}

// The group order L = 2^252 + 27742317777372353535851937790883648493, little-endian
constexpr std::uint8_t kL[32] = {0xed, 0xd3, 0xf5, 0x5c, 0x1a, 0x63, 0x12, 0x58, 0xd6, 0x9c, 0xf7, 0xa2, 0xde, 0xf9, 0xde, 0x14,
                                 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10};

// a (little-endian, n bytes) mod L, by bytes from the top: r = r * 256 + byte, then r -= L while r >= L
void mod_l(const std::uint8_t* a, std::size_t n, std::uint8_t out[32]) {
    std::uint32_t r[9] = {};   // r < 256 L < 2^261: nine 32-bit limbs
    std::uint32_t l[9] = {};
    for (int i = 0; i < 32; ++i) l[i / 4] |= std::uint32_t(kL[i]) << (8 * (i % 4));
    auto ge = [&]() {
        for (int i = 8; i >= 0; --i) {
            if (r[i] != l[i]) return r[i] > l[i];
        }
        return true;
    };
    for (std::size_t k = n; k-- > 0;) {
        std::uint32_t c = a[k];
        for (int i = 0; i < 9; ++i) {
            const std::uint64_t v = (std::uint64_t(r[i]) << 8) | c;
            r[i] = std::uint32_t(v);
            c = std::uint32_t(v >> 32);
        }
        while (ge()) {
            std::uint64_t borrow = 0;
            for (int i = 0; i < 9; ++i) {
                const std::uint64_t v = std::uint64_t(r[i]) - l[i] - borrow;
                r[i] = std::uint32_t(v);
                borrow = (v >> 63) & 1;
            }
        }
    }
    for (int i = 0; i < 32; ++i) out[i] = std::uint8_t(r[i / 4] >> (8 * (i % 4)));
}
bool below_l(const std::uint8_t s[32]) {
    for (int i = 31; i >= 0; --i) {
        if (s[i] != kL[i]) return s[i] < kL[i];
    }
    return false;
}

}  // namespace

bool ed25519_verify(const std::uint8_t pub[32], const void* msg, std::size_t len, const std::uint8_t sig[64]) {
    const Curve& c = curve();
    if (!c.ok) return false;
    Point a, r;
    if (!decode_point(c, pub, a) || !decode_point(c, sig, r)) return false;
    const std::uint8_t* s = sig + 32;
    if (!below_l(s)) return false;
    // k = SHA-512(R || A || M) mod L
    std::vector<std::uint8_t> buf(64 + len);
    std::memcpy(buf.data(), sig, 32);
    std::memcpy(buf.data() + 32, pub, 32);
    if (len) std::memcpy(buf.data() + 64, msg, len);
    const std::array<std::uint8_t, 64> hk = sha512(buf.data(), buf.size());
    std::uint8_t k[32];
    mod_l(hk.data(), 64, k);
    // [S]B == R + [k]A
    const Point lhs = scalar_mul(c, c.base, s);
    const Point rhs = padd(c, r, scalar_mul(c, a, k));
    std::uint8_t le[32], re[32];
    encode_point(lhs, le);
    encode_point(rhs, re);
    return std::memcmp(le, re, 32) == 0;
}

// ---- base64, OpenSSH keys and signatures ----

bool base64_decode(std::string_view text, std::vector<std::uint8_t>& out) {
    out.clear();
    std::uint32_t acc = 0;
    int bits = 0, pad = 0;
    for (const char ch : text) {
        int v;
        if (ch >= 'A' && ch <= 'Z') v = ch - 'A';
        else if (ch >= 'a' && ch <= 'z') v = ch - 'a' + 26;
        else if (ch >= '0' && ch <= '9') v = ch - '0' + 52;
        else if (ch == '+') v = 62;
        else if (ch == '/') v = 63;
        else if (ch == '=') { ++pad; continue; }
        else if (ch == ' ' || ch == '\n' || ch == '\r' || ch == '\t') continue;
        else return false;
        if (pad) return false;   // data after padding
        acc = (acc << 6) | std::uint32_t(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(std::uint8_t(acc >> bits));
        }
    }
    return pad <= 2;
}

namespace {
// SSH wire format: uint32 big-endian length, then the bytes
struct Reader {
    const std::vector<std::uint8_t>& b;
    std::size_t at = 0;
    bool u32(std::uint32_t& v) {
        if (b.size() - at < 4) return false;
        v = std::uint32_t(b[at]) << 24 | std::uint32_t(b[at + 1]) << 16 | std::uint32_t(b[at + 2]) << 8 | b[at + 3];
        at += 4;
        return true;
    }
    bool str(std::vector<std::uint8_t>& s) {
        std::uint32_t n;
        if (!u32(n) || b.size() - at < n) return false;
        s.assign(b.begin() + std::ptrdiff_t(at), b.begin() + std::ptrdiff_t(at + n));
        at += n;
        return true;
    }
};
void put_str(std::vector<std::uint8_t>& o, const void* p, std::size_t n) {
    for (int i = 3; i >= 0; --i) o.push_back(std::uint8_t(n >> (8 * i)));
    const auto* c = static_cast<const std::uint8_t*>(p);
    o.insert(o.end(), c, c + n);
}
bool is(const std::vector<std::uint8_t>& v, std::string_view s) {
    return v.size() == s.size() && (s.empty() || std::memcmp(v.data(), s.data(), s.size()) == 0);
}
// a public key blob: string "ssh-ed25519", string key (32 bytes)
bool ed25519_blob(const std::vector<std::uint8_t>& blob, std::array<std::uint8_t, 32>& key) {
    Reader r{blob};
    std::vector<std::uint8_t> type, k;
    if (!r.str(type) || !is(type, "ssh-ed25519") || !r.str(k) || k.size() != 32 || r.at != blob.size()) return false;
    std::memcpy(key.data(), k.data(), 32);
    return true;
}
bool fail(std::string* why, const char* w) {
    if (why) *why = w;
    return false;
}
}  // namespace

bool parse_ssh_ed25519(std::string_view line, std::array<std::uint8_t, 32>& key) {
    while (!line.empty() && (line.front() == ' ' || line.front() == '\t')) line.remove_prefix(1);
    if (line.substr(0, 12) != "ssh-ed25519 ") return false;
    line.remove_prefix(12);
    const std::size_t end = line.find_first_of(" \t\r\n");
    std::vector<std::uint8_t> blob;
    return base64_decode(line.substr(0, end), blob) && ed25519_blob(blob, key);
}

bool verify_sshsig(std::string_view armoured, std::string_view message, std::string_view ns, const std::array<std::uint8_t, 32>& key,
                   std::string* why) {
    constexpr std::string_view kBegin = "-----BEGIN SSH SIGNATURE-----", kEnd = "-----END SSH SIGNATURE-----";
    const std::size_t b = armoured.find(kBegin), e = armoured.find(kEnd);
    if (b == std::string_view::npos || e == std::string_view::npos || e < b) return fail(why, "not an SSH signature");
    std::vector<std::uint8_t> blob;
    if (!base64_decode(armoured.substr(b + kBegin.size(), e - b - kBegin.size()), blob)) return fail(why, "the signature is not valid base64");
    if (blob.size() < 10 || std::memcmp(blob.data(), "SSHSIG", 6) != 0) return fail(why, "not an SSHSIG signature");
    Reader r{blob, 6};
    std::uint32_t version;
    std::vector<std::uint8_t> pub, sig_ns, reserved, hash_alg, sig;
    if (!r.u32(version) || version != 1 || !r.str(pub) || !r.str(sig_ns) || !r.str(reserved) || !r.str(hash_alg) || !r.str(sig))
        return fail(why, "a damaged signature");
    std::array<std::uint8_t, 32> signer{};
    if (!ed25519_blob(pub, signer)) return fail(why, "not an Ed25519 signature");
    if (signer != key) return fail(why, "signed by another key");
    if (!is(sig_ns, ns)) return fail(why, "signed for another purpose (namespace)");
    if (!is(hash_alg, "sha512")) return fail(why, "not a SHA-512 signature");
    Reader sr{sig};
    std::vector<std::uint8_t> sig_type, raw;
    if (!sr.str(sig_type) || !is(sig_type, "ssh-ed25519") || !sr.str(raw) || raw.size() != 64) return fail(why, "a damaged signature");
    // the signed data
    const std::array<std::uint8_t, 64> h = sha512(message.data(), message.size());
    std::vector<std::uint8_t> data(blob.begin(), blob.begin() + 6);   // "SSHSIG"
    put_str(data, sig_ns.data(), sig_ns.size());
    put_str(data, reserved.data(), reserved.size());
    put_str(data, hash_alg.data(), hash_alg.size());
    put_str(data, h.data(), h.size());
    if (!ed25519_verify(key.data(), data.data(), data.size(), raw.data())) return fail(why, "the signature does not match");
    return true;
}

}  // namespace b3launcher::sign
