// disc_alias.h -- the disc path aliases platform_disc.cpp asks before the disc (trafficseries: a traffic vehicle's race car
// files; customseries: the extra cars' files): providers in the order added, the first non-empty answer taken, each
// provider once. Pure (tests/test_extra_series.cpp).
#ifndef B3NATIVE_DISC_ALIAS_H
#define B3NATIVE_DISC_ALIAS_H

#include <algorithm>
#include <string>
#include <vector>

namespace b3native::platform::disc {

using AliasFn = std::string (*)(const std::string& disc_path);
class AliasList {
public:
    void add(AliasFn f) {
        if (f && std::find(fns_.begin(), fns_.end(), f) == fns_.end()) fns_.push_back(f);
    }
    // the first provider's non-empty answer for `path` ("" none)
    std::string ask(const std::string& path) const {
        for (AliasFn f : fns_)
            if (std::string a = f(path); !a.empty()) return a;
        return {};
    }
    std::size_t size() const { return fns_.size(); }

private:
    std::vector<AliasFn> fns_;
};

}  // namespace b3native::platform::disc

#endif
