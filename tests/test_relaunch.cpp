// test_relaunch.cpp -- nextstart: the game started again after PC SETTINGS' APPLY AND RESTART (tools/native/relaunch.h):
// the arguments kept and dropped, START AT for the new start, a Windows command line's quoting.
#include "test.h"

#include "relaunch.h"

namespace {

using b3native::relaunch_args;
using b3native::relaunch_boot_to;
using b3native::windows_quote;

TEST(the_arguments_without_log_and_replay) {
    const std::vector<std::string> in = {"--game", "/g/data", "--log", "/l/run.log", "--safe-mode", "--replay", "/r/x.b3replay", "--offscreen",
                                         "--keys", "9000:41:1", "--keys2", "100:29:1", "--record-pad", "/o/pads.txt", "--record-replay",
                                         "/o/s.b3replay", "--replay-pad", "/i/p.txt", "--mouse", "m", "--text", "t", "--tune", "u",
                                         "--strict", "--mods-dir", "/m"};
    const std::vector<std::string> want = {"--game", "/g/data", "--safe-mode", "--offscreen", "--strict", "--mods-dir", "/m"};
    CHECK(relaunch_args(in) == want);
    CHECK(relaunch_args({}).empty());
    // a last --log without its file stays (as it was given: the game reports it)
    CHECK(relaunch_args({"--game", "g", "--log"}) == (std::vector<std::string>{"--game", "g", "--log"}));
}

TEST(start_at_at_least_the_title) {
    CHECK(relaunch_boot_to(0) == 1);    // the intro films: skipped (the title screen)
    CHECK(relaunch_boot_to(1) == 1);
    CHECK(relaunch_boot_to(3) == 3);    // START AT's own choice beyond it stays
    CHECK(relaunch_boot_to(9) == 1);
    CHECK(relaunch_boot_to(-2) == 1);
}

TEST(windows_quoting) {
    CHECK(windows_quote("plain") == "plain");
    CHECK(windows_quote("C:\\Games\\Burnout 3") == "\"C:\\Games\\Burnout 3\"");
    CHECK(windows_quote("") == "\"\"");
    CHECK(windows_quote("a\"b") == "\"a\\\"b\"");
    CHECK(windows_quote("C:\\dir with space\\") == "\"C:\\dir with space\\\\\"");   // the trailing backslash doubled
}

}  // namespace

TEST_MAIN("nextstart: the game started again (relaunch.h)")
