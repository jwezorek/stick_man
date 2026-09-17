#include "core/sm_project.hpp"
#include "core/sm_package.hpp"
#include "json.hpp"
#include <iostream>
#include <stdexcept>

void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
int main() {
    try {
        sm::project p;
        auto& s = p.create_skeleton({12, 34});
        std::vector<sm::const_skel_ref> rig{s};
        auto c = p.create_character(rig).value();
        auto& assets = p.animation_data(c->id());
        require(assets.poses.size() == 1, "character must capture Default");
        require(assets.poses.front().node_positions.at(s.root_node().id()).x == 12, "Default captures position");
        auto pose = assets.poses.front();
        pose.id = sm::object_id::generate(); pose.name = "Raised";
        assets.poses.push_back(pose);
        sm::animation a; a.name = "Test"; a.base_pose = pose.id;
        require(a.duration() == 0, "empty animation duration");
        a.layers.resize(2);
        a.layers[0].actions.push_back({sm::object_id::generate(), 100, 250, sm::easing::linear,
            sm::rigid_translation{{s.id()}, {2, 3}}});
        a.layers[1].actions.push_back({sm::object_id::generate(), 20, 40, sm::easing::smoothstep,
            sm::rigid_rotation{sm::object_id::generate(), sm::rotation_pivot::tip, 0.5}});
        assets.animations.push_back(a);
        require(a.duration() == 350, "duration is maximum end, not sum");
        sm::project loaded;
        require(loaded.deserialize(p.serialize().value()) == sm::project_result::success, "archive round trip");
        const auto& copy = loaded.animation_data(c->id());
        require(copy.poses.size() == 2 && copy.animations.size() == 1, "assets survive archive");
        require(copy.animations[0].duration() == 350, "millisecond timing survives archive");
        require(copy.animations[0].base_pose == pose.id, "base pose identity survives archive");
        auto semantic = sm::animation_assets_to_json(assets);
        auto bad = semantic;
        bad["animations"][0]["layers"][0][0]["duration"] = 0;
        bool rejected = false;
        try { sm::animation_assets_from_json(bad); } catch (...) { rejected = true; }
        require(rejected, "zero duration is rejected");
        bad = semantic; bad["animations"][0]["layers"][0][0]["start"] = 1.5;
        rejected = false;
        try { sm::animation_assets_from_json(bad); } catch (...) { rejected = true; }
        require(rejected, "fractional millisecond time is rejected");
        auto buffer = p.serialize().value();
        sm::detail::package_reader archive(buffer);
        auto bytes = archive.read("project.json");
        auto legacy = nlohmann::json::parse(bytes);
        legacy["characters"][0].erase("animation_data");
        auto legacy_text = legacy.dump();
        sm::detail::package_writer writer;
        writer.add("project.json", {reinterpret_cast<const std::uint8_t*>(legacy_text.data()), legacy_text.size()});
        sm::project old;
        require(old.deserialize(writer.finish()) == sm::project_result::success, "missing animation data remains loadable");
        require(old.animation_data(c->id()).poses.size() == 1 && old.animation_data(c->id()).animations.empty(), "legacy gets Default and no animations");
        require(old.animation_data(c->id()).poses[0].node_positions.at(s.root_node().id()).y == 34, "legacy Default captures loaded rig");
        require(sm::ease(sm::easing::ease_in, .5) == .25, "quadratic ease in");
        require(sm::ease(sm::easing::ease_out, .5) == .75, "quadratic ease out");
        require(sm::ease(sm::easing::ease_in_out, .25) == .125, "symmetric ease in/out");
        require(sm::ease(sm::easing::smoothstep, .5) == .5, "smoothstep midpoint");
        require(sm::ease(sm::easing::linear, -1) == 0 && sm::ease(sm::easing::linear, 2) == 1, "easing clamps progress");
        sm::ik_translation ik{s.root_node().id(), {s.root_node().id()}, sm::target_reference::node, s.root_node().id(),
            sm::spline_path{{{{0,0},{1,2},{3,4},{5,6}}}}};
        assets.animations[0].layers.push_back({{{sm::object_id::generate(), 500, 123, sm::easing::ease_in, ik}}});
        auto round_trip = sm::animation_assets_from_json(sm::animation_assets_to_json(assets));
        const auto& copied_ik = std::get<sm::ik_translation>(round_trip.animations[0].layers.back().actions[0].data);
        require(copied_ik.pins.size() == 1 && copied_ik.reference_node == s.root_node().id(), "IK references preserved");
        require(std::get<sm::spline_path>(copied_ik.path).segments[0].control2.y == 4, "spline geometry preserved");
        auto membership = p.snapshot_membership({s.id()});
        p.remove_character(c->id());
        require(p.restore_membership(membership) == sm::result::success, "restore membership");
        require(p.animation_data(membership.characters[0].id).animations.size() == 1, "undo snapshot preserves assets");
        std::cout << "animation stage 1 passed\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
