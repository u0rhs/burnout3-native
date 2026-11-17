// platform_vu0.cpp -- see platform_vu0.h: the native platform's one VU0 (src/native/vu0_unit.h).
#include "platform_vu0.h"

#include "vu0_unit.h"

#include <cstdlib>

namespace b3native::platform {
namespace {
Vu0Unit& vu0() {
    static Vu0Unit* v = [] {
        auto* u = new Vu0Unit;
        if (const char* e = std::getenv("B3_VU0_STATS"); e && *e == '1') std::atexit([] { vu0_report(stderr); });
        return u;
    }();
    return *v;
}
}  // namespace

std::uint8_t* vu0_micro_bytes() { return vu0().micro_bytes(); }
std::uint8_t* vu0_data_bytes() { return vu0().data_bytes(); }
void vu0_micro_changed() { vu0().micro_changed(); }
std::uint64_t vu0_micro_calls() { return vu0().calls(); }
void vu0_micro_call(b3asm::State& s, std::uint32_t entry, std::uint32_t caller_pc) { vu0().call(s, entry, caller_pc); }
void vif0_upload(std::uint32_t chcr, std::uint32_t tadr, const std::function<std::uint32_t(std::uint32_t)>& read32) {
    vu0().vif0_upload(chcr, tadr, read32);
}
void vu0_report(std::FILE* out) { vu0().report(out); }
void vu0_set_strict(bool on) { vu0().set_strict(on); }
void vu0_summary(std::FILE* out, const char* prefix) { vu0().summary(out, prefix); }

}  // namespace b3native::platform
