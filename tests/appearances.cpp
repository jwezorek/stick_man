#include "core/sm_project.hpp"
#include "json.hpp"
#include "miniz.h"
#include "core/sm_package.hpp"
#include <algorithm>
#include <limits>
#include <iostream>
#include <stdexcept>

void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
template<class F> void rejects(F fn) {
    bool rejected = false;
    try { fn(); } catch (const std::exception&) { rejected = true; }
    require(rejected, "invalid artwork accepted");
}
sm::image_resource fixture() {
    return sm::image_resource::from_rgba(2, 2, {255,0,0,0, 0,255,0,73, 0,0,255,128, 5,6,7,255});
}
void same_pixels(const sm::image_resource& a, const sm::image_resource& b) {
    require(a.width() == b.width() && a.height() == b.height(), "image dimensions changed");
    for (int y = 0; y < a.height(); ++y) require(std::ranges::equal(a.row(y), b.row(y)), "RGBA pixels changed");
}
void resources_and_semantics() {
    auto img = fixture();
    same_pixels(img, sm::image_resource::decode(img.encode_png()));
    auto region = img.region({1, 0, 1, 2});
    same_pixels(region, sm::image_resource::decode(region.encode_png()));
    rejects([&] { img.region({std::numeric_limits<int>::max(), 0, 1, 1}); });
    rejects([&] { sm::image_resource::decode({}); });
    sm::artwork art;
    art.insert_frame("eye", img.encode_png());
    rejects([&] { art.insert_frame("eye", img.encode_png()); });
    auto bone = sm::object_id::generate();
    rejects([&] { art.add_slot("bad", {bone, sm::bone_anchor::root, {"open"}}); });
    rejects([&] { art.add_slot("bad", {bone, sm::bone_anchor::root, {"default", "default"}}); });
    art.add_slot("eyes", {bone}); art.add_slot("glasses", {bone});
    rejects([&] { art.add_slot("eyes", {bone}); });
    art.add_state("eyes", "closed");
    art.add_appearance("Human", {{{"eyes", {{"default", "eye"}, {"closed", std::nullopt}}}}});
    rejects([&] { art.add_appearance("Human"); });
    rejects([&] { art.add_appearance("bad", {{{"unknown"}}}); });
    rejects([&] { art.add_appearance("bad", {{{"eyes", {}}}}); });
    rejects([&] { art.add_appearance("bad", {{{"eyes", {{"default", "missing"}}}}}); });
    rejects([&] { art.add_appearance("bad", {{{"eyes", {{"default", "eye"}, {"invalid", "eye"}}}}}); });
    rejects([&] { art.add_appearance("bad", {{{"eyes"}, {"eyes"}}}); });
    rejects([&] { art.delete_frame("eye"); });
    require(!art.resolve_frame("Human", "eyes", "closed"), "hidden state fell back");
    require(art.resolve_frame("Human", "eyes", "open") == "eye", "missing state did not fall back");
    art.rename_frame("eye", "renamed"); art.rename_slot("eyes", "face"); art.rename_state("face", "closed", "shut");
    require(art.resolve_frame("Human", "face") == "renamed", "rename lost frame reference");
    require(art.appearances().at("Human").appearance_slots[0].states.contains("shut"), "state rename lost reference");
    rejects([&] { art.delete_state("face", "default"); });
    rejects([&] { art.rename_state("face", "default", "x"); });
    art.delete_state("face", "shut"); art.delete_slot("face"); art.delete_frame("renamed");
    require(art.appearances().at("Human").appearance_slots.empty(), "slot deletion left implementation");
    art.rename_appearance("Human", "Robot"); art.delete_appearance("Robot");
    for (int i = 0; i < 3; ++i) art.insert_frame(std::to_string(i), {img, {double(i), -2}});
    auto packed = art.pack(4);
    require(packed.pages.size() == 3 && packed.frames.size() == 3, "multipage packing failed");
    for (const auto& f : packed.frames) {
        auto page = sm::image_resource::decode(packed.pages.at(f.page).png);
        same_pixels(img, page.region(f.rect));
        require(std::ranges::equal(page.row(0).first(4), img.row(0).first(4)), "padding not extruded");
        require(f.registration_origin == art.frames().at(f.name).registration_origin, "packed origin lost");
    }
    rejects([&] { art.pack(3); });
    sm::artwork wide;
    wide.insert_frame("wide", {sm::image_resource::from_rgba(2050, 1, sm::image_buffer(2050 * 4, 255)), {}});
    require(wide.pack().frames[0].rect.width == 2050, "default packing must handle large frames");
    rejects([&] { wide.insert_frame("too-wide", {sm::image_resource::from_rgba(8191, 1, sm::image_buffer(8191 * 4)), {}}); });
}
void persistence() {
    sm::project p;
    auto& sk = p.create_skeleton({0, 0});
    std::vector<sm::const_skel_ref> members{sk};
    auto id = p.create_character(members)->get().id();
    auto& art = p.artwork(id);
    art.insert_frame("eye", {fixture(), {1, -3}});
    art.add_slot("eyes", {sm::object_id::generate(), sm::bone_anchor::tip, {"default", "closed"}});
    art.add_appearance("Robot", {{{"eyes", {{"default", "eye"}, {"closed", std::nullopt}}, {{4, 5}, 0.3, {-1, 2}}}}});
    require(!p.slot_resolved(id, "eyes"), "missing bone resolved");
    auto encoded = p.serialize(); require(encoded.has_value(), "artwork save failed");
    sm::project loaded; require(loaded.deserialize(*encoded) == sm::project_result::success, "artwork load failed");
    same_pixels(fixture(), loaded.artwork(id).frames().at("eye").image);
    require(loaded.artwork(id).frames().at("eye").registration_origin == sm::point{1,-3}, "origin lost");
    const auto& slot = loaded.artwork(id).appearances().at("Robot").appearance_slots[0];
    require(slot.transform.scale == sm::point{-1,2} && slot.transform.rotation == 0.3, "transform lost");
    require(!loaded.artwork(id).resolve_frame("Robot", "eyes", "closed"), "hidden mapping lost");
    loaded.artwork(id).insert_frame("new", fixture().encode_png());
    auto mixed = loaded.serialize(); require(mixed.has_value(), "mixed backing save failed");
    require(loaded.deserialize(*mixed) == sm::project_result::success, "mixed backing load failed");
    same_pixels(fixture(), loaded.artwork(id).frames().at("new").image);
    sm::detail::package_reader reader(*encoded);
    auto bytes = reader.read("project.json");
    auto semantic = nlohmann::json::parse(bytes);
    require(semantic["version"] == 5.0, "artwork requires a new package version");
    auto prefix = "characters/" + id.to_string() + "/artwork/";
    auto png = reader.read(prefix + "page-0.png");
    auto malformed = [&](auto mutate, bool include_page = true) {
        auto j = semantic; mutate(j["characters"][0]["artwork"]);
        sm::detail::package_writer writer;
        auto text = j.dump();
        writer.add("project.json", {reinterpret_cast<const std::uint8_t*>(text.data()), text.size()});
        if (include_page) writer.add(prefix + "page-0.png", png);
        require(loaded.deserialize(writer.finish()) != sm::project_result::success, "malformed artwork accepted");
        require(loaded.artwork(id).frames().contains("new"), "failed load changed live project");
    };
    malformed([](auto&) {}, false);
    malformed([](auto& a) { a["frames"][0]["rect"][0] = 100000; });
    malformed([](auto& a) { a["frames"][0]["rect"][2] = 0; });
    malformed([](auto& a) { a["frames"][0]["page"] = -1; });
    malformed([](auto& a) { a["frames"].push_back(a["frames"][0]); });
    malformed([](auto& a) { a["slots"].push_back(a["slots"][0]); });
    malformed([](auto& a) { a["appearances"].push_back(a["appearances"][0]); });
    malformed([](auto& a) { a["appearances"][0]["slots"][0]["states"]["default"] = "missing"; });
    malformed([](auto& a) { a["appearances"][0]["slots"][0]["slot"] = "missing"; });
    malformed([](auto& a) { a["appearances"][0]["slots"][0]["states"]["bad"] = nullptr; });
    auto legacy = semantic;
    legacy["version"] = 4.0; legacy["characters"][0].erase("artwork");
    sm::detail::package_writer legacy_writer;
    auto legacy_text = legacy.dump();
    legacy_writer.add("project.json", {reinterpret_cast<const std::uint8_t*>(legacy_text.data()), legacy_text.size()});
    require(loaded.deserialize(legacy_writer.finish()) == sm::project_result::success, "legacy v4 project did not load");
    require(loaded.artwork(id).frames().empty(), "legacy artwork must start empty");
}
void bone_replacement() {
    sm::project p;
    auto& a = p.create_skeleton({0, 0});
    auto& b = p.create_skeleton({1, 0});
    auto bone = p.create_bone("bone", a.root_node(), b.root_node());
    require(bone.has_value(), "bone fixture failed");
    const auto bone_id = bone->get().id(), skeleton_id = bone->get().owner().id();
    std::vector<sm::const_skel_ref> members{bone->get().owner()};
    auto id = p.create_character(members)->get().id();
    p.artwork(id).add_slot("part", {bone_id});
    require(p.slot_resolved(id, "part"), "live member bone unresolved");
    auto& loose = p.create_skeleton({5, 0});
    p.artwork(id).add_slot("not-bone", {loose.root_node().id()});
    require(!p.slot_resolved(id, "not-bone"), "node reference resolved as bone");
    sm::topology scratch;
    auto copied = bone->get().owner().copy_to(scratch);
    auto change = p.replace_skeletons({skeleton_id}, {copied->get()}, {bone_id});
    require(change.status == sm::result::success, "bone replacement failed");
    require(p.slot_resolved(id, "part"), "replacement remap lost artwork binding");
    require(p.artwork(id).slot_definitions().at("part").bone != bone_id, "replacement failed to remap bone");
}
int main() {
    try {
        resources_and_semantics();
        persistence();
        bone_replacement();
        sm::project project;
        auto& skeleton = project.create_skeleton({0, 0});
        std::vector<sm::const_skel_ref> members{skeleton};
        auto character = project.create_character(members);
        auto buffer = project.serialize();
        require(buffer.has_value(), "serialize failed");
        mz_zip_archive zip{};
        require(mz_zip_reader_init_mem(&zip, buffer->data(), buffer->size(), 0), "archive failed");
        size_t size = 0;
        auto* data = mz_zip_reader_extract_file_to_heap(&zip, "project.json", &size, 0);
        auto json = nlohmann::json::parse(static_cast<char*>(data), static_cast<char*>(data) + size);
        mz_free(data);
        mz_zip_reader_end(&zip);
        require(json["characters"][0].contains("artwork"), "character must serialize owned artwork");
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
