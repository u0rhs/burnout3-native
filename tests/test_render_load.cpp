// dll1114: the renderer load's message per Windows error, and the folder test its diagnosis uses (tools/native/render_load.h)
#include "test.h"

#include "render_load.h"

#include <string>

using b3native::render_load::in_folder;
using b3native::render_load::render_load_message;

TEST(folder_prefix_case_and_slashes) {
    CHECK(in_folder("C:\\Windows\\System32\\user32.dll", "C:\\Windows"));
    CHECK(in_folder("c:/windows/system32/USER32.dll", "C:\\WINDOWS\\"));
    CHECK(!in_folder("C:\\WindowsApps\\x.dll", "C:\\Windows"));   // a longer name, not a subfolder
    CHECK(!in_folder("C:\\Windows", "C:\\Windows"));
    CHECK(!in_folder("D:\\game\\version.dll", "C:\\Windows"));
    CHECK(!in_folder("C:\\Windows\\x.dll", ""));
}

TEST(message_per_error) {
    const std::string lib = "C:\\Games\\b3\\b3render.dll";
    const std::string init = render_load_message(1114, lib, "");
    CHECK(init.find("(error 1114)") != std::string::npos);
    CHECK(init.find("was found") != std::string::npos);
    CHECK(init.find(".dll file in the game folder") != std::string::npos);
    CHECK(init.find("run log") == std::string::npos);   // no log: no log line
    const std::string missing = render_load_message(126, lib, "");
    CHECK(missing.find("was not found (error 126)") != std::string::npos);
    CHECK(missing.find("together in one folder") != std::string::npos);
    CHECK(render_load_message(4551, lib, "").find("blocked") != std::string::npos);
    CHECK(render_load_message(193, lib, "").find("64-bit") != std::string::npos);
    const std::string other = render_load_message(31, lib, "");
    CHECK(other.find("did not load (error 31)") != std::string::npos);
    CHECK(other.find(lib) != std::string::npos);
}

TEST(message_names_the_run_log) {
    const std::string m = render_load_message(1114, "b3render.dll", "C:\\Users\\p\\AppData\\logs\\run-1.log");
    CHECK(m.find("The details are in the run log: C:\\Users\\p\\AppData\\logs\\run-1.log") != std::string::npos);
}

TEST_MAIN("renderer load message (dll1114)")
