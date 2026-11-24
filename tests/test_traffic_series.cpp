// trafficseries (tools/native/traffic_series.h): the traffic vehicles as race cars and the TRAFFIC / TRAFFIC SPECIAL series --
// the table against the game's traffic table (order, ids), the series (sizes within the carousel's 20 rows, what is
// left out), the steps after the last series allowed, the disc names, the tuning files, the model file made of a .BTV
// and its donor's engine sound models, the record word and its folder, the replay's entry.
#include "test.h"

#include "traffic_series.h"
#include "../src/b3replay/records.h"

#include <cstdint>
#include <set>
#include <string>
#include <vector>

namespace {

using namespace b3native;

TEST(gtid_compress_matches_the_vehicle_list) {
    CHECK(gtid_compress("HEVYCAR1") == 0x7704283866EB4000ull);   // Heavy Pick Up [the vehicle list's id]
    CHECK(gtid_compress("COMPCAR1") == 0x5B55839B95EB4000ull);
    CHECK(gtid_compress("HEVYCAR9") == 0x770428386823C000ull);   // City Bus
}

TEST(the_table_is_the_games_traffic_table) {
    // D_004CDC00's 40 ids in order [C FindTrafficVehicle 0x3EAD20]: COMP CAR11-19, HEVY CAR11-35, TSPC CAR1-6
    const auto& t = traffic_table();
    CHECK(t.size() == std::size_t(kTrafficVehicles));
    CHECK(t[0].id() == 0x5B55839B95EF2800ull && t[0].name() == "COMPCAR11");
    CHECK(t[7].id() == 0x5B55839B95F5FE00ull && t[7].name() == "COMPCAR18");    // Tuk Tuk (string 1901 = 1894 + 7)
    CHECK(t[17].id() == 0x7704283866F6F800ull && t[17].name() == "HEVYCAR19");  // Bus (1911)
    CHECK(t[27].id() == 0x77042838671E0800ull && t[27].name() == "HEVYCAR29");  // the tanker [C the crash code's id]
    CHECK(t[34].id() == 0xBEE0FDB3A0EB4000ull && t[39].name() == "TSPCCAR6");
    std::set<std::uint64_t> ids;
    for (std::size_t i = 0; i < t.size(); ++i) {
        ids.insert(t[i].id());
        CHECK(traffic_index(t[i].id()) == int(i));
    }
    CHECK(ids.size() == t.size());
    CHECK(traffic_index(gtid_compress("COMPCAR1")) == -1);   // a race car
    CHECK(traffic_index(0) == -1);
}

TEST(two_series_within_the_carousel) {
    const std::vector<int> a = traffic_series_cars(kTrafficClass), b = traffic_series_cars(kTrafficSpecialClass);
    CHECK(a.size() == 20 && b.size() == 7);
    CHECK(int(a.size()) <= kTrafficSeriesRows && int(b.size()) <= kTrafficSeriesRows);
    CHECK(a[7] == 7);                         // the tuk tuk: TRAFFIC's 8th
    CHECK(b[0] == 17);                        // the first bus: TRAFFIC SPECIAL's first
    for (int i = 22; i <= 28; ++i) CHECK(traffic_table()[std::size_t(i)].series == 0);   // the trailers
    for (int i = 34; i <= 39; ++i) CHECK(traffic_table()[std::size_t(i)].series == 0);   // the trams
    for (const TrafficEntry& t : traffic_table()) {   // donors: race cars, the race classes
        CHECK(traffic_index(t.donor_id()) == -1);
        CHECK(traffic_class_of(t.donor_cls) >= 0 && traffic_class_of(t.donor_cls) <= 6);
    }
    CHECK(traffic_class_of("HEVY") == 6 && traffic_class_of("CUPE") == 2);
}

TEST(steps_after_the_last_series) {
    for (int last : {4, 5, 6}) {
        CHECK(traffic_next_class(last, last) == kTrafficClass);
        CHECK(traffic_next_class(kTrafficClass, last) == kTrafficSpecialClass);
        CHECK(traffic_next_class(kTrafficSpecialClass, last) == -1);
        CHECK(traffic_next_class(last - 1, last) == -1);   // the game's own step
        CHECK(traffic_prev_class(kTrafficSpecialClass, last) == kTrafficClass);
        CHECK(traffic_prev_class(kTrafficClass, last) == last);
        CHECK(traffic_prev_class(last, last) == -1);
    }
    CHECK(carousel_last_class(false, false) == 4 && carousel_last_class(true, false) == 5 && carousel_last_class(true, true) == 6);
    CHECK(traffic_series_event(true, 1) && !traffic_series_event(false, 1) && !traffic_series_event(true, 0) && !traffic_series_event(true, 2));
}

TEST(disc_names) {
    CHECK(traffic_alias("pveh\\COMP\\CAR18.bgv") == "PVEH/COMP/CAR18.BTV");
    CHECK(traffic_alias("pveh\\COMP\\CAR18.hwd") == "PVEH/COMP/CAR1.HWD");
    CHECK(traffic_alias("pveh\\HEVY\\CAR19.lwd") == "PVEH/HEVY/CAR9.LWD");
    CHECK(traffic_alias("pveh\\COMP\\CAR1.bgv").empty());     // a race car's
    CHECK(traffic_alias("pveh\\COMP\\CAR18.btv").empty());    // the traffic's own model
    CHECK(traffic_alias("pveh\\COMP\\CAR1.hwd").empty());
}

TEST(tuning_files) {
    const std::vector<int> racing = {7, 17};   // the tuk tuk, a bus
    const std::string bus = "../Export/ValueDB/VehiclePhysics/HEVYCAR19.cfg";
    CHECK(traffic_config(racing, bus, "Torque", "Physics/Transmission/Engine", true) == "../Export/ValueDB/VehiclePhysics/HEVYCAR9.cfg");
    CHECK(traffic_config(racing, bus, "Mass (Kg)", "Physics/Vehicle", true) == "../Export/ValueDB/VehiclePhysics/HEVYCAR9.cfg");
    CHECK(traffic_config(racing, bus, "Spring Force", "Physics/Suspension/Front", true) == "../Export/ValueDB/VehiclePhysics/HEVYCAR9.cfg");
    CHECK(traffic_config(racing, bus, "Spring Length", "Physics/Suspension/Front", true).empty());   // its own geometry
    CHECK(traffic_config(racing, bus, "Attatch Height", "Physics/Suspension/Rear", true).empty());
    // the tuk tuk has no VehiclePhysics file: all the donor's
    CHECK(traffic_config(racing, "../Export/ValueDB/VehiclePhysics/COMPCAR18.cfg", "Spring Length", "Physics/Suspension/Front", true) ==
          "../Export/ValueDB/VehiclePhysics/COMPCAR1.cfg");
    CHECK(traffic_config(racing, "../Export/ValueDB/Camera/compcar18.cfg", "x", "Camera", false) == "../Export/ValueDB/Camera/COMPCAR1.cfg");
    CHECK(traffic_config(racing, "../Export/ValueDB/Camera/COMPCAR15.cfg", "x", "Camera", false).empty());   // not racing
    CHECK(traffic_config({}, bus, "Torque", "Physics/Transmission/Engine", true).empty());
    // the traffic system's own registrations of the model (the traffic on the road): kept
    CHECK(traffic_config(racing, bus, "Mass (Kg)", "Physics/Vehicle", false).empty());
    CHECK(traffic_config(racing, "../export/ValueDB/VehicleMaterial/HEVYCAR19.cfg", "x", "Material", true).empty());
}

std::vector<std::uint8_t> file_of(std::size_t size, std::uint32_t hi, std::uint32_t lo, std::uint8_t fill) {
    std::vector<std::uint8_t> b(size, fill);
    auto put = [&](std::size_t o, std::uint32_t v) { for (int k = 0; k < 4; ++k) b[o + std::size_t(k)] = std::uint8_t(v >> (8 * k)); };
    put(0x08, std::uint32_t(size));
    put(0x16DC, hi);
    put(0x16E0, lo);
    return b;
}

TEST(model_file) {
    const std::vector<std::uint8_t> btv = file_of(0x3000, 0, 0, 0x11), bgv = file_of(0x6000, 0x5000, 0x5800, 0x22);
    const std::vector<std::uint8_t> m = traffic_model(btv, bgv);
    CHECK(m.size() == bgv.size());
    CHECK(m[0x08] == 0x00 && m[0x09] == 0x30);                 // the .BTV's own size (the renderer knows its header)
    CHECK(m[0x16DC] == 0x00 && m[0x16DD] == 0x50 && m[0x16E1] == 0x58);   // the donor's engine models
    CHECK(m[0x2FFF] == 0x11 && m[0x3000] == 0 && m[0x4FFF] == 0);        // the .BTV, then zeros
    CHECK(m[0x5000] == 0x22 && m.back() == 0x22);              // the donor's tail
    CHECK(traffic_model(file_of(0x5100, 0, 0, 1), bgv).empty());   // reaches past the donor's engine models
    CHECK(traffic_model(btv, file_of(0x6000, 0, 0, 2)).empty());   // a donor without them
}

TEST(records_apart) {
    using b3replay::records::options_folder;
    CHECK(traffic_record_word() == 0x10008002ull);
    CHECK(options_folder(traffic_record_word()) == "options-trafficcars");
    CHECK(options_folder(traffic_record_word(0x8000u)) == "options-trafficcars-traffic-off");   // with TRAFFIC Off: one folder
    CHECK((traffic_record_word() & 0x7FFFu) == 0x0002u);   // no other owner's low bits
}

TEST(replay_entry) {
    CHECK(traffic_series_entry("1"));
    CHECK(!traffic_series_entry("0") && !traffic_series_entry("") && !traffic_series_entry(nullptr));
}

}  // namespace

TEST(donor_ids) {   // tsdonor: the helper gameopts reuses
    CHECK(traffic_donor_id(gtid_compress("COMPCAR18")) == gtid_compress("COMPCAR1"));    // the tuk tuk: Compact Type 1
    CHECK(traffic_donor_id(gtid_compress("HEVYCAR19")) == gtid_compress("HEVYCAR9"));    // the bus: the City Bus
    CHECK(traffic_donor_id(gtid_compress("COMPCAR1")) == 0);   // a race car: none
    CHECK(traffic_donor_id(0) == 0);
    for (const TrafficEntry& t : traffic_table()) CHECK(traffic_donor_id(t.id()) == t.donor_id() && traffic_donor_id(t.id()) != 0);
}

TEST_MAIN("traffic vehicles as race cars, the TRAFFIC series (trafficseries)")
