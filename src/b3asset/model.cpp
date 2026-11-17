#include "b3asset/model.h"

namespace b3asset {

void Model::grow_bounds(const float p[3]) {
    for (int i = 0; i < 3; ++i) {
        if (p[i] < bmin[i]) bmin[i] = p[i];
        if (p[i] > bmax[i]) bmax[i] = p[i];
    }
}

void strip_to_triangles(std::uint32_t base, const std::vector<bool>& adc, std::vector<std::uint32_t>& out) {
    // The GS draws (i-2, i-1, i) for every vertex i >= 2 without ADC, whatever the flags before it; the track
    // programs' back-face test orients triangle i by the parity of i itself (docs/native-renderer.md §11.3), so the
    // emitted order is (i-2, i-1, i) for even i and (i-1, i-2, i) for odd i: one consistent front face per batch.
    for (std::size_t i = 2; i < adc.size(); ++i) {
        if (adc[i]) continue;
        const std::uint32_t a = base + std::uint32_t(i - 2), b = base + std::uint32_t(i - 1), c = base + std::uint32_t(i);
        if (i & 1) out.insert(out.end(), {b, a, c});
        else out.insert(out.end(), {a, b, c});
    }
}

}  // namespace b3asset
