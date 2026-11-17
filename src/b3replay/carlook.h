// carlook.h -- the enhanced car reflections' look as live values (reflect2; docs/car-look.md). The numbers are
// b3gfx::CarLook (include/b3gfx/b3gfx.h); this file is their table (names, ranges, steps), the process-wide current look,
// the ini (car_look.ini in the user data folder), the C++ initializer block and the plain-text values file. Presentation
// only: nothing here touches the game. The panel that edits them live is carlookpanel.h.
#ifndef B3REPLAY_CARLOOK_H
#define B3REPLAY_CARLOOK_H

#include <string>
#include <vector>

#include "b3gfx/b3gfx.h"

namespace b3replay::carlook {

struct Param {
    const char* key;      // the ini's name
    const char* label;    // the panel's
    const char* help;
    float lo, hi, step;   // the range and one key press
    float b3gfx::CarLook::* field;
};
const std::vector<Param>& params();

b3gfx::CarLook defaults();                    // the shipped look (CarLook{})
b3gfx::CarLook current();                     // the look in use now (any thread)
void set_current(const b3gfx::CarLook& l);    // clamps every value to its range
bool at_default(const b3gfx::CarLook& l, const Param& p);

// ini: `key = value` lines, '#' comments; unknown keys and bad numbers are warned about, values clamped to the range
std::string to_ini(const b3gfx::CarLook& l);
b3gfx::CarLook from_ini(const std::string& text, std::vector<std::string>* warnings = nullptr, const b3gfx::CarLook& base = {});
bool load_file(const std::string& path, std::string* why = nullptr);   // into the current look (missing file: false, why set)
bool load_explicit(const std::string& path, std::string* why = nullptr);   // --car-look <ini>: wins over the user's car_look.ini
bool explicit_loaded();
// the user's car_look.ini beside the settings at start (a missing file: the shipped look); not when --car-look was given
void load_user(const std::string& settings_dir);
bool save_file(const std::string& path, std::string* why = nullptr);   // the current look, as ini

// "b3gfx::CarLook{ ... }" with every value (or only those that differ from the shipped look), as a tuning session sends back
std::string code_block(const b3gfx::CarLook& l, bool all = true);
// values file: the ini's lines plus the code block (car_look_values.txt)
std::string values_text(const b3gfx::CarLook& l);

// where the user's files live: the settings folder (set once at start), car_look.ini and car_look_values.txt in it
void set_user_dir(const std::string& dir);
std::string ini_path();
std::string values_path();

}  // namespace b3replay::carlook

#endif
