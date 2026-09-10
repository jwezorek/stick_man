#include "json.hpp"
#include "model/project.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace {
using nlohmann::json;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

json arm_json() {
    return json::parse(R"({"skeletons":[{
        "id":"00000000-0000-0000-0000-000000000001", "name":"arm", "root":"shoulder",
        "nodes":[
            {"name":"shoulder", "pos":{"x":0,"y":0}},
            {"name":"elbow", "pos":{"x":1,"y":0}},
            {"name":"wrist", "pos":{"x":2,"y":0}}],
        "bones":[
            {"id":"00000000-0000-0000-0000-000000000002", "name":"forearm", "u":"elbow", "v":"wrist"},
            {"id":"00000000-0000-0000-0000-000000000003", "name":"upper arm", "u":"shoulder", "v":"elbow"}]
    }]})");
}

void check_constraints(const sm::skeleton& skel) {
    auto relative = skel.get_by_name<sm::bone>("forearm")->get().rotation_constraint();
    require(relative.has_value(), "parent-relative constraint was lost");
    require(relative->relative_to_parent && relative->start_angle == 0.25 && relative->span_angle == 1.5,
        "parent-relative constraint changed");
    auto absolute = skel.get_by_name<sm::bone>("upper arm")->get().rotation_constraint();
    require(absolute.has_value() && !absolute->relative_to_parent &&
        absolute->start_angle == -0.5 && absolute->span_angle == 2.0, "absolute constraint changed");
}

void copying(const std::string& mode) {
    sm::topology source;
    auto fixture = arm_json();
    require(source.from_json(fixture) == sm::result::success, "fixture failed");
    // Force the unordered bone iteration to visit the child before its parent.
    if ((*(*source.skeletons().begin())->bones().begin())->name() != "forearm") {
        std::swap(fixture["skeletons"][0]["bones"][0]["id"], fixture["skeletons"][0]["bones"][1]["id"]);
        require(source.from_json(fixture) == sm::result::success, "fixture reload failed");
    }
    auto& arm = (*source.skeletons().begin()).get();
    require((*arm.bones().begin())->name() == "forearm", "fixture must visit child first");
    require(arm.get_by_name<sm::bone>("forearm")->get().set_rotation_constraint(0.25, 1.5, true)
        == sm::result::success, "fixture relative constraint failed");
    arm.get_by_name<sm::bone>("upper arm")->get().set_rotation_constraint(-0.5, 2.0, false);
    sm::topology dest;
    if (mode == "copy") {
        require(arm.copy_to(dest).has_value(), "copy failed");
    } else if (mode == "remap") {
        std::unordered_map<sm::object_id, sm::object_id> remap;
        remap[arm.id()] = sm::object_id::generate();
        for (auto node : arm.nodes()) remap[node->id()] = sm::object_id::generate();
        for (auto bone : arm.bones()) remap[bone->id()] = sm::object_id::generate();
        require(arm.copy_to(dest, remap).has_value(), "remapped copy failed");
    } else if (mode == "duplicate") {
        require(arm.duplicate_to(dest).has_value(), "duplicate failed");
    } else if (mode == "json") {
        auto saved = source.to_json();
        auto& bones = saved["skeletons"][0]["bones"];
        for (int order = 0; order < 2; ++order) {
            require(dest.from_json(saved) == sm::result::success, "JSON load failed");
            check_constraints((*dest.skeletons().begin()).get());
            std::reverse(bones.begin(), bones.end());
        }
    } else {
        sm::project original;
        require(original.copy_skeleton(arm).has_value(), "project copy failed");
        // Set constraints on the live source so this tests archive loading independently of copying.
        auto& live = (*original.topology().skeletons().begin()).get();
        live.get_by_name<sm::bone>("forearm")->get().set_rotation_constraint(0.25, 1.5, true);
        auto saved = original.serialize();
        require(saved.has_value(), "archive save failed");
        sm::project loaded;
        require(loaded.deserialize(*saved) == sm::project_result::success, "archive load failed");
        check_constraints((*loaded.topology().skeletons().begin()).get());
        return;
    }
    check_constraints((*dest.skeletons().begin()).get());
}

void replacement(const std::string& mode) {
    mdl::project project;
    sm::topology source;
    require(source.from_json(arm_json()) == sm::result::success, "fixture failed");
    auto& arm = (*source.skeletons().begin()).get();
    std::vector<sm::object_id> replacees;
    std::unordered_set<sm::object_id> regenerate;
    if (mode != "paste") {
        auto& old = project.core().create_skeleton({10, 10});
        replacees.push_back(old.id());
        if (mode == "collision") {
            require(project.core().copy_skeleton(arm).has_value(), "collision fixture failed");
        } else {
            regenerate.insert(arm.id());
            for (auto node : arm.nodes()) regenerate.insert(node->id());
            for (auto bone : arm.bones()) regenerate.insert(bone->id());
        }
    }
    project.replace_skeletons(replacees, {sm::skel_ref(arm)}, regenerate);
    sm::object_id added_id;
    for (auto skel : project.topology().skeletons()) {
        if (skel->id() != arm.id()) added_id = skel->id();
    }
    auto added = project.topology().skeleton(added_id);
    require(added.has_value(), "replacement missing");
    auto node_id = added->get().get_by_name<sm::node>("wrist")->get().id();
    auto bone_id = added->get().get_by_name<sm::bone>("forearm")->get().id();
    project.transform({node_id}, std::function<void(sm::node&)>([](sm::node& node) { node.set_world_pos({7, 8}); }));
    project.rename(mdl::skel_piece{added->get().get<sm::bone>(bone_id).value()}, "edited bone");
    for (int cycle = 0; cycle < 3; ++cycle) {
        project.undo();
        project.undo();
        project.undo();
        require(!project.topology().skeleton(added_id), "undo left replacement behind");
        project.redo();
        require(project.topology().skeleton(added_id).has_value(), "redo changed skeleton ID");
        require(project.topology().get<sm::node>(node_id).has_value(), "redo changed node ID");
        require(project.topology().get<sm::bone>(bone_id).has_value(), "redo changed bone ID");
        project.redo();
        project.redo();
        require(project.topology().get<sm::node>(node_id)->get().world_x() == 7, "later move did not replay");
        require(project.topology().get<sm::bone>(bone_id)->get().name() == "edited bone", "later rename did not replay");
        require(project.core().has_unique_object_ids(), "duplicate live IDs");
    }
}
}

int main(int argc, char** argv) {
    try {
        require(argc == 2, "expected test case name");
        std::string mode = argv[1];
        if (mode == "regenerate" || mode == "collision" || mode == "paste") replacement(mode);
        else copying(mode);
        std::cout << "PASS " << mode << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
