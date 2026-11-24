// taxihead (src/b3replay/carmatch.h): a captured vehicle data header matched to its car file by the words after 0x4C,
// relocated by one load offset -- the three taxis share their first 0x4C bytes (the words are the files' own, US/C1_V1
// and AS/C1_V2's captures).
#include "test.h"

#include "carmatch.h"

#include <cstdint>
#include <cstring>
#include <vector>

namespace {

std::vector<std::uint8_t> header(const std::vector<std::uint32_t>& words, std::uint32_t delta) {
    std::vector<std::uint8_t> b(0x70, 0);
    for (std::size_t i = 0; i < words.size(); ++i) {
        const std::uint32_t w = words[i] ? words[i] + delta : 0;
        std::memcpy(b.data() + 0x4C + 4 * i, &w, 4);
    }
    return b;
}

const std::vector<std::uint32_t> kCar15 = {0x16f0, 0x146a0, 0x1fb30, 0, 0, 0x2b80, 0x34f10, 0x35460, 0};
const std::vector<std::uint32_t> kCar16 = {0x16f0, 0x14d20, 0x20230, 0, 0, 0x3100, 0x34c10, 0x35160, 0};
const std::vector<std::uint32_t> kCar17 = {0x16f0, 0x14d20, 0x20330, 0, 0, 0x3100, 0x34e10, 0x35360, 0};

TEST(asia_taxi) {   // AS/C1_V2's capture: loaded at 0xd0d080
    const auto h = header(kCar17, 0xd0d080);
    const auto f15 = header(kCar15, 0), f16 = header(kCar16, 0), f17 = header(kCar17, 0);
    CHECK(!b3replay::head_tail_fits(f15.data(), f15.size(), h.data(), h.size()));
    CHECK(!b3replay::head_tail_fits(f16.data(), f16.size(), h.data(), h.size()));
    CHECK(b3replay::head_tail_fits(f17.data(), f17.size(), h.data(), h.size()));
}

TEST(us_taxi) {   // US/C1_V1's: at 0xd4d080
    const auto h = header(kCar15, 0xd4d080);
    const auto f15 = header(kCar15, 0), f17 = header(kCar17, 0);
    CHECK(b3replay::head_tail_fits(f15.data(), f15.size(), h.data(), h.size()));
    CHECK(!b3replay::head_tail_fits(f17.data(), f17.size(), h.data(), h.size()));
}

// Why the first LOD offset matters: two files whose words differ only by one uniform shift of every offset after the first
// (0x4C, the same in both) are told apart -- the capture's words all carry one load offset only against its own file
TEST(uniform_shift_after_the_first) {
    std::vector<std::uint32_t> b = kCar16;
    for (std::size_t i = 1; i < b.size(); ++i) if (b[i]) b[i] += 0x100;
    const auto h = header(kCar16, 0xd0d080);
    const auto f_own = header(kCar16, 0), f_shifted = header(b, 0);
    CHECK(b3replay::head_tail_fits(f_own.data(), f_own.size(), h.data(), h.size()));
    CHECK(!b3replay::head_tail_fits(f_shifted.data(), f_shifted.size(), h.data(), h.size()));
    // ...whereas a shift of every offset, the first too, is the load offset's own shape: both fit (the limit of the rule;
    // match_car then takes the first and says so)
    std::vector<std::uint32_t> all = kCar16;
    for (std::uint32_t& w : all) if (w) w += 0x100;
    const auto f_all = header(all, 0);
    CHECK(b3replay::head_tail_fits(f_all.data(), f_all.size(), h.data(), h.size()));
}

TEST(unrelocated_and_short) {
    const auto f = header(kCar16, 0);
    CHECK(b3replay::head_tail_fits(f.data(), f.size(), f.data(), f.size()));   // as in the file (no relocation)
    CHECK(!b3replay::head_tail_fits(f.data(), 0x50, f.data(), f.size()));     // a file shorter than the header
}

}  // namespace

TEST_MAIN("a vehicle's car file by its header's relocated words (taxihead)")
