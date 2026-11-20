// file_url.h -- robust2: a "file://" URL of a local path for SDL_OpenURL (the launcher's folder buttons and the macOS
// update's disk image, the game's SAVES > SAVE FOLDER > Open). The path is UTF-8; '\' becomes '/'; every byte but
// ASCII letters, digits, "/:-_.~" and UTF-8 (>= 0x80, left as is: the system openers take it) is percent-encoded, so
// "Application Support", '#', '%' and '?' survive. A Windows drive path gets the third slash ("file:///C:/...").
// Tested in tests/test_launcher_update.cpp.
#ifndef B3PORT_FILE_URL_H
#define B3PORT_FILE_URL_H

#include <cctype>
#include <string>
#include <string_view>

namespace b3port {

inline std::string file_url(std::string_view path_utf8) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    out.reserve(path_utf8.size() + 16);
    for (char ch : path_utf8) {
        const unsigned char c = static_cast<unsigned char>(ch == '\\' ? '/' : ch);
        if ((c < 0x80 && std::isalnum(c)) || c == '/' || c == ':' || c == '-' || c == '_' || c == '.' || c == '~' || c >= 0x80) {
            out += char(c);
        } else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 15];
        }
    }
    if (out.size() > 1 && out[1] == ':') return "file:///" + out;   // C:/...
    if (!out.empty() && out[0] != '/') return "file:///" + out;
    return "file://" + out;
}

}  // namespace b3port

#endif
