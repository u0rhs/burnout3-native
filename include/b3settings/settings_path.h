// settings_path.h -- extrastart (pre4): where the PC settings are read. One rule for the renderer (src/b3replay/main.cpp,
// which loads them on its own thread) and its b3r_ getters asked before that (src/b3replay/inproc.cpp: native_play's
// setup and first VBlanks run before the renderer has loaded them): --settings when given, else a window run of the
// native game (--native-pad, no --offscreen-live) reads the user's pc_settings.ini unless $B3_NO_USER_SETTINGS is set;
// offscreen runs (tests) never read a user's file.
#ifndef B3SETTINGS_SETTINGS_PATH_H
#define B3SETTINGS_SETTINGS_PATH_H

#include <string>

namespace b3settings {

inline std::string settings_path_for(const std::string& given, bool native_pad, bool offscreen, bool no_user_settings,
                                     const std::string& user_dir_slash) {
    if (!given.empty()) return given;
    if (native_pad && !offscreen && !no_user_settings && !user_dir_slash.empty()) return user_dir_slash + "pc_settings.ini";
    return {};
}

}  // namespace b3settings

#endif
