#include "sm_artwork_io.hpp"
#include <limits>

namespace {
    using nlohmann::json;
    json point_json(sm::point p) { return json::array({p.x, p.y}); }
    sm::point read_point(const json& j) {
        if (!j.is_array() || j.size() != 2) throw std::invalid_argument("Invalid point");
        return {j.at(0).get<double>(), j.at(1).get<double>()};
    }
    const json& array(const json& j, const char* key) {
        const auto& value = j.at(key);
        if (!value.is_array()) throw std::invalid_argument("Expected artwork array");
        return value;
    }
    int integer(const json& j) {
        if (!j.is_number_integer() || j.get<double>() < 0 || j.get<double>() > std::numeric_limits<int>::max())
            throw std::invalid_argument("Invalid image coordinate");
        return j.get<int>();
    }
}
nlohmann::json sm::detail::write_artwork(const artwork& art, const std::string& prefix, package_writer& package) {
    auto packed = art.pack();
    json result{{"pages", json::array()}, {"frames", json::array()}, {"slots", json::array()}, {"appearances", json::array()}};
    for (std::size_t i = 0; i < packed.pages.size(); ++i) {
        auto name = "page-" + std::to_string(i) + ".png";
        package.add(prefix + name, packed.pages[i].png);
        result["pages"].push_back(name);
    }
    for (const auto& f : packed.frames) result["frames"].push_back({{"name", f.name}, {"origin", point_json(f.registration_origin)},
        {"page", f.page}, {"rect", {f.rect.x, f.rect.y, f.rect.width, f.rect.height}}});
    for (const auto& [name, slot] : art.slot_definitions()) result["slots"].push_back({{"name", name}, {"bone", slot.bone.to_string()},
        {"anchor", slot.anchor == bone_anchor::root ? "root" : "tip"}, {"states", slot.states}});
    for (const auto& [name, app] : art.appearances()) {
        json slots = json::array();
        for (const auto& slot : app.appearance_slots) {
            json states = json::object();
            for (const auto& [state, target] : slot.states) states[state] = target ? json(*target) : json(nullptr);
            slots.push_back({{"slot", slot.slot}, {"states", std::move(states)},
                {"translation", point_json(slot.transform.translation)}, {"rotation", slot.transform.rotation}, {"scale", point_json(slot.transform.scale)}});
        }
        result["appearances"].push_back({{"name", name}, {"slots", std::move(slots)}});
    }
    return result;
}
sm::artwork sm::detail::read_artwork(const nlohmann::json& j, const std::string& prefix, package_reader& package) {
    artwork art;
    std::vector<image_resource> pages;
    for (const auto& page : array(j, "pages")) {
        auto expected = "page-" + std::to_string(pages.size()) + ".png";
        if (page.get<std::string>() != expected) throw std::invalid_argument("Invalid page name");
        auto png = package.read(prefix + expected);
        constexpr std::uint8_t signature[]{137,80,78,71,13,10,26,10};
        if (png.size() < 8 || !std::equal(std::begin(signature), std::end(signature), png.begin()))
            throw std::invalid_argument("Page must be PNG");
        pages.push_back(image_resource::decode(png));
    }
    for (const auto& f : array(j, "frames")) {
        const auto& r = f.at("rect");
        if (!r.is_array() || r.size() != 4) throw std::invalid_argument("Invalid frame rectangle");
        const auto page = integer(f.at("page"));
        art.insert_frame(f.at("name").get<std::string>(), sprite_frame{
            pages.at(page).region({integer(r[0]), integer(r[1]), integer(r[2]), integer(r[3])}), read_point(f.at("origin"))});
    }
    for (const auto& s : array(j, "slots")) {
        auto bone = object_id::from_string(s.at("bone").get<std::string>());
        if (!bone) throw std::invalid_argument("Invalid slot bone ID");
        auto anchor = s.at("anchor").get<std::string>();
        if (anchor != "root" && anchor != "tip") throw std::invalid_argument("Invalid bone anchor");
        art.add_slot(s.at("name").get<std::string>(), {*bone, anchor == "root" ? bone_anchor::root : bone_anchor::tip,
            array(s, "states").get<std::vector<std::string>>()});
    }
    for (const auto& a : array(j, "appearances")) {
        appearance app;
        for (const auto& s : array(a, "slots")) {
            appearance_slot slot; slot.slot = s.at("slot").get<std::string>(); slot.states.clear();
            const auto& states = s.at("states");
            if (!states.is_object()) throw std::invalid_argument("Invalid appearance mappings");
            for (const auto& [state, target] : states.items()) slot.states.emplace(state, target.is_null() ? frame_target{} : frame_target{target.get<std::string>()});
            slot.transform = {read_point(s.at("translation")), s.at("rotation").get<double>(), read_point(s.at("scale"))};
            app.appearance_slots.push_back(std::move(slot));
        }
        art.add_appearance(a.at("name").get<std::string>(), std::move(app));
    }
    return art;
}
