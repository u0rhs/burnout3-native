// JSON and glTF 2.0 for the model export (include/b3modtool/gltf.h).
#include "b3modtool/gltf.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>

namespace b3modtool {

// ---- Json ----

Json Json::floats(const float* p, std::size_t n) {
    Json j = array();
    for (std::size_t i = 0; i < n; ++i) j.push(f32(p[i]));
    return j;
}

Json& Json::set(const std::string& key, Json v) {
    type = Obj;
    for (auto& kv : o)
        if (kv.first == key) { kv.second = std::move(v); return kv.second; }
    o.emplace_back(key, std::move(v));
    return o.back().second;
}

const Json* Json::get(std::string_view key) const {
    if (type != Obj) return nullptr;
    for (const auto& kv : o)
        if (kv.first == key) return &kv.second;
    return nullptr;
}
Json* Json::get(std::string_view key) { return const_cast<Json*>(static_cast<const Json*>(this)->get(key)); }

namespace {

void put_str(std::string& out, const std::string& s) {
    out += '"';
    for (unsigned char c : s) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20) { char t[8]; std::snprintf(t, sizeof t, "\\u%04x", c); out += t; }
            else out += char(c);
        }
    }
    out += '"';
}

void put_num(std::string& out, const Json& j) {
    char t[40];
    if (!std::isfinite(j.n)) { out += "null"; return; }   // not JSON: gltf_check reports what reads back as null
    if (j.kind == Json::Int) std::snprintf(t, sizeof t, "%lld", (long long)j.n);
    else if (j.kind == Json::Float) std::snprintf(t, sizeof t, "%.9g", double(float(j.n)));
    else std::snprintf(t, sizeof t, "%.17g", j.n);
    out += t;
}

bool scalar_array(const Json& j) {
    for (const Json& e : j.a)
        if (e.type == Json::Arr || e.type == Json::Obj) return false;
    return true;
}

void dump_rec(const Json& j, std::string& out, int indent, int depth) {
    auto nl = [&](int d) { if (indent >= 0) { out += '\n'; out.append(std::size_t(indent * d), ' '); } };
    switch (j.type) {
    case Json::Null: out += "null"; break;
    case Json::Bool: out += j.b ? "true" : "false"; break;
    case Json::Num: put_num(out, j); break;
    case Json::Str: put_str(out, j.s); break;
    case Json::Arr: {
        if (j.a.empty()) { out += "[]"; break; }
        const bool flat = indent < 0 || scalar_array(j);
        out += '[';
        for (std::size_t i = 0; i < j.a.size(); ++i) {
            if (i) out += flat && indent >= 0 ? ", " : ",";
            if (!flat) nl(depth + 1);
            dump_rec(j.a[i], out, indent, depth + 1);
        }
        if (!flat) nl(depth);
        out += ']';
        break;
    }
    case Json::Obj: {
        if (j.o.empty()) { out += "{}"; break; }
        out += '{';
        for (std::size_t i = 0; i < j.o.size(); ++i) {
            if (i) out += ',';
            nl(depth + 1);
            put_str(out, j.o[i].first);
            out += indent >= 0 ? ": " : ":";
            dump_rec(j.o[i].second, out, indent, depth + 1);
        }
        nl(depth);
        out += '}';
        break;
    }
    }
}

struct Parser {
    std::string_view t;
    std::size_t i = 0;
    std::string err;
    int depth = 0;

    void ws() { while (i < t.size() && (t[i] == ' ' || t[i] == '\t' || t[i] == '\n' || t[i] == '\r')) ++i; }
    bool fail(const std::string& m) { if (err.empty()) err = m + " at byte " + std::to_string(i); return false; }
    bool lit(const char* w) {
        const std::size_t n = std::strlen(w);
        if (t.substr(i, n) != w) return fail("bad literal");
        i += n;
        return true;
    }
    static void utf8(std::string& s, std::uint32_t c) {
        if (c < 0x80) s += char(c);
        else if (c < 0x800) { s += char(0xC0 | (c >> 6)); s += char(0x80 | (c & 0x3F)); }
        else if (c < 0x10000) { s += char(0xE0 | (c >> 12)); s += char(0x80 | ((c >> 6) & 0x3F)); s += char(0x80 | (c & 0x3F)); }
        else { s += char(0xF0 | (c >> 18)); s += char(0x80 | ((c >> 12) & 0x3F)); s += char(0x80 | ((c >> 6) & 0x3F)); s += char(0x80 | (c & 0x3F)); }
    }
    bool hex4(std::uint32_t& v) {
        if (i + 4 > t.size()) return fail("short \\u escape");
        v = 0;
        for (int k = 0; k < 4; ++k) {
            const char c = t[i++];
            v <<= 4;
            if (c >= '0' && c <= '9') v |= std::uint32_t(c - '0');
            else if (c >= 'a' && c <= 'f') v |= std::uint32_t(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= std::uint32_t(c - 'A' + 10);
            else return fail("bad \\u escape");
        }
        return true;
    }
    bool string(std::string& s) {
        if (i >= t.size() || t[i] != '"') return fail("string expected");
        ++i;
        while (true) {
            if (i >= t.size()) return fail("unterminated string");
            const char c = t[i++];
            if (c == '"') return true;
            if (static_cast<unsigned char>(c) < 0x20) return fail("control character in a string");
            if (c != '\\') { s += c; continue; }
            if (i >= t.size()) return fail("bad escape");
            const char e = t[i++];
            switch (e) {
            case '"': s += '"'; break;
            case '\\': s += '\\'; break;
            case '/': s += '/'; break;
            case 'b': s += '\b'; break;
            case 'f': s += '\f'; break;
            case 'n': s += '\n'; break;
            case 'r': s += '\r'; break;
            case 't': s += '\t'; break;
            case 'u': {
                std::uint32_t v;
                if (!hex4(v)) return false;
                if (v >= 0xD800 && v < 0xDC00 && t.substr(i, 2) == "\\u") {
                    i += 2;
                    std::uint32_t lo;
                    if (!hex4(lo)) return false;
                    if (lo >= 0xDC00 && lo < 0xE000) v = 0x10000 + ((v - 0xD800) << 10) + (lo - 0xDC00);
                }
                utf8(s, v);
                break;
            }
            default: return fail("bad escape");
            }
        }
    }
    bool value(Json& j) {
        if (++depth > 200) return fail("nesting too deep");
        ws();
        if (i >= t.size()) return fail("value expected");
        const char c = t[i];
        bool ok = true;
        if (c == '{') {
            j = Json::object();
            ++i;
            ws();
            if (i < t.size() && t[i] == '}') ++i;
            else
                while (ok) {
                    ws();
                    std::string k;
                    if (!string(k)) return false;
                    ws();
                    if (i >= t.size() || t[i] != ':') return fail("':' expected");
                    ++i;
                    Json v;
                    if (!value(v)) return false;
                    j.o.emplace_back(std::move(k), std::move(v));
                    ws();
                    if (i < t.size() && t[i] == ',') { ++i; continue; }
                    if (i < t.size() && t[i] == '}') { ++i; break; }
                    return fail("',' or '}' expected");
                }
        } else if (c == '[') {
            j = Json::array();
            ++i;
            ws();
            if (i < t.size() && t[i] == ']') ++i;
            else
                while (ok) {
                    Json v;
                    if (!value(v)) return false;
                    j.a.push_back(std::move(v));
                    ws();
                    if (i < t.size() && t[i] == ',') { ++i; continue; }
                    if (i < t.size() && t[i] == ']') { ++i; break; }
                    return fail("',' or ']' expected");
                }
        } else if (c == '"') {
            j = Json::str("");
            ok = string(j.s);
        } else if (c == 't') {
            ok = lit("true");
            j = Json::boolean(true);
        } else if (c == 'f') {
            ok = lit("false");
            j = Json::boolean(false);
        } else if (c == 'n') {
            ok = lit("null");
            j = Json();
        } else {
            const std::size_t s0 = i;
            bool frac = false;
            if (t[i] == '-') ++i;
            while (i < t.size() && ((t[i] >= '0' && t[i] <= '9') || t[i] == '.' || t[i] == 'e' || t[i] == 'E' || t[i] == '+' || t[i] == '-')) {
                if (t[i] == '.' || t[i] == 'e' || t[i] == 'E') frac = true;
                ++i;
            }
            if (i == s0) return fail("unexpected character");
            const std::string num(t.substr(s0, i - s0));
            char* end = nullptr;
            const double v = std::strtod(num.c_str(), &end);
            if (!end || *end) return fail("bad number");
            j = Json::num(v);
            if (!frac) j.kind = Json::Int;
        }
        --depth;
        return ok;
    }
};

}  // namespace

std::string Json::dump(int indent) const {
    std::string out;
    dump_rec(*this, out, indent, 0);
    return out;
}

bool json_parse(std::string_view text, Json& out, std::string* why) {
    Parser p;
    p.t = text;
    if (!p.value(out)) { if (why) *why = p.err; return false; }
    p.ws();
    if (p.i != text.size()) { if (why) *why = "text after the JSON value at byte " + std::to_string(p.i); return false; }
    return true;
}

std::string hex64(std::uint64_t v) {
    char t[24];
    std::snprintf(t, sizeof t, "%016llx", (unsigned long long)v);
    return t;
}

// ---- GltfBuilder ----

GltfBuilder::GltfBuilder() {
    doc = Json::object();
    Json asset = Json::object();
    asset.set("version", Json::str("2.0"));
    asset.set("generator", Json::str("b3modtool (Burnout 3 port mod kit)"));
    doc.set("asset", std::move(asset));
}

int GltfBuilder::add(const char* name, Json item) {
    Json* arr = doc.get(name);
    if (!arr) arr = &doc.set(name, Json::array());
    arr->push(std::move(item));
    return int(arr->a.size() - 1);
}

void GltfBuilder::use_extension(const std::string& name, bool required) {
    for (const char* key : {"extensionsUsed", "extensionsRequired"}) {
        if (key[10] == 'R' && !required) continue;
        Json* arr = doc.get(key);
        if (!arr) arr = &doc.set(key, Json::array());
        bool have = false;
        for (const Json& e : arr->a) have = have || e.s == name;
        if (!have) arr->push(Json::str(name));
    }
}

int GltfBuilder::view(const void* p, std::size_t n, int target) {
    while (bin.size() % 4) bin.push_back(0);
    const std::size_t at = bin.size();
    bin.insert(bin.end(), static_cast<const std::uint8_t*>(p), static_cast<const std::uint8_t*>(p) + n);
    Json v = Json::object();
    v.set("buffer", Json::integer(0));
    v.set("byteOffset", Json::integer(std::int64_t(at)));
    v.set("byteLength", Json::integer(std::int64_t(n)));
    if (target) v.set("target", Json::integer(target));
    return add("bufferViews", std::move(v));
}

int GltfBuilder::accessor(int view, int component, std::size_t count, const char* type, bool normalized,
                          const std::vector<double>* min, const std::vector<double>* max) {
    Json a = Json::object();
    a.set("bufferView", Json::integer(view));
    a.set("componentType", Json::integer(component));
    if (normalized) a.set("normalized", Json::boolean(true));
    a.set("count", Json::integer(std::int64_t(count)));
    a.set("type", Json::str(type));
    auto nums = [&](const std::vector<double>& v) {
        Json j = Json::array();
        for (double d : v) j.push(component == gl::kFloat ? Json::f32(float(d)) : Json::integer(std::int64_t(d)));
        return j;
    };
    if (min) a.set("min", nums(*min));
    if (max) a.set("max", nums(*max));
    return add("accessors", std::move(a));
}

int GltfBuilder::positions(const std::vector<std::array<float, 3>>& p) {
    std::vector<double> lo(3, 0), hi(3, 0);
    for (std::size_t i = 0; i < p.size(); ++i)
        for (int c = 0; c < 3; ++c) {
            if (!i || p[i][c] < lo[c]) lo[c] = p[i][c];
            if (!i || p[i][c] > hi[c]) hi[c] = p[i][c];
        }
    return accessor(view(p.data(), p.size() * 12, gl::kArrayBuffer), gl::kFloat, p.size(), "VEC3", false, &lo, &hi);
}
int GltfBuilder::vec3(const std::vector<std::array<float, 3>>& p) {
    return accessor(view(p.data(), p.size() * 12, gl::kArrayBuffer), gl::kFloat, p.size(), "VEC3");
}
int GltfBuilder::vec2(const std::vector<std::array<float, 2>>& p) {
    return accessor(view(p.data(), p.size() * 8, gl::kArrayBuffer), gl::kFloat, p.size(), "VEC2");
}
int GltfBuilder::vec4(const std::vector<std::array<float, 4>>& p) {
    return accessor(view(p.data(), p.size() * 16, gl::kArrayBuffer), gl::kFloat, p.size(), "VEC4");
}
int GltfBuilder::ubyte4(const std::vector<std::array<std::uint8_t, 4>>& p, bool normalized) {
    return accessor(view(p.data(), p.size() * 4, gl::kArrayBuffer), gl::kUByte, p.size(), "VEC4", normalized);
}
int GltfBuilder::byte4(const std::vector<std::array<std::int8_t, 4>>& p) {
    return accessor(view(p.data(), p.size() * 4, gl::kArrayBuffer), gl::kByte, p.size(), "VEC4");
}
int GltfBuilder::ushort2(const std::vector<std::array<std::uint16_t, 2>>& p) {
    return accessor(view(p.data(), p.size() * 4, gl::kArrayBuffer), gl::kUShort, p.size(), "VEC2");
}
int GltfBuilder::uint1(const std::vector<std::uint32_t>& p) {
    return accessor(view(p.data(), p.size() * 4, gl::kArrayBuffer), gl::kUInt, p.size(), "SCALAR");
}
int GltfBuilder::float1(const std::vector<float>& p) {
    return accessor(view(p.data(), p.size() * 4, gl::kArrayBuffer), gl::kFloat, p.size(), "SCALAR");
}
int GltfBuilder::indices(const std::vector<std::uint32_t>& idx, std::size_t vertex_count) {
    // the largest value of the type is the primitive restart value, which glTF does not allow
    if (vertex_count < 0xFFFF) {
        std::vector<std::uint16_t> s(idx.begin(), idx.end());
        return accessor(view(s.data(), s.size() * 2, gl::kElementArrayBuffer), gl::kUShort, s.size(), "SCALAR");
    }
    return accessor(view(idx.data(), idx.size() * 4, gl::kElementArrayBuffer), gl::kUInt, idx.size(), "SCALAR");
}

std::string GltfBuilder::text(const std::string& bin_uri) const {
    Json d = doc;
    // the buffer padded to 4 bytes (the CLI writes `bin` as it is: the padding is in it)
    Json buf = Json::object();
    buf.set("byteLength", Json::integer(std::int64_t((bin.size() + 3) & ~std::size_t(3))));
    buf.set("uri", Json::str(bin_uri));
    Json arr = Json::array();
    arr.push(std::move(buf));
    // keep "asset" first and the rest in glTF's usual order
    Json out = Json::object();
    for (const char* k : {"asset", "extensionsUsed", "extensionsRequired", "extensions", "scene", "scenes", "nodes", "meshes",
                          "materials", "textures", "samplers", "images", "accessors", "bufferViews"})
        if (const Json* v = d.get(k)) out.set(k, *v);
    out.set("buffers", std::move(arr));
    for (const auto& kv : d.o)
        if (!out.get(kv.first)) out.set(kv.first, kv.second);
    return out.dump(1) + "\n";
}

// ---- coordinates ----

std::array<float, 16> gltf_matrix(const float s[16]) {
    return {s[0], s[1], s[2], 0.0f, s[4], s[5], s[6], 0.0f, s[8], s[9], s[10], 0.0f, s[12], s[13], s[14], 1.0f};
}

std::array<float, 3> gltf_apply(const std::array<float, 16>& m, const float p[3], float w) {
    std::array<float, 3> r{};
    for (int c = 0; c < 3; ++c) r[c] = m[c] * p[0] + m[4 + c] * p[1] + m[8 + c] * p[2] + m[12 + c] * w;
    return r;
}

// ---- gltf_check ----

namespace {

int comp_size(int c) {
    switch (c) {
    case gl::kByte: case gl::kUByte: return 1;
    case gl::kShort: case gl::kUShort: return 2;
    case gl::kUInt: case gl::kFloat: return 4;
    default: return 0;
    }
}
int type_count(const std::string& t) {
    if (t == "SCALAR") return 1;
    if (t == "VEC2") return 2;
    if (t == "VEC3") return 3;
    if (t == "VEC4") return 4;
    if (t == "MAT2") return 4;
    if (t == "MAT3") return 9;
    if (t == "MAT4") return 16;
    return 0;
}

struct Acc {
    bool ok = false;
    int comp = 0, n = 0;
    std::size_t count = 0, at = 0, stride = 0;   // absolute byte position in bin of element 0
    double get(const std::vector<std::uint8_t>& bin, std::size_t i, int c) const {
        const std::uint8_t* p = bin.data() + at + i * stride + std::size_t(c) * std::size_t(comp_size(comp));
        switch (comp) {
        case gl::kByte: return double(std::int8_t(p[0]));
        case gl::kUByte: return double(p[0]);
        case gl::kShort: { std::int16_t v; std::memcpy(&v, p, 2); return v; }
        case gl::kUShort: { std::uint16_t v; std::memcpy(&v, p, 2); return v; }
        case gl::kUInt: { std::uint32_t v; std::memcpy(&v, p, 4); return v; }
        default: { float v; std::memcpy(&v, p, 4); return v; }
        }
    }
};

}  // namespace

bool gltf_check(const Json& doc, const std::vector<std::uint8_t>& bin, std::vector<std::string>& errors, GltfCheckStats* stats) {
    const std::size_t e0 = errors.size();
    auto err = [&](const std::string& m) { if (errors.size() - e0 < 50) errors.push_back(m); };
    auto arr = [&](const char* k) -> const std::vector<Json>& {
        static const std::vector<Json> none;
        const Json* j = doc.get(k);
        return j && j->type == Json::Arr ? j->a : none;
    };
    auto idx_ok = [&](const Json* j, const char* k) {
        return j && j->type == Json::Num && j->n >= 0 && j->n < double(arr(k).size()) && j->n == std::floor(j->n);
    };
    GltfCheckStats st;
    const Json* asset = doc.get("asset");
    if (!asset || !asset->get("version") || asset->get("version")->s != "2.0") err("asset.version is not \"2.0\"");
    const auto& buffers = arr("buffers");
    std::vector<std::size_t> buf_len;
    for (std::size_t i = 0; i < buffers.size(); ++i) {
        const std::size_t len = std::size_t(buffers[i].get("byteLength") ? buffers[i].get("byteLength")->as_int() : 0);
        if (i == 0 && len > bin.size()) err("buffer 0: byteLength " + std::to_string(len) + " > the .bin's " + std::to_string(bin.size()) + " bytes");
        if (i > 0) err("buffer " + std::to_string(i) + ": only one buffer is checked");
        buf_len.push_back(i == 0 ? std::min(len, bin.size()) : 0);
    }
    struct View { std::size_t at = 0, len = 0, stride = 0; bool ok = false; };
    std::vector<View> views;
    for (std::size_t i = 0; i < arr("bufferViews").size(); ++i) {
        const Json& v = arr("bufferViews")[i];
        View w;
        const Json* b = v.get("buffer");
        const std::int64_t off = v.get("byteOffset") ? v.get("byteOffset")->as_int() : 0, len = v.get("byteLength") ? v.get("byteLength")->as_int() : -1;
        const std::int64_t stride = v.get("byteStride") ? v.get("byteStride")->as_int() : 0;
        if (!idx_ok(b, "buffers")) err("bufferView " + std::to_string(i) + ": bad buffer");
        else if (off < 0 || len < 1 || std::size_t(off + len) > buf_len[std::size_t(b->n)]) err("bufferView " + std::to_string(i) + ": outside its buffer");
        else if (stride && (stride < 4 || stride > 252 || stride % 4)) err("bufferView " + std::to_string(i) + ": bad byteStride");
        else { w.at = std::size_t(off); w.len = std::size_t(len); w.stride = std::size_t(stride); w.ok = true; }
        views.push_back(w);
    }
    std::vector<Acc> accs;
    const auto& accessors = arr("accessors");
    for (std::size_t i = 0; i < accessors.size(); ++i) {
        const Json& a = accessors[i];
        Acc c;
        const std::string name = "accessor " + std::to_string(i);
        c.comp = int(a.get("componentType") ? a.get("componentType")->as_int() : 0);
        c.n = type_count(a.get("type") ? a.get("type")->s : "");
        const std::int64_t count = a.get("count") ? a.get("count")->as_int() : 0;
        const std::int64_t off = a.get("byteOffset") ? a.get("byteOffset")->as_int() : 0;
        const Json* bv = a.get("bufferView");
        if (!comp_size(c.comp) || !c.n || count < 1) { err(name + ": bad componentType, type or count"); accs.push_back(c); continue; }
        if (!bv) { err(name + ": no bufferView (sparse / zero accessors are not written by the exporter)"); accs.push_back(c); continue; }
        if (!idx_ok(bv, "bufferViews") || !views[std::size_t(bv->n)].ok) { err(name + ": bad bufferView"); accs.push_back(c); continue; }
        const View& w = views[std::size_t(bv->n)];
        const std::size_t esz = std::size_t(comp_size(c.comp) * c.n);
        c.stride = w.stride ? w.stride : esz;
        c.count = std::size_t(count);
        c.at = w.at + std::size_t(off);
        if (off < 0 || off % comp_size(c.comp) || c.at % std::size_t(comp_size(c.comp))) err(name + ": misaligned");
        else if (std::size_t(off) + c.stride * (c.count - 1) + esz > w.len) err(name + ": outside its bufferView");
        else c.ok = true;
        if (c.ok) {
            const Json* mn = a.get("min");
            const Json* mx = a.get("max");
            if ((mn && mn->size() != std::size_t(c.n)) || (mx && mx->size() != std::size_t(c.n))) err(name + ": min / max length");
            std::vector<double> lo(std::size_t(c.n), 0), hi(std::size_t(c.n), 0);
            bool bad = false;
            for (std::size_t k = 0; k < c.count; ++k)
                for (int q = 0; q < c.n; ++q) {
                    const double v = c.get(bin, k, q);
                    if (!std::isfinite(v)) bad = true;
                    if (!k || v < lo[std::size_t(q)]) lo[std::size_t(q)] = v;
                    if (!k || v > hi[std::size_t(q)]) hi[std::size_t(q)] = v;
                }
            if (bad) err(name + ": NaN or infinity");
            for (int q = 0; q < c.n && mn && mx && mn->size() == std::size_t(c.n) && mx->size() == std::size_t(c.n); ++q) {
                const double fl = c.comp == gl::kFloat ? double(float(mn->a[std::size_t(q)].n)) : mn->a[std::size_t(q)].n;
                const double fh = c.comp == gl::kFloat ? double(float(mx->a[std::size_t(q)].n)) : mx->a[std::size_t(q)].n;
                if (fl != lo[std::size_t(q)] || fh != hi[std::size_t(q)]) { err(name + ": min / max do not match the data"); break; }
            }
        }
        accs.push_back(c);
    }
    st.accessors = accs.size();
    const auto& images = arr("images");
    for (std::size_t i = 0; i < images.size(); ++i)
        if (!images[i].get("uri") && !idx_ok(images[i].get("bufferView"), "bufferViews")) err("image " + std::to_string(i) + ": no uri or bufferView");
    const auto& textures = arr("textures");
    for (std::size_t i = 0; i < textures.size(); ++i) {
        if (!idx_ok(textures[i].get("source"), "images")) err("texture " + std::to_string(i) + ": bad source");
        if (textures[i].get("sampler") && !idx_ok(textures[i].get("sampler"), "samplers")) err("texture " + std::to_string(i) + ": bad sampler");
    }
    const auto& materials = arr("materials");
    for (std::size_t i = 0; i < materials.size(); ++i) {
        const Json* pbr = materials[i].get("pbrMetallicRoughness");
        const Json* t = pbr ? pbr->get("baseColorTexture") : nullptr;
        if (t && !idx_ok(t->get("index"), "textures")) err("material " + std::to_string(i) + ": bad base colour texture");
        const Json* f = pbr ? pbr->get("baseColorFactor") : nullptr;
        if (f && f->size() != 4) err("material " + std::to_string(i) + ": baseColorFactor is not 4 numbers");
    }
    std::size_t nvariants = 0;
    if (const Json* ex = doc.get("extensions"))
        if (const Json* mv = ex->get("KHR_materials_variants"))
            if (const Json* v = mv->get("variants")) nvariants = v->size();
    const auto& meshes = arr("meshes");
    for (std::size_t i = 0; i < meshes.size(); ++i) {
        const std::string name = "mesh " + std::to_string(i);
        const Json* prims = meshes[i].get("primitives");
        if (!prims || !prims->size()) { err(name + ": no primitives"); continue; }
        for (std::size_t p = 0; p < prims->size(); ++p) {
            const Json& pr = prims->a[p];
            const std::string pn = name + " primitive " + std::to_string(p);
            ++st.primitives;
            const Json* at = pr.get("attributes");
            if (!at || at->type != Json::Obj || !at->get("POSITION")) { err(pn + ": no POSITION"); continue; }
            std::size_t nv = 0;
            bool ok = true;
            for (const auto& kv : at->o) {
                if (!idx_ok(&kv.second, "accessors")) { err(pn + ": attribute " + kv.first + " is not an accessor"); ok = false; continue; }
                const Acc& c = accs[std::size_t(kv.second.n)];
                if (!c.ok) { ok = false; continue; }
                if (c.at % 4 || c.stride % 4) err(pn + ": attribute " + kv.first + " not 4-byte aligned");
                if (kv.first == "POSITION") {
                    nv = c.count;
                    const Json& a = accessors[std::size_t(kv.second.n)];
                    if (c.comp != gl::kFloat || c.n != 3 || !a.get("min") || !a.get("max")) err(pn + ": POSITION must be VEC3 float with min / max");
                    else
                        for (int q = 0; q < 3; ++q) {
                            const double lo = a.get("min")->a[std::size_t(q)].n, hi = a.get("max")->a[std::size_t(q)].n;
                            if (st.vertices == 0 || lo < st.bmin[q]) st.bmin[q] = lo;
                            if (st.vertices == 0 || hi > st.bmax[q]) st.bmax[q] = hi;
                        }
                }
                if (kv.first == "NORMAL" && c.comp == gl::kFloat && c.n == 3)
                    for (std::size_t k = 0; k < c.count; ++k) {
                        const double x = c.get(bin, k, 0), y = c.get(bin, k, 1), z = c.get(bin, k, 2);
                        if (std::fabs(std::sqrt(x * x + y * y + z * z) - 1.0) > 0.005) { err(pn + ": NORMAL not unit length"); break; }
                    }
            }
            if (!ok) continue;
            for (const auto& kv : at->o)
                if (accs[std::size_t(kv.second.n)].count != nv) err(pn + ": attribute " + kv.first + " count differs from POSITION's");
            st.vertices += nv;
            const int mode = int(pr.get("mode") ? pr.get("mode")->as_int() : gl::kTriangles);
            if (const Json* ix = pr.get("indices")) {
                if (!idx_ok(ix, "accessors") || !accs[std::size_t(ix->n)].ok) { err(pn + ": bad indices accessor"); continue; }
                const Acc& c = accs[std::size_t(ix->n)];
                if (c.n != 1 || (c.comp != gl::kUByte && c.comp != gl::kUShort && c.comp != gl::kUInt)) err(pn + ": indices are not unsigned scalars");
                const double restart = c.comp == gl::kUByte ? 255.0 : c.comp == gl::kUShort ? 65535.0 : 4294967295.0;
                for (std::size_t k = 0; k < c.count; ++k) {
                    const double v = c.get(bin, k, 0);
                    if (v >= double(nv) || v == restart) { err(pn + ": index " + std::to_string(std::uint64_t(v)) + " out of range (" + std::to_string(nv) + " vertices)"); break; }
                }
                if (mode == gl::kTriangles) {
                    if (c.count % 3) err(pn + ": index count is not a multiple of 3");
                    st.triangles += c.count / 3;
                }
            } else if (mode == gl::kTriangles) {
                if (nv % 3) err(pn + ": vertex count is not a multiple of 3");
                st.triangles += nv / 3;
            }
            if (pr.get("material") && !idx_ok(pr.get("material"), "materials")) err(pn + ": bad material");
            if (const Json* ex = pr.get("extensions"))
                if (const Json* mv = ex->get("KHR_materials_variants"))
                    if (const Json* maps = mv->get("mappings"))
                        for (const Json& m : maps->a) {
                            if (!idx_ok(m.get("material"), "materials")) err(pn + ": variant mapping with a bad material");
                            if (const Json* vs = m.get("variants"))
                                for (const Json& v : vs->a)
                                    if (v.n < 0 || v.n >= double(nvariants)) err(pn + ": variant mapping with a bad variant");
                        }
        }
    }
    const auto& nodes = arr("nodes");
    std::vector<int> parent(nodes.size(), -1);
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        const Json& n = nodes[i];
        const std::string name = "node " + std::to_string(i);
        if (n.get("mesh") && !idx_ok(n.get("mesh"), "meshes")) err(name + ": bad mesh");
        if (const Json* m = n.get("matrix")) {
            if (m->size() != 16) err(name + ": matrix is not 16 numbers");
            else {
                for (const Json& v : m->a) if (v.type != Json::Num || !std::isfinite(v.n)) { err(name + ": matrix value not finite"); break; }
                if (m->a[3].n != 0 || m->a[7].n != 0 || m->a[11].n != 0 || m->a[15].n != 1) err(name + ": matrix last row is not 0 0 0 1");
            }
            if (n.get("translation") || n.get("rotation") || n.get("scale")) err(name + ": matrix and TRS together");
        }
        if (const Json* t = n.get("translation"))
            if (t->size() != 3) err(name + ": translation is not 3 numbers");
        if (const Json* c = n.get("children"))
            for (const Json& ch : c->a) {
                if (!idx_ok(&ch, "nodes")) { err(name + ": bad child"); continue; }
                const std::size_t k = std::size_t(ch.n);
                if (parent[k] >= 0) err("node " + std::to_string(k) + ": more than one parent");
                else if (k == i) err(name + ": its own child");
                else parent[k] = int(i);
            }
    }
    for (std::size_t i = 0; i < nodes.size(); ++i) {   // cycles: walk up at most nodes.size() steps
        int p = parent[i];
        std::size_t steps = 0;
        while (p >= 0 && steps <= nodes.size()) { p = parent[std::size_t(p)]; ++steps; }
        if (steps > nodes.size()) { err("node " + std::to_string(i) + ": in a cycle"); break; }
    }
    const auto& scenes = arr("scenes");
    for (std::size_t i = 0; i < scenes.size(); ++i)
        if (const Json* ns = scenes[i].get("nodes"))
            for (const Json& r : ns->a) {
                if (!idx_ok(&r, "nodes")) err("scene " + std::to_string(i) + ": bad node");
                else if (parent[std::size_t(r.n)] >= 0) err("scene " + std::to_string(i) + ": node " + std::to_string(std::int64_t(r.n)) + " is not a root");
            }
    if (doc.get("scene") && !idx_ok(doc.get("scene"), "scenes")) err("bad default scene");
    st.nodes = nodes.size();
    st.meshes = meshes.size();
    st.materials = materials.size();
    st.textures = textures.size();
    st.images = images.size();
    if (stats) *stats = st;
    return errors.size() == e0;
}

bool gltf_read_accessor(const Json& doc, const std::vector<std::uint8_t>& bin, int index, std::vector<double>& out, int& ncomp,
                        std::string* why) {
    auto bad = [&](const std::string& m) { if (why) *why = "accessor " + std::to_string(index) + ": " + m; return false; };
    const Json* accs = doc.get("accessors");
    const Json* views = doc.get("bufferViews");
    if (!accs || index < 0 || std::size_t(index) >= accs->size() || !views) return bad("missing");
    const Json& a = accs->a[std::size_t(index)];
    const int comp = int(a.get("componentType") ? a.get("componentType")->as_int() : 0);
    ncomp = type_count(a.get("type") ? a.get("type")->s : "");
    const std::int64_t count = a.get("count") ? a.get("count")->as_int() : 0;
    const Json* bv = a.get("bufferView");
    if (!comp_size(comp) || !ncomp || count < 0 || !bv || bv->n < 0 || std::size_t(bv->n) >= views->size()) return bad("unsupported");
    const Json& v = views->a[std::size_t(bv->n)];
    const std::size_t esz = std::size_t(comp_size(comp) * ncomp);
    Acc c;
    c.comp = comp;
    c.n = ncomp;
    c.count = std::size_t(count);
    c.stride = v.get("byteStride") ? std::size_t(v.get("byteStride")->as_int()) : esz;
    c.at = std::size_t(v.get("byteOffset") ? v.get("byteOffset")->as_int() : 0) + std::size_t(a.get("byteOffset") ? a.get("byteOffset")->as_int() : 0);
    const std::size_t vend = std::size_t(v.get("byteOffset") ? v.get("byteOffset")->as_int() : 0) + std::size_t(v.get("byteLength") ? v.get("byteLength")->as_int() : 0);
    if (c.count && (c.at + c.stride * (c.count - 1) + esz > vend || vend > bin.size())) return bad("outside the buffer");
    const bool norm = a.get("normalized") && a.get("normalized")->b;
    const double scale = !norm ? 1.0 : comp == gl::kUByte ? 255.0 : comp == gl::kByte ? 127.0 : comp == gl::kUShort ? 65535.0 : comp == gl::kShort ? 32767.0 : 1.0;
    out.resize(c.count * std::size_t(ncomp));
    for (std::size_t k = 0; k < c.count; ++k)
        for (int q = 0; q < ncomp; ++q) {
            double x = c.get(bin, k, q) / scale;
            if (norm && x < -1.0) x = -1.0;
            out[k * std::size_t(ncomp) + std::size_t(q)] = x;
        }
    return true;
}

std::array<float, 16> gltf_mul(const std::array<float, 16>& a, const std::array<float, 16>& b) {
    std::array<float, 16> r{};
    for (int c = 0; c < 4; ++c)
        for (int rr = 0; rr < 4; ++rr) {
            float s = 0;
            for (int k = 0; k < 4; ++k) s += a[std::size_t(k * 4 + rr)] * b[std::size_t(c * 4 + k)];
            r[std::size_t(c * 4 + rr)] = s;
        }
    return r;
}

std::array<float, 16> gltf_node_matrix(const Json& n) {
    std::array<float, 16> m{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    if (const Json* mj = n.get("matrix"); mj && mj->size() == 16) {
        for (int i = 0; i < 16; ++i) m[std::size_t(i)] = float(mj->a[std::size_t(i)].n);
        return m;
    }
    float t[3] = {0, 0, 0}, q[4] = {0, 0, 0, 1}, s[3] = {1, 1, 1};
    if (const Json* j = n.get("translation"); j && j->size() == 3) for (int i = 0; i < 3; ++i) t[i] = float(j->a[std::size_t(i)].n);
    if (const Json* j = n.get("rotation"); j && j->size() == 4) for (int i = 0; i < 4; ++i) q[i] = float(j->a[std::size_t(i)].n);
    if (const Json* j = n.get("scale"); j && j->size() == 3) for (int i = 0; i < 3; ++i) s[i] = float(j->a[std::size_t(i)].n);
    const float x = q[0], y = q[1], z = q[2], w = q[3];
    const float r[9] = {1 - 2 * (y * y + z * z), 2 * (x * y + z * w), 2 * (x * z - y * w),
                        2 * (x * y - z * w), 1 - 2 * (x * x + z * z), 2 * (y * z + x * w),
                        2 * (x * z + y * w), 2 * (y * z - x * w), 1 - 2 * (x * x + y * y)};   // columns
    for (int c = 0; c < 3; ++c)
        for (int rr = 0; rr < 3; ++rr) m[std::size_t(c * 4 + rr)] = r[c * 3 + rr] * s[c];
    for (int i = 0; i < 3; ++i) m[std::size_t(12 + i)] = t[i];
    return m;
}

}  // namespace b3modtool
