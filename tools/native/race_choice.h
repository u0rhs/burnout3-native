// race_choice.h -- raceopts (2026-11-21): the per-race options of SINGLE EVENT's SELECT OPTIONS page as values, and the
// rules the page and the race follow (pure: tests/test_race_options.cpp). The page and the hooks are race_options.h.
//
// The rows per mode (RACE 0, TIME ATTACK 1, ROAD RAGE 2; CRASH 3 has no option rows and gets none):
//   RACE        REGION TRACK RIVALS (the game's) + TRAFFIC, LAPS
//   TIME ATTACK REGION TRACK (the game's)        + TRAFFIC          (its laps never end: the game sets 0, no LAPS row)
//   ROAD RAGE   REGION TRACK (the game's)        + RIVALS, TRAFFIC  (timed by its takedowns: no LAPS row)
//   + TIME OF DAY in each, held back with night mode.
//   + TRANSMISSION in RACE, TIME ATTACK, ROAD RAGE and ELIMINATOR (gearbox): Automatic (the game's) / Manual (the game's
//     own manual gearbox, never enabled by retail: tools/native/gearbox.h).
//   ELIMINATOR (elim1, SELECT MODE's fifth row; the game's mode is RACE): REGION TRACK RIVALS + TRAFFIC, TIMED ELIMINATION
//   (no LAPS: an Eliminator's laps are its cars less one). TIMED ELIMINATION: Off (the game's: the last car is out at
//   each lap) / On (the last car is out every kTimedEliminationSeconds of race time; single_eliminator.h).
// options3 (2026-11-21), appended after those in each mode's list: START, RIVAL STYLE (RACE), BURNOUT POINTS and TIME
// LIMIT (ROAD RAGE), TRAFFIC PATTERN, RIVALS' CARS (RACE, ROAD RAGE); the page holds 9 rows, so RACE and ROAD RAGE show
// theirs on two pages with a MORE row (optpages, race_pages_for). TRAFFIC has six values.
// TRAFFIC CHECK (trafficcheck, traffic_check_rules.h) in RACE, TIME ATTACK, ROAD RAGE and ELIMINATOR (not CRASH): Off (the
// game's) / Revenge / Extreme (RACE's and ROAD RAGE's second page).
// TRAFFIC: Off / Light / Normal (the game's) / Busy / Heavy / Extreme, a scale of the track's own spawn rates (0, 0.5, 1,
// 1.5, 2, 3; stored 0, 1, 2, 4, 3, 5: Heavy keeps its value).
// LAPS: 1..8 for a lap race, the track's own count tagged "(Default)" (the game counts laps 0..7 [C IncrementLapCount
// 0x15A1F0 wraps the index after 8; the lap time arrays are 8 long]: 9 or more would never finish). A point-to-point
// race (1 lap) or an unknown track: the row disabled, the track's value.
// ROAD RAGE's RIVALS: 1..5, 3 the game's (it gives Road Rage min(rivals, 3) [C CB3MenuChoices::RequestOnePlayerGameMode
// 0x30C2C0]; 4 and 5 are set in the stage record after that call, within the game's 6-car frame: the Road Rage logic's
// per-car tables are [6] and it walks the AI count [C OnWorldPrepared 0x38C3F0, the respawns 0x38CBB0]).
// Values are the page's own case ("Random"); the default one is tagged " (Default)".
#ifndef B3NATIVE_RACE_CHOICE_H
#define B3NATIVE_RACE_CHOICE_H

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace b3native {

constexpr int kRaceMaxLaps = 8;
constexpr int kRoadRageRivals = 3;      // the game's
constexpr int kRoadRageMaxRivals = 5;   // the 6-car frame
constexpr int kTimedEliminationSeconds = 24;   // elim1: TIMED ELIMINATION On
constexpr int kRaceTrafficValues = 6;   // options3: Off, Light, Normal, Heavy, Busy, Extreme (stored values)
constexpr int kRaceTimeLimits = 5;      // options3: Off, 1:00, 2:00, 3:00, 5:00
inline int race_time_limit_seconds(int i) { static const int kS[kRaceTimeLimits] = {0, 60, 120, 180, 300}; return i >= 0 && i < kRaceTimeLimits ? kS[i] : 0; }
constexpr int kRaceRivalCars = 9;       // options3: RIVALS' CARS: your class (the game's), Compact, Muscle, Coupe, Sports, Super, Heavyweights, Same as yours;
                                         // mixedrivals: Mixed (each rival a class of its own, race_options.h pick_rival_cars)
// RIVALS' CARS -> the car class the game's picker takes (EB3MenuCarClasses; -1: the player's, the game's own; -2: the
// player's car for every rival)
// (-1 the game's pick, -2 the player's car, -3 Mixed: a class per rival)
constexpr int kRivalClassMixed = -3, kRaceRivalCarsMixed = 8;
inline int race_rival_class(int cars) { static const int kC[kRaceRivalCars] = {-1, 0, 1, 2, 3, 4, 6, -2, kRivalClassMixed}; return cars >= 0 && cars < kRaceRivalCars ? kC[cars] : -1; }

// The records word's bits each field takes (the race's options as one word: src/b3replay/records.h options_folder; every
// owner of the word: tests/test_bit_owners.cpp, which checks these masks against its table)
constexpr std::uint64_t kRaceWordTraffic = 0x3ull | 1ull << 16, kRaceWordLaps = 0xFull << 2, kRaceWordRivals = 0x7ull << 6,
                        kRaceWordTimed = 1ull << 9, kRaceWordManual = 1ull << 10, kRaceWordFlag = 1ull << 15,
                        kRaceWordStart = 1ull << 17, kRaceWordStyle = 1ull << 18, kRaceWordPoints = 1ull << 19,
                        kRaceWordLimit = 0x7ull << 20, kRaceWordSeed = 1ull << 23, kRaceWordCars = 0x7ull << 35,
                        kRaceWordCarsMixed = 1ull << 38;   // mixedrivals
constexpr std::uint64_t kRaceWordMask = kRaceWordTraffic | kRaceWordLaps | kRaceWordRivals | kRaceWordTimed | kRaceWordManual |
                                        kRaceWordFlag | kRaceWordStart | kRaceWordStyle | kRaceWordPoints | kRaceWordLimit |
                                        kRaceWordSeed | kRaceWordCars | kRaceWordCarsMixed;

struct RaceChoice {
    std::uint8_t traffic = 2;   // 0 Off, 1 Light, 2 Normal (the game's), 3 Heavy (x2), 4 Busy (x1.5), 5 Extreme (x3)
    std::uint8_t laps = 0;      // 0: the track's own; else 1..kRaceMaxLaps (RACE)
    std::uint8_t rivals = 0;    // ROAD RAGE: 0 the game's (3); else 1, 2, 4, 5
    std::uint8_t time = 0;      // race_time.h kRaceTimeDefault / Day / Night (held back with night mode)
    std::uint8_t timed = 0;     // elim1: ELIMINATOR's TIMED ELIMINATION, 0 Off (the game's), 1 On
    std::uint8_t manual = 0;    // gearbox: TRANSMISSION, 0 Automatic (the game's), 1 Manual
    std::uint8_t start = 0;     // options3: 0 the game's (rolling), 1 standing
    std::uint8_t style = 0;     // options3: RACE's rivals, 0 the game's (racing), 1 the Road Rage AI
    std::uint8_t points = 0;    // options3: ROAD RAGE's Burnout points, 0 the game's (takedowns), 1 all (as a race)
    std::uint8_t limit = 0;     // options3: ROAD RAGE's time limit, 0 none (the game's), else race_time_limit_seconds
    std::uint8_t seed = 0;      // options3: TRAFFIC PATTERN, 0 the game's fixed seed, 1 varied (a seed per race start)
    std::uint8_t cars = 0;      // options3: RIVALS' CARS, 0 the game's (your class), else race_rival_class
    std::uint8_t check = 0;     // trafficcheck: TRAFFIC CHECK, 0 Off (the game's), 1 Revenge, 2 Extreme (its records bits are
                                // traffic_check_rules.h's, from the race's latched mode)
    // all of these as the game's: the race is the game's own (TIME OF DAY -- night, rain -- is drawn only: its races
    // count as the game's, the owner's, 2026-11-23)
    bool gameplay_default() const {
        return traffic == 2 && laps == 0 && rivals == 0 && timed == 0 && manual == 0 && start == 0 && style == 0 &&
               points == 0 && limit == 0 && seed == 0 && cars == 0 && check == 0;
    }
    bool operator==(const RaceChoice& o) const {
        return traffic == o.traffic && laps == o.laps && rivals == o.rivals && time == o.time && timed == o.timed &&
               manual == o.manual && start == o.start && style == o.style && points == o.points && limit == o.limit &&
               seed == o.seed && cars == o.cars && check == o.check;
    }
    bool operator!=(const RaceChoice& o) const { return !(*this == o); }
    // "t2 l0 r0 d0" (the replay's and the log's), then a tag for each field not the game's, in this order: " e1" TIMED
    // ELIMINATION On (elim1), " m1" TRANSMISSION Manual (gearbox), options3's " s1" START standing, " a1" RIVAL STYLE Road
    // Rage, " p1" BURNOUT POINTS all, " k<n>" TIME LIMIT, " v1" TRAFFIC PATTERN varied, " c<n>" RIVALS' CARS, trafficcheck's
    // " x1" / " x2" TRAFFIC CHECK Revenge / Extreme. Older codes
    // have fewer tags; the defaults' code is the same as before.
    static constexpr const char* kTags = "emsapkvcx";
    static constexpr int kTagCount = 9;
    std::string code() const {
        char b[96];
        int n = std::snprintf(b, sizeof b, "t%u l%u r%u d%u", unsigned(traffic), unsigned(laps), unsigned(rivals), unsigned(time));
        const std::uint8_t v[kTagCount] = {timed, manual, start, style, points, limit, seed, cars, check};
        for (int i = 0; i < kTagCount && n > 0 && n < int(sizeof b); ++i)
            if (v[i]) n += std::snprintf(b + n, sizeof b - std::size_t(n), " %c%u", kTags[i], unsigned(v[i]));
        return b;
    }
    static bool parse(const std::string& s, RaceChoice& c) {
        unsigned t, l, r, d;
        int used = 0;
        if (std::sscanf(s.c_str(), "t%u l%u r%u d%u%n", &t, &l, &r, &d, &used) != 4 || t >= unsigned(kRaceTrafficValues) ||
            l > unsigned(kRaceMaxLaps) || r > unsigned(kRoadRageMaxRivals) || r == unsigned(kRoadRageRivals) || d > 4)
            return false;
        static const unsigned kMax[kTagCount] = {1, 1, 1, 1, 1, unsigned(kRaceTimeLimits) - 1, 1, unsigned(kRaceRivalCars) - 1, 2};
        unsigned v[kTagCount] = {};
        int last = -1;
        const char* rest = s.c_str() + used;
        while (*rest == ' ') {   // the optional tags, each once, in kTags' order
            char tag = 0;
            unsigned x = 0;
            int k = 0;
            if (std::sscanf(rest, " %c%u%n", &tag, &x, &k) != 2 || k <= 0) return false;
            const char* at = std::char_traits<char>::find(kTags, kTagCount, tag);
            if (!at) return false;
            const int i = int(at - kTags);
            if (i <= last || x > kMax[i]) return false;
            v[i] = x;
            last = i;
            rest += k;
        }
        if (*rest) return false;
        c.traffic = std::uint8_t(t), c.laps = std::uint8_t(l), c.rivals = std::uint8_t(r), c.time = std::uint8_t(d);
        c.timed = std::uint8_t(v[0]), c.manual = std::uint8_t(v[1]), c.start = std::uint8_t(v[2]), c.style = std::uint8_t(v[3]);
        c.points = std::uint8_t(v[4]), c.limit = std::uint8_t(v[5]), c.seed = std::uint8_t(v[6]), c.cars = std::uint8_t(v[7]);
        c.check = std::uint8_t(v[8]);
        return true;
    }
    // The gameplay options as one word for the renderer (records kept apart: src/b3replay/records.h); 0: the game's.
    // The bits: kRaceWord* above (bit 15 the flag; others' bits in the same word: tests/test_bit_owners.cpp)
    std::uint64_t record_word() const {
        if (gameplay_default()) return 0;
        return kRaceWordFlag | (traffic & 3u) | std::uint64_t((traffic >> 2) & 1u) << 16 | std::uint64_t(laps & 15u) << 2 |
               std::uint64_t(rivals & 7u) << 6 | std::uint64_t(timed & 1u) << 9 | std::uint64_t(manual & 1u) << 10 |
               std::uint64_t(start & 1u) << 17 | std::uint64_t(style & 1u) << 18 | std::uint64_t(points & 1u) << 19 |
               std::uint64_t(limit & 7u) << 20 | std::uint64_t(seed & 1u) << 23 |
               // mixedrivals: Mixed its own bit 38 (35-37 hold 1..7 only)
               (cars == kRaceRivalCarsMixed ? std::uint64_t(1) << 38 : std::uint64_t(cars & 7u) << 35);
    }
};
// The folder a record word's records go to: src/b3replay/records.h options_folder (the same bits; tested together)
inline const char* race_traffic_name(int t) {   // (the log's)
    static const char* const kN[kRaceTrafficValues] = {"Off", "Light", "Normal", "Heavy", "Busy", "Extreme"};
    return t >= 0 && t < kRaceTrafficValues ? kN[t] : "Normal";
}
// The scale of the track's spawn rates (vehicles a minute per valve lane and category; the busy values: the game's 60 s
// spawn interval as 40 / 30 / 20 s)
inline float race_traffic_scale(int t) {
    static const float kS[kRaceTrafficValues] = {0.0f, 0.5f, 1.0f, 2.0f, 1.5f, 3.0f};
    return t >= 0 && t < kRaceTrafficValues ? kS[t] : 1.0f;
}
// TRAFFIC's row order (the page's: lightest to heaviest) as stored values
constexpr std::uint8_t kRaceTrafficOrder[kRaceTrafficValues] = {0, 1, 2, 4, 3, 5};

enum class RaceRow : std::uint8_t { kTraffic, kLaps, kRivals, kTime, kTimed, kGearbox,
                                    kStart, kStyle, kPoints, kLimit, kSeed, kCars,   // (options3's after gearbox's 5)
                                    kCheck,                                          // trafficcheck
                                    kPage,                                           // optpages: MORE (the page row)
                                    kWeather };   // condrows: WEATHER (Clear / Rain), after MORE so no row's index moves
constexpr unsigned kRaceRowKinds = 15;

// The rows' words in the game's text language (platform_disc.h language_text(): "" the disc's English, else the imported
// language's code). UTF-8 (the game's fonts have the European letters). Each value with its default tag fits a string slot
// (23 UTF-16 units, race_options.h kStringBytes; tests/test_race_options.cpp words_per_language). Where the game has the word (its string table:
// 709 LAPS, 712 RIVALS, 380 TRAFFIC, as FR / GE / IT have them) it is the game's; the rest are the port's.
struct RaceWords {
    const char* code;
    const char* label[kRaceRowKinds];   // by RaceRow (upper case, as the game's)
    const char* traffic[kRaceTrafficValues];   // by stored value: Off, Light, Normal, Heavy, Busy, Extreme
    const char* day;
    const char* night;
    const char* dflt;         // the tag of the default value, with its leading space
    const char* rolling;      // START
    const char* standing;
    const char* racing;       // RIVAL STYLE (the other value is "Road Rage", the mode's name)
    const char* takedowns;    // BURNOUT POINTS
    const char* all;
    const char* off;          // TIME LIMIT's none
    const char* fixed;        // TRAFFIC PATTERN
    const char* varied;
    const char* onoff[2];     // elim1: TIMED ELIMINATION's Off, On
    const char* gears[2];     // gearbox: TRANSMISSION's Automatic, Manual
    const char* cars[kRaceRivalCars];   // RIVALS' CARS
    const char* check[3];     // trafficcheck: TRAFFIC CHECK's Off, Revenge, Extreme
    const char* clear;        // condrows: WEATHER's Clear, Rain
    const char* rain;
};
inline const RaceWords& race_words(const std::string& code) {
    static const RaceWords kWords[] = {
        {"", {"TRAFFIC", "LAPS", "RIVALS", "TIME OF DAY", "TIMED ELIMINATION", "TRANSMISSION", "START", "RIVAL STYLE", "BURNOUT POINTS", "TIME LIMIT", "TRAFFIC PATTERN", "RIVALS' CARS", "TRAFFIC CHECK", "MORE", "WEATHER"},
         {"Off", "Light", "Normal", "Heavy", "Busy", "Extreme"}, "Day", "Night", " (Default)", "Rolling", "Standing", "Race",
         "Takedowns", "All", "Off", "Fixed", "Varied", {"Off", "On"}, {"Automatic", "Manual"},
         {"Your class", "Compact", "Muscle", "Coupe", "Sports", "Super", "Heavyweights", "Same as yours", "Mixed"}, {"Off", "Revenge", "Extreme"}, "Clear", "Rain"},
        {"FR", {"TRAFIC", "TOURS", "RIVAUX", "HEURE", "\xC3\x89LIMINATION CHRONO", "TRANSMISSION", "D\xC3\xA9" "PART", "STYLE RIVAUX", "POINTS BURNOUT", "LIMITE DE TEMPS", "SCH\xC3\x89MA TRAFIC", "VOITURES RIVALES", "BOUSCULADE", "SUITE", "M\xC3\x89T\xC3\x89O"},
         {"Aucun", "L\xC3\xA9ger", "Normal", "Dense", "Charg\xC3\xA9", "Extr\xC3\xAAme"}, "Jour", "Nuit", " (d\xC3\xA9" "faut)", "Lanc\xC3\xA9", "Arr\xC3\xAAt\xC3\xA9", "Course",
         "Takedowns", "Tous", "Non", "Fixe", "Variable", {"Non", "Oui"}, {"Automatique", "Manuelle"},
         {"Votre classe", "Compacte", "Muscle", "Coup\xC3\xA9", "Sport", "Super", "Poids lourds", "Comme la v\xC3\xB4" "tre", "Mixte"}, {"Non", "Revenge", "Extr\xC3\xAAme"}, "D\xC3\xA9gag\xC3\xA9", "Pluie"},
        {"GE", {"VERKEHR", "RUNDEN", "RIVALEN", "TAGESZEIT", "ZEIT-ELIMINIERUNG", "GETRIEBE", "START", "RIVALEN-STIL", "BURNOUT-PUNKTE", "ZEITLIMIT", "VERKEHRSMUSTER", "RIVALEN-AUTOS", "VERKEHR RAMMEN", "MEHR", "WETTER"},
         {"Aus", "Leicht", "Normal", "Dicht", "Viel", "Extrem"}, "Tag", "Nacht", " (Standard)", "Fliegend", "Stehend", "Rennen",
         "Takedowns", "Alle", "Aus", "Fest", "Wechselnd", {"Aus", "An"}, {"Automatik", "Manuell"},
         {"Deine Klasse", "Kompakt", "Muscle", "Coup\xC3\xA9", "Sport", "Super", "Schwergewichte", "Wie deins", "Gemischt"}, {"Aus", "Revenge", "Extrem"}, "Klar", "Regen"},
        {"IT", {"TRAFFICO", "GIRI", "RIVALI", "ORARIO", "ELIMINAZIONE A TEMPO", "CAMBIO", "PARTENZA", "STILE RIVALI", "PUNTI BURNOUT", "LIMITE TEMPO", "SCHEMA TRAFFICO", "AUTO RIVALI", "SPINTA AL TRAFFICO", "ALTRO", "METEO"},
         {"Assente", "Leggero", "Normale", "Intenso", "Trafficato", "Estremo"}, "Giorno", "Notte", " (default)", "Lanciata", "Da fermo", "Gara",
         "Takedown", "Tutti", "No", "Fisso", "Variabile", {"No", "S\xC3\xAC"}, {"Automatico", "Manuale"},
         {"Tua classe", "Compatta", "Muscle", "Coup\xC3\xA9", "Sportiva", "Super", "Pesi massimi", "Come la tua", "Misto"}, {"No", "Revenge", "Estremo"}, "Sereno", "Pioggia"},
        {"SP", {"TR\xC3\x81" "FICO", "VUELTAS", "RIVALES", "HORA", "ELIMINACI\xC3\x93N POR TIEMPO", "CAMBIO", "SALIDA", "ESTILO RIVALES", "PUNTOS BURNOUT", "L\xC3\x8D" "MITE TIEMPO", "PATR\xC3\x93N TR\xC3\x81" "FICO", "COCHES RIVALES", "EMBESTIR TR\xC3\x81" "FICO", "M\xC3\x81S", "CLIMA"},
         {"Nada", "Ligero", "Normal", "Denso", "Abundante", "Extremo"}, "D\xC3\xAD" "a", "Noche", " (predet.)", "Lanzada", "Parada", "Carrera",
         "Takedowns", "Todos", "No", "Fijo", "Variable", {"No", "S\xC3\xAD"}, {"Autom\xC3\xA1tico", "Manual"},
         {"Tu clase", "Compacto", "Muscle", "Coup\xC3\xA9", "Deportivo", "S\xC3\xBA" "per", "Pesos pesados", "Como el tuyo", "Mixto"}, {"No", "Revenge", "Extremo"}, "Despejado", "Lluvia"},
        {"DU", {"VERKEER", "RONDES", "RIVALEN", "TIJDSTIP", "ELIMINATIE OP TIJD", "VERSNELLINGSBAK", "START", "RIVALENSTIJL", "BURNOUT-PUNTEN", "TIJDLIMIET", "VERKEERSPATROON", "RIVALENAUTO'S", "VERKEER RAMMEN", "MEER", "WEER"},
         {"Uit", "Licht", "Normaal", "Druk", "Veel", "Extreem"}, "Dag", "Nacht", " (standaard)", "Vliegend", "Staand", "Race",
         "Takedowns", "Alle", "Uit", "Vast", "Wisselend", {"Uit", "Aan"}, {"Automaat", "Handgeschakeld"},
         {"Jouw klasse", "Compact", "Muscle", "Coup\xC3\xA9", "Sport", "Super", "Zwaargewichten", "Zoals de jouwe", "Gemengd"}, {"Uit", "Revenge", "Extreem"}, "Helder", "Regen"},
        {"SW", {"TRAFIK", "VARV", "RIVALER", "TID P\xC3\x85 DYGNET", "TIDSELIMINERING", "V\xC3\x84XELL\xC3\x85" "DA", "START", "RIVALSTIL", "BURNOUT-PO\xC3\x84NG", "TIDSGR\xC3\x84NS", "TRAFIKM\xC3\x96NSTER", "RIVALERNAS BILAR", "KNUFFA TRAFIK", "MER", "V\xC3\x84" "DER"},
         {"Av", "L\xC3\xA4tt", "Normal", "T\xC3\xA4t", "Mycket", "Extrem"}, "Dag", "Natt", " (standard)", "Flygande", "St\xC3\xA5" "ende", "Race",
         "Takedowns", "Alla", "Av", "Fast", "Varierande", {"Av", "P\xC3\xA5"}, {"Automat", "Manuell"},
         {"Din klass", "Kompakt", "Muscle", "Kup\xC3\xA9", "Sport", "Super", "Tungviktare", "Som din", "Blandat"}, {"Av", "Revenge", "Extrem"}, "Klart", "Regn"},
    };
    for (const RaceWords& w : kWords)
        if (code == w.code) return w;
    return kWords[0];   // English (US / EN, an unknown code)
}
inline const char* race_row_label(RaceRow r, const std::string& code = "") { return race_words(code).label[unsigned(r)]; }
// UTF-16 units of a UTF-8 string (the page's string slots hold 23)
inline std::size_t race_utf16_units(const std::string& s) {
    std::size_t n = 0;
    for (unsigned char c : s) n += (c & 0xC0u) != 0x80u ? ((c & 0xF8u) == 0xF0u ? 2u : 1u) : 0u;
    return n;
}

// The page's own rows before ours, by mode (RACE REGION, TRACK, RIVALS; TIME ATTACK and ROAD RAGE REGION, TRACK), and the
// options component's capacity: CB3VOptionsSelectComponent::Prepare 0x3BE2A0 keeps per-row state in arrays of 9 inside
// the component (+0x14 objects, +0x38, +0x5C, +0x80, +0xAC), so 9 rows is the game's limit, not ours to raise
constexpr int kRacePageGameRows[3] = {3, 2, 2};
constexpr int kRacePageMaxRows = 9;
// optpages (2026-11-22): our rows in pages. A mode whose rows do not fit the component's 9 with its own rows gets a
// last row MORE ("1 / 2"): Left / Right on it shows the other page's rows in place (race_options.h). Pure in (mode,
// time_row): the order is the same in every run and recording (a recording's presses reach the same rows); the rows
// before options3 come first as they were. Every page fits: the game's rows + the page + MORE <= 9, and page 1 is the
// largest (the component is prepared with page 1's count; race_options.h page_rows). A value on a page not shown stays
// as chosen (both pages' rows reach the race).
// - RACE: TRAFFIC, LAPS, TRANSMISSION, START, RIVALS' CARS | TRAFFIC CHECK, RIVAL STYLE, TRAFFIC PATTERN (+ TIME OF DAY,
//   + WEATHER)
// - TIME ATTACK: one page, TRAFFIC, TRANSMISSION (+ TIME OF DAY, + WEATHER), TRAFFIC PATTERN, START, TRAFFIC CHECK
// - ROAD RAGE: RIVALS, TRAFFIC, TRANSMISSION, TIME LIMIT, BURNOUT POINTS, START | RIVALS' CARS, TRAFFIC PATTERN,
//   TRAFFIC CHECK (+ TIME OF DAY, + WEATHER)
// condrows (2026-11-23, the owner's): TIME OF DAY (Day / Night, with EXTRAS' NIGHT) and WEATHER (Clear / Rain, with
// RAIN) are two rows, one after the other; with both, Night + Rain is night rain. Every page still fits (TIME ATTACK's
// one page exactly: its 2 + 7)
// - CRASH: none
using RacePages = std::vector<std::vector<RaceRow>>;
inline RacePages race_pages_for(int mode, bool time_row, bool weather_row = false) {
    using R = RaceRow;
    RacePages p;
    if (mode == 0) p = {{R::kTraffic, R::kLaps, R::kGearbox, R::kStart, R::kCars}, {R::kCheck, R::kStyle, R::kSeed}};
    else if (mode == 1) p = {{R::kTraffic, R::kGearbox, R::kSeed, R::kStart, R::kCheck}};
    else if (mode == 2) p = {{R::kRivals, R::kTraffic, R::kGearbox, R::kLimit, R::kPoints, R::kStart}, {R::kCars, R::kSeed, R::kCheck}};
    else return p;   // CRASH, anything else: none
    std::vector<RaceRow> cond;
    if (time_row) cond.push_back(R::kTime);
    if (weather_row) cond.push_back(R::kWeather);
    if (mode == 1) p[0].insert(p[0].begin() + 2, cond.begin(), cond.end());   // (TIME ATTACK: after TRANSMISSION, as before)
    else p.back().insert(p.back().end(), cond.begin(), cond.end());
    return p;
}
// extras (PC SETTINGS > EXTRAS > SELECT OPTIONS): the pages with only the rows the player has on (`mask`: bits by RaceRow
// index, b3settings::kExtraRaceRows; TIME OF DAY and WEATHER are night's and rain's own and stay as given), packed again in their order: one page
// when they fit the component beside the game's rows, else page 1 as full as a page with MORE can be and the rest on
// page 2 (with every row on: race_pages_for's pages, unchanged). Pure in (pages, mode, mask): a recording's word gives
// the same rows. No row on: no pages (the game's own page)
inline RacePages race_pages_masked(const RacePages& full, int mode, std::uint32_t mask) {
    RacePages out;
    if (mode < 0 || mode > 2) return out;
    std::vector<RaceRow> v;
    for (const auto& pg : full)
        for (RaceRow r : pg)
            if (r == RaceRow::kTime || r == RaceRow::kWeather || (mask >> unsigned(r) & 1u)) v.push_back(r);
    if (v.empty()) return out;
    const std::size_t room = std::size_t(kRacePageMaxRows - kRacePageGameRows[mode]);
    if (v.size() <= room) {
        out.push_back(v);
        return out;
    }
    const std::size_t first = room - 1;   // (MORE takes a row)
    out.emplace_back(v.begin(), v.begin() + std::ptrdiff_t(first));
    out.emplace_back(v.begin() + std::ptrdiff_t(first), v.end());
    return out;
}
// The rows a page shows (with MORE last when the mode has more than one page)
inline std::vector<RaceRow> race_page_rows(const RacePages& pages, int page) {
    if (pages.empty()) return {};
    std::vector<RaceRow> v = pages[std::size_t(page >= 0 && page < int(pages.size()) ? page : 0)];
    if (pages.size() > 1) v.push_back(RaceRow::kPage);
    return v;
}
// Every mode's pages fit the component with its own rows, page 1 the largest (checked in tests/test_race_options.cpp)
inline bool race_pages_fit(int mode, bool time_row, bool weather_row = false) {
    if (mode < 0 || mode > 2) return true;
    const RacePages p = race_pages_for(mode, time_row, weather_row);
    const std::size_t first = race_page_rows(p, 0).size();
    for (int k = 0; k < int(p.size()); ++k) {
        const std::size_t n = race_page_rows(p, k).size();
        if (kRacePageGameRows[mode] + int(n) > kRacePageMaxRows || n > first) return false;
    }
    return true;
}

// A row's field back to the game's value
inline void race_row_default(RaceRow r, RaceChoice& c) {
    switch (r) {
    case RaceRow::kTraffic: c.traffic = 2; break;
    case RaceRow::kLaps: c.laps = 0; break;
    case RaceRow::kRivals: c.rivals = 0; break;
    case RaceRow::kTime: c.time = 0; break;
    case RaceRow::kTimed: c.timed = 0; break;
    case RaceRow::kGearbox: c.manual = 0; break;
    case RaceRow::kStart: c.start = 0; break;
    case RaceRow::kStyle: c.style = 0; break;
    case RaceRow::kPoints: c.points = 0; break;
    case RaceRow::kLimit: c.limit = 0; break;
    case RaceRow::kSeed: c.seed = 0; break;
    case RaceRow::kCars: c.cars = 0; break;
    case RaceRow::kCheck: c.check = 0; break;
    case RaceRow::kPage: break;
    // (the rain half off, the night half as chosen: Day / Night as a value of its own -- 0 is the settings' pair, which
    // may be Night; Fable's review)
    case RaceRow::kWeather: c.time = std::uint8_t(c.time == 4 ? 2 : c.time == 3 ? 1 : c.time); break;
    }
}

// One row's values as the page shows them. track_laps: the track's own lap count (0 unknown); night: the setting's
// TIME OF DAY is Night. condrows: the race's conditions are one race_time.h value (c.time: 1 Day, 2 Night, 3 Rain,
// 4 Night rain; 0 the setting's own pair, TIME OF DAY and WEATHER); TIME OF DAY (Day / Night) shows and sets its night
// half, WEATHER (Clear / Rain; weather: the row is shown) its rain half. rain: the setting's WEATHER is Rain (only with
// the row).
struct RaceRowModel {
    int track_laps = 0;
    bool night = false;
    bool weather = false;
    bool rain = false;
    int pages = 1, page = 0;   // optpages: the page row's values (1 / 2, ...) and the page shown
    int setting_time() const { return (night ? 2 : 1) + (weather && rain ? 2 : 0); }   // as a race_time.h value
    // The conditions the choice gives (its own value, else the setting's), and a value from its two halves
    int conditions(const RaceChoice& c) const { return c.time >= 1 && c.time <= 4 ? c.time : setting_time(); }
    static bool at_night(int v) { return v == 2 || v == 4; }
    static bool in_rain(int v) { return v == 3 || v == 4; }
    void set_conditions(bool n, bool r, RaceChoice& c) const {
        const int v = (n ? 2 : 1) + (r ? 2 : 0);
        c.time = std::uint8_t(v == setting_time() ? 0 : v);
    }
    // A stored value with only what the rows shown offer (the page's Prepare: NIGHT / RAIN switched off): a half whose
    // row is off goes back to the setting's (Day, Clear); no row: 0
    int offered(int t, bool time_row, bool weather_row) const {
        if (!t || (!time_row && !weather_row)) return 0;
        RaceChoice c;
        c.time = std::uint8_t(t);
        set_conditions(time_row ? at_night(t) : night, weather_row && in_rain(t), c);
        return c.time;
    }
    const char* label(RaceRow r) const { return race_row_label(r, language); }
    std::string language;   // race_words' code
    bool lap_race() const { return track_laps > 1 && track_laps <= kRaceMaxLaps; }
    int count(RaceRow r) const {
        switch (r) {
        case RaceRow::kTraffic: return kRaceTrafficValues;
        case RaceRow::kLaps: return lap_race() ? kRaceMaxLaps : 1;
        case RaceRow::kRivals: return kRoadRageMaxRivals;
        case RaceRow::kTime: case RaceRow::kWeather: return 2;
        case RaceRow::kTimed: case RaceRow::kGearbox: case RaceRow::kStart: case RaceRow::kStyle:
        case RaceRow::kPoints: case RaceRow::kSeed: return 2;
        case RaceRow::kLimit: return kRaceTimeLimits;
        case RaceRow::kCars: return kRaceRivalCars;
        case RaceRow::kCheck: return 3;
        case RaceRow::kPage: return pages;
        }
        return 1;
    }
    bool disabled(RaceRow r) const { return count(r) <= 1; }
    int index(RaceRow r, const RaceChoice& c) const {
        switch (r) {
        case RaceRow::kTraffic:
            for (int i = 0; i < kRaceTrafficValues; ++i)
                if (kRaceTrafficOrder[i] == c.traffic) return i;
            return 2;
        case RaceRow::kLaps: return lap_race() ? (c.laps ? c.laps : track_laps) - 1 : 0;
        case RaceRow::kRivals: return (c.rivals ? c.rivals : kRoadRageRivals) - 1;
        case RaceRow::kTime: return at_night(conditions(c)) ? 1 : 0;
        case RaceRow::kWeather: return in_rain(conditions(c)) ? 1 : 0;
        case RaceRow::kTimed: return c.timed ? 1 : 0;
        case RaceRow::kGearbox: return c.manual ? 1 : 0;
        case RaceRow::kStart: return c.start;
        case RaceRow::kStyle: return c.style;
        case RaceRow::kPoints: return c.points;
        case RaceRow::kLimit: return c.limit;
        case RaceRow::kSeed: return c.seed;
        case RaceRow::kCars: return c.cars < kRaceRivalCars ? c.cars : 0;
        case RaceRow::kCheck: return c.check <= 2 ? c.check : 0;
        case RaceRow::kPage: return page;
        }
        return 0;
    }
    // The choice with the row at index i (the default value stored as "the game's": 0)
    void set(RaceRow r, int i, RaceChoice& c) const {
        switch (r) {
        case RaceRow::kTraffic: c.traffic = kRaceTrafficOrder[i >= 0 && i < kRaceTrafficValues ? i : 2]; break;
        case RaceRow::kLaps:
            if (lap_race()) c.laps = std::uint8_t(i + 1 == track_laps ? 0 : i + 1);
            break;
        case RaceRow::kRivals: c.rivals = std::uint8_t(i + 1 == kRoadRageRivals ? 0 : i + 1); break;
        case RaceRow::kTime: set_conditions(i == 1, weather && in_rain(conditions(c)), c); break;
        case RaceRow::kWeather: set_conditions(at_night(conditions(c)), i == 1, c); break;
        case RaceRow::kTimed: c.timed = std::uint8_t(i == 1 ? 1 : 0); break;
        case RaceRow::kGearbox: c.manual = std::uint8_t(i == 1 ? 1 : 0); break;
        case RaceRow::kStart: c.start = std::uint8_t(i == 1); break;
        case RaceRow::kStyle: c.style = std::uint8_t(i == 1); break;
        case RaceRow::kPoints: c.points = std::uint8_t(i == 1); break;
        case RaceRow::kLimit: c.limit = std::uint8_t(i >= 0 && i < kRaceTimeLimits ? i : 0); break;
        case RaceRow::kSeed: c.seed = std::uint8_t(i == 1); break;
        case RaceRow::kCars: c.cars = std::uint8_t(i >= 0 && i < kRaceRivalCars ? i : 0); break;
        case RaceRow::kCheck: c.check = std::uint8_t(i >= 0 && i <= 2 ? i : 0); break;
        case RaceRow::kPage: break;   // (not a value of the race: race_options.h keeps the page)
        }
    }
    std::string text(RaceRow r, int i) const {
        const RaceWords& w = race_words(language);
        std::string t;
        bool dflt = false;
        switch (r) {
        case RaceRow::kTraffic: {
            const int v = kRaceTrafficOrder[i >= 0 && i < kRaceTrafficValues ? i : 2];
            t = w.traffic[v];
            dflt = v == 2;
            break;
        }
        case RaceRow::kLaps:
            if (track_laps > 0) { t = std::to_string(lap_race() ? i + 1 : track_laps); dflt = !lap_race() || i + 1 == track_laps; }
            else t = "-";
            break;
        case RaceRow::kRivals: t = std::to_string(i + 1); dflt = i + 1 == kRoadRageRivals; break;
        case RaceRow::kTime: t = i == 0 ? w.day : w.night; dflt = (i == 1) == night; break;
        case RaceRow::kWeather: t = i == 0 ? w.clear : w.rain; dflt = (i == 1) == (weather && rain); break;
        case RaceRow::kTimed: t = w.onoff[i == 1 ? 1 : 0]; dflt = i == 0; break;
        case RaceRow::kGearbox: t = w.gears[i == 1 ? 1 : 0]; dflt = i == 0; break;
        case RaceRow::kStart: t = i == 0 ? w.rolling : w.standing; dflt = i == 0; break;
        case RaceRow::kStyle: t = i == 0 ? w.racing : "Road Rage"; dflt = i == 0; break;
        case RaceRow::kPoints: t = i == 0 ? w.takedowns : w.all; dflt = i == 0; break;
        case RaceRow::kLimit: {
            const int sec = race_time_limit_seconds(i);
            t = sec ? std::to_string(sec / 60) + ":00" : w.off;
            dflt = i == 0;
            break;
        }
        case RaceRow::kSeed: t = i == 0 ? w.fixed : w.varied; dflt = i == 0; break;
        case RaceRow::kCars: t = w.cars[i >= 0 && i < kRaceRivalCars ? i : 0]; dflt = i == 0; break;
        case RaceRow::kCheck: t = w.check[i >= 0 && i <= 2 ? i : 0]; dflt = i == 0; break;
        case RaceRow::kPage: t = std::to_string(i + 1) + " / " + std::to_string(pages); break;
        }
        return dflt ? t + w.dflt : t;
    }
};

// Whether the page's options reach the race now: the page shown since its last reset (mode 0..2; the main menu and
// SELECT MODE reset it), the game's current mode the one-player stage record built for SINGLE EVENT (World Tour's
// carries another tag; MULTIPLAYER another record), game type 1 (Single Event) and its mode the page's.
// [C RequestOnePlayerGameMode 0x30C2C0: the record 0x0051AD00, +0x1B8 0x0051B920 Single Event / 0x0051B928 World Tour]
struct RaceStage {
    std::uint32_t current_mode = 0;   // *(0x0051BA88)
    std::uint32_t tag = 0;            // the record's +0x1B8
    std::int32_t game_type = 0;       // CB3MenuChoices +4
    std::int32_t mode = -1;           // CB3MenuChoices +8
};
constexpr std::uint32_t kRaceOnePlayerStage = 0x0051AD00u, kRaceSingleEventTag = 0x0051B920u;
inline bool race_options_apply(int page_mode, const RaceStage& s) {
    return page_mode >= 0 && page_mode <= 2 && s.current_mode == kRaceOnePlayerStage && s.tag == kRaceSingleEventTag &&
           s.game_type == 1 && s.mode == page_mode;
}

// The race's laps with the choice: 0 keeps the game's (`game`: what the stage gives); TIME ATTACK and ROAD RAGE keep
// theirs, as does a race that is not a lap race
inline int race_laps(const RaceChoice& c, int mode, int game) {
    if (mode != 0 || c.laps == 0 || game <= 1 || game > kRaceMaxLaps) return 0;
    return c.laps;
}

// A stage's lap count in a track's GAMEDATA.BGD (b3asset/crash.h: the 0x800-byte directory {+0x08 u64 stage id[50],
// +0x198 s32 header offset[50], +0x260 s32 count}; CB3GameData::GetNumLaps 0x14E860 reads the stage header's +0x3B8 [C]).
// 0: no such stage, or the file is short.
inline int race_bgd_laps(const std::uint8_t* f, std::size_t size, std::uint64_t stage) {
    auto u32 = [&](std::size_t o) {
        return std::uint32_t(f[o]) | std::uint32_t(f[o + 1]) << 8 | std::uint32_t(f[o + 2]) << 16 | std::uint32_t(f[o + 3]) << 24;
    };
    if (!f || size < 0x800) return 0;
    const std::int32_t n = std::int32_t(u32(0x260));
    for (std::int32_t i = 0; i >= 0 && i < n && i < 50; ++i) {
        const std::uint64_t id = std::uint64_t(u32(0x08 + 8u * unsigned(i))) | std::uint64_t(u32(0x0C + 8u * unsigned(i))) << 32;
        if (id != stage) continue;
        const std::size_t h = u32(0x198 + 4u * unsigned(i));
        if (h > size || size - h < 0x3BC) return 0;
        return std::int32_t(u32(h + 0x3B8));
    }
    return 0;
}

}  // namespace b3native

#endif
