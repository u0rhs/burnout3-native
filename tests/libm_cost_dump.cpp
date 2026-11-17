// libm_cost_dump.cpp -- TEST TOOL (not part of `make test`): for every libm override entry and every sweep input,
// the port's block hits (B3LIBM_TRACE_BLOCKS) next to the retail code's retired instruction count (tests/r5900_mini.h),
// one row per distinct hit vector. tools/libm_cost/fit.py solves those rows for the per-block costs B3LIBM_B(id, n).
// Build and run: make build/libm_cost_dump && build/libm_cost_dump > build/libm_cost_rows.tsv
// Row: entry \t retired \t inputs \t first input (x,y,i) \t id:hits,... ; block 500 = the wrapper's own instructions.
// A hit vector seen with two different retired counts is a CONFLICT row (the port's blocks do not separate two retail
// paths: a block mark is missing).
#ifndef B3LIBM_TRACE_BLOCKS
#error "build with -DB3LIBM_TRACE_BLOCKS"
#endif
#include "libm_cost_entries.h"

#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

struct Row {
    std::uint64_t retired;
    std::size_t inputs;
    std::uint32_t x, y;
    std::int64_t i;
};

std::string key_of() {
    std::string k;
    char buf[32];
    for (int id = 0; id < b3libm::ps2::kMaxBlocks; ++id)
        if (b3libm::ps2::block_hits[id] != 0) {
            std::snprintf(buf, sizeof buf, "%s%d:%u", k.empty() ? "" : ",", id, b3libm::ps2::block_hits[id]);
            k += buf;
        }
    return k;
}

}  // namespace

int main() {
    const auto& all = libm_cost::entries();
    std::vector<std::string> out(all.size());
    std::vector<std::thread> threads;
    for (std::size_t e = 0; e < all.size(); ++e) {
        threads.emplace_back([&, e] {
            const libm_cost::Entry& en = all[e];
            mini::R5900 m;
            if (!m.load_elf(B3_RETAIL_ELF)) { out[e] = std::string("#error no ELF\n"); return; }
            std::map<std::string, Row> rows;
            std::string conflicts;
            std::size_t bad_values = 0;
            const std::size_t n = libm_cost::for_each_input(en, [&](std::uint32_t x, std::uint32_t y, std::int64_t i) {
                std::memset(b3libm::ps2::block_hits, 0, sizeof b3libm::ps2::block_hits);
                if (en.wrapper) b3libm::ps2::block_hits[500] = 1;
                const std::uint32_t got = libm_cost::run_port(en.name, x, y, i);
                const std::uint32_t want = libm_cost::run_retail(m, en, x, y, i);
                if (got != want) ++bad_values;
                const std::string k = key_of();
                auto it = rows.find(k);
                if (it == rows.end()) rows.emplace(k, Row{m.retired, 1, x, y, i});
                else {
                    ++it->second.inputs;
                    if (it->second.retired != m.retired && conflicts.size() < 4000) {
                        char buf[256];
                        std::snprintf(buf, sizeof buf, "CONFLICT\t%s\t%llu vs %llu\t%08X,%08X,%lld vs %08X,%08X,%lld\t%s\n",
                                      en.name, static_cast<unsigned long long>(it->second.retired),
                                      static_cast<unsigned long long>(m.retired), it->second.x, it->second.y,
                                      static_cast<long long>(it->second.i), x, y, static_cast<long long>(i), k.c_str());
                        conflicts += buf;
                    }
                }
            });
            std::string s;
            char buf[160];
            std::snprintf(buf, sizeof buf, "#entry\t%s\tinputs %zu\trows %zu\tvalue mismatches %zu\n", en.name, n,
                          rows.size(), bad_values);
            s += buf;
            for (const auto& [k, r] : rows) {
                std::snprintf(buf, sizeof buf, "%s\t%llu\t%zu\t%08X,%08X,%lld\t", en.name,
                              static_cast<unsigned long long>(r.retired), r.inputs, r.x, r.y, static_cast<long long>(r.i));
                s += buf;
                s += k;
                s += '\n';
            }
            s += conflicts;
            out[e] = s;
        });
    }
    for (auto& t : threads) t.join();
    for (const auto& s : out) std::fputs(s.c_str(), stdout);
    return 0;
}
