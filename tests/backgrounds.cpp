#include "core/sm_project.hpp"
#include "core/sm_package.hpp"
#include "json.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace {
void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}

template<class F> void rejects(F fn, const char* message) {
    bool rejected = false;
    try { fn(); } catch (const std::exception&) { rejected = true; }
    require(rejected, message);
}

sm::image_resource fixture(std::uint8_t bias = 0) {
    return sm::image_resource::from_rgba(2, 2,
        {std::uint8_t(20 + bias), 0, 0, 255, 0, std::uint8_t(40 + bias), 0, 128,
            0, 0, std::uint8_t(60 + bias), 255, std::uint8_t(80 + bias), 90, 100, 255});
}

void same_pixels(const sm::image_resource& a, const sm::image_resource& b) {
    require(a.width() == b.width() && a.height() == b.height(), "background dimensions changed");
    for (int y = 0; y < a.height(); ++y)
        require(std::ranges::equal(a.row(y), b.row(y)), "background pixels changed");
}

nlohmann::json project_json(std::span<const std::uint8_t> package_bytes) {
    sm::detail::package_reader reader(package_bytes);
    auto bytes = reader.read("project.json");
    return nlohmann::json::parse(
        std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
}

void missing_backgrounds_is_empty() {
    sm::project original;
    auto saved = original.serialize();
    require(saved.has_value(), "empty project save failed");

    auto semantic = project_json(*saved);
    require(semantic["version"] == 8.0, "backgrounds must not bump project version");
    semantic.erase("backgrounds");
    auto text = semantic.dump();
    sm::detail::package_writer writer;
    writer.add("project.json",
        {reinterpret_cast<const std::uint8_t*>(text.data()), text.size()});
    auto legacy_shape = writer.finish();

    sm::project loaded;
    require(loaded.deserialize(legacy_shape) == sm::project_result::success,
        "project without backgrounds field did not load");
    require(loaded.backgrounds().empty(), "missing backgrounds field was not treated as empty");
}

void round_trip_and_order() {
    sm::project project;
    const auto back = project.add_background("blueprint", fixture());
    const auto front = project.add_background("notes", fixture(10));
    project.set_background_transform(back, {{12.5, -8.25}, 0.375, {1.5, -0.75}});
    project.set_background_transform(front, {{-3, 4}, -0.125, {0.5, 2.0}});
    project.reorder_background(back, 1);

    require(project.backgrounds().size() == 2 && project.backgrounds()[0].id == front &&
        project.backgrounds()[1].id == back, "background reorder failed before save");

    auto saved = project.serialize();
    require(saved.has_value(), "background project save failed");
    auto semantic = project_json(*saved);
    require(semantic["version"] == 8.0, "background feature changed project version");
    require(semantic["backgrounds"].size() == 2 && semantic["backgrounds"][0]["order"] == 0 &&
        semantic["backgrounds"][1]["order"] == 1, "explicit background order was not serialized");

    sm::project loaded;
    require(loaded.deserialize(*saved) == sm::project_result::success,
        "background project load failed");
    require(loaded.backgrounds().size() == 2, "background count changed on round trip");
    require(loaded.backgrounds()[0].id == front && loaded.backgrounds()[1].id == back,
        "background painter order changed on round trip");
    require(loaded.backgrounds()[0].name == "notes" && loaded.backgrounds()[1].name == "blueprint",
        "background names changed on round trip");
    same_pixels(fixture(10), loaded.backgrounds()[0].image);
    same_pixels(fixture(), loaded.backgrounds()[1].image);
    const auto& transform = loaded.backgrounds()[1].transform;
    require(transform.translation == sm::point{12.5, -8.25} && transform.rotation == 0.375 &&
        transform.scale == sm::point{1.5, -0.75}, "background transform changed on round trip");
}

void deletion_reordering_and_invariants() {
    sm::project project;
    const auto a = project.add_background("a", fixture());
    const auto b = project.add_background("b", fixture(1));
    const auto c = project.add_background("c", fixture(2));
    project.delete_background(b);
    require(project.backgrounds().size() == 2 && project.backgrounds()[0].id == a &&
        project.backgrounds()[1].id == c, "background deletion changed remaining identities/order");
    project.reorder_background(c, 0);
    require(project.backgrounds()[0].id == c && project.backgrounds()[1].id == a,
        "background reorder changed identities");

    auto saved = project.serialize();
    require(saved.has_value(), "background delete/reorder save failed");
    sm::project reloaded;
    require(reloaded.deserialize(*saved) == sm::project_result::success &&
        reloaded.backgrounds().size() == 2 && reloaded.backgrounds()[0].id == c &&
        reloaded.backgrounds()[1].id == a,
        "background delete/reorder order changed on round trip");

    auto duplicate = project.backgrounds();
    duplicate.push_back(duplicate.front());
    rejects([&] { project.set_backgrounds(duplicate); }, "duplicate background ID accepted");

    auto& skeleton = project.create_skeleton({0, 0});
    auto collision = project.backgrounds();
    collision.push_back({skeleton.id(), "collision", fixture(), {}});
    rejects([&] { project.set_backgrounds(collision); }, "background ID collision accepted");

    auto invalid = project.backgrounds();
    invalid.front().transform.rotation = std::numeric_limits<double>::infinity();
    rejects([&] { project.set_backgrounds(invalid); }, "non-finite background transform accepted");
    require(project.backgrounds().size() == 2 && project.backgrounds()[0].id == c,
        "rejected background edit mutated project");
}
}

int main() {
    try {
        missing_backgrounds_is_empty();
        round_trip_and_order();
        deletion_reordering_and_invariants();
        std::cout << "PASS backgrounds\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL backgrounds: " << e.what() << '\n';
        return 1;
    }
}
