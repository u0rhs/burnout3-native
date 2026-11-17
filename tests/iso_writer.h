// iso_writer.h -- a minimal ISO 9660 image writer for tests (iso1): folders and files from memory, 2048-byte sectors,
// names stored as given ("NAME.EXT;1" for files). No disc data: the tests make their own volumes.
#ifndef B3_TESTS_ISO_WRITER_H
#define B3_TESTS_ISO_WRITER_H

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace isotest {

struct Node {
    bool dir = true;
    std::vector<std::uint8_t> bytes;
    std::map<std::string, std::unique_ptr<Node>> kids;   // by name
    std::uint32_t lba = 0, size = 0;
};

class Writer {
public:
    // a file at "DIR/SUB/NAME.EXT" (folders made on the way)
    void file(const std::string& path, std::vector<std::uint8_t> bytes) {
        Node* n = at(path, false);
        n->dir = false;
        n->bytes = std::move(bytes);
    }
    void dir(const std::string& path) { at(path, true); }

    bool write(const std::string& out) {
        next_ = 18;
        layout(root_, root_);
        std::vector<std::uint8_t> img(std::size_t(next_) * 2048, 0);
        emit(root_, root_, img);
        std::uint8_t* pvd = &img[16 * 2048];
        pvd[0] = 1;
        std::memcpy(pvd + 1, "CD001", 5);
        pvd[6] = 1;
        le32(pvd + 80, next_);
        pvd[128] = 0x00; pvd[129] = 0x08;
        const auto r = record(std::string(1, '\0'), root_.lba, root_.size, 2);
        std::memcpy(pvd + 156, r.data(), 34);
        std::uint8_t* term = &img[17 * 2048];
        term[0] = 255;
        std::memcpy(term + 1, "CD001", 5);
        term[6] = 1;
        std::ofstream f(out, std::ios::binary | std::ios::trunc);
        f.write(reinterpret_cast<const char*>(img.data()), std::streamsize(img.size()));
        return bool(f);
    }

private:
    Node root_;
    std::uint32_t next_ = 18;

    Node* at(const std::string& path, bool dir) {
        Node* cur = &root_;
        std::size_t p = 0;
        while (p < path.size()) {
            std::size_t e = path.find('/', p);
            if (e == std::string::npos) e = path.size();
            const std::string name = path.substr(p, e - p);
            auto& k = cur->kids[name];
            if (!k) { k = std::make_unique<Node>(); k->dir = e < path.size() || dir; }
            cur = k.get();
            p = e + 1;
        }
        return cur;
    }
    static void le32(std::uint8_t* p, std::uint32_t v) { for (int k = 0; k < 4; ++k) p[k] = std::uint8_t(v >> (8 * k)); }
    static void be32(std::uint8_t* p, std::uint32_t v) { for (int k = 0; k < 4; ++k) p[3 - k] = std::uint8_t(v >> (8 * k)); }
    static std::vector<std::uint8_t> record(const std::string& name, std::uint32_t lba, std::uint32_t size, std::uint8_t flags) {
        const std::size_t len = 33 + name.size() + (name.size() % 2 == 0 ? 1 : 0);
        std::vector<std::uint8_t> r(len, 0);
        r[0] = std::uint8_t(len);
        le32(&r[2], lba); be32(&r[6], lba);
        le32(&r[10], size); be32(&r[14], size);
        r[25] = flags;
        r[28] = 1; r[31] = 1;
        r[32] = std::uint8_t(name.size());
        std::memcpy(&r[33], name.data(), name.size());
        return r;
    }
    std::vector<std::vector<std::uint8_t>> records(const Node& d, const Node& parent) const {
        std::vector<std::vector<std::uint8_t>> rr = {record(std::string(1, '\0'), d.lba, d.size, 2), record(std::string(1, '\1'), parent.lba, parent.size, 2)};
        for (const auto& [name, k] : d.kids) rr.push_back(record(k->dir ? name : name + ";1", k->lba, k->size, k->dir ? 2 : 0));
        return rr;
    }
    static std::vector<std::uint8_t> pack(const std::vector<std::vector<std::uint8_t>>& rr) {
        std::vector<std::uint8_t> d;
        for (const auto& r : rr) {
            if (d.size() % 2048 + r.size() > 2048) d.resize((d.size() / 2048 + 1) * 2048, 0);
            d.insert(d.end(), r.begin(), r.end());
        }
        d.resize(std::max<std::size_t>(2048, (d.size() + 2047) / 2048 * 2048), 0);
        return d;
    }
    // LBAs: each folder's extent (its size from its records), then its files, then its folders
    void layout(Node& d, Node& parent) {
        d.size = std::uint32_t(pack(records(d, parent)).size());   // the sizes in records do not change the length
        d.lba = next_;
        next_ += d.size / 2048;
        for (auto& [name, k] : d.kids)
            if (!k->dir) {
                k->lba = next_;
                k->size = std::uint32_t(k->bytes.size());
                next_ += std::uint32_t((k->bytes.size() + 2047) / 2048);
            }
        for (auto& [name, k] : d.kids)
            if (k->dir) layout(*k, d);
    }
    void emit(const Node& d, const Node& parent, std::vector<std::uint8_t>& img) const {
        const auto bytes = pack(records(d, parent));
        std::memcpy(&img[std::size_t(d.lba) * 2048], bytes.data(), bytes.size());
        for (const auto& [name, k] : d.kids) {
            if (k->dir) emit(*k, d, img);
            else if (!k->bytes.empty()) std::memcpy(&img[std::size_t(k->lba) * 2048], k->bytes.data(), k->bytes.size());
        }
    }
};

}  // namespace isotest

#endif
