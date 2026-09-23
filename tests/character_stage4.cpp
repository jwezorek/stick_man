#include "core/sm_project.hpp"
#include "json.hpp"
#include "miniz.h"

#include <algorithm>
#include <cstring>
#include <iostream>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

namespace {
using nlohmann::json;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

std::size_t character_count(const sm::project& project) {
    return static_cast<std::size_t>(std::ranges::distance(project.characters()));
}

sm::object_id make_character(sm::project& project, std::initializer_list<sm::skeleton*> skeletons) {
    std::vector<sm::const_skel_ref> members;
    members.reserve(skeletons.size());
    for (auto* skeleton : skeletons) members.emplace_back(*skeleton);
    auto character = project.create_character(members);
    require(character.has_value(), "character fixture creation failed");
    return character->get().id();
}

json extract_project_json(std::span<const std::uint8_t> buffer) {
    mz_zip_archive archive{};
    require(mz_zip_reader_init_mem(&archive, buffer.data(), buffer.size(), 0) != 0,
        "could not open project archive");
    const int file_index = mz_zip_reader_locate_file(&archive, "project.json", nullptr, 0);
    require(file_index >= 0, "project.json missing from archive");
    size_t size = 0;
    void* data = mz_zip_reader_extract_to_heap(
        &archive, static_cast<mz_uint>(file_index), &size, 0);
    require(data != nullptr, "could not extract project.json");
    std::string text(static_cast<const char*>(data), size);
    mz_free(data);
    mz_zip_reader_end(&archive);
    return json::parse(text);
}

sm::project_buffer archive_with_project_json(const json& semantic_project) {
    const auto text = semantic_project.dump(4);
    mz_zip_archive archive{};
    require(mz_zip_writer_init_heap(&archive, 0, 0) != 0, "could not create archive");
    require(mz_zip_writer_add_mem(&archive, "project.json", text.data(), text.size(),
        MZ_DEFAULT_COMPRESSION) != 0, "could not add project.json");
    void* archive_data = nullptr;
    size_t archive_size = 0;
    require(mz_zip_writer_finalize_heap_archive(&archive, &archive_data, &archive_size) != 0,
        "could not finalize archive");
    sm::project_buffer buffer(archive_size);
    if (archive_size != 0) std::memcpy(buffer.data(), archive_data, archive_size);
    mz_free(archive_data);
    mz_zip_writer_end(&archive);
    return buffer;
}

void require_membership_invariant(const sm::project& project) {
    require(project.has_consistent_membership(), "project membership invariant failed");
    for (auto character : project.characters()) {
        require(!character->rig().empty(), "loaded character has empty rig");
        for (auto skeleton : character->rig().skeletons()) {
            auto parent = skeleton->parent_character();
            require(parent.has_value(), "loaded rig member has no parent character");
            require(parent->get().id() == character->id(), "loaded parent disagrees with rig");
        }
    }
}

void round_trip_characters() {
    sm::project source;
    auto& torso = source.create_skeleton({0, 0});
    auto& eyes = source.create_skeleton({10, 0});
    auto& prop = source.create_skeleton({20, 0});
    auto& loose = source.create_skeleton({30, 0});
    const auto torso_id = torso.id();
    const auto eyes_id = eyes.id();
    const auto prop_id = prop.id();
    const auto loose_id = loose.id();

    const auto alice_id = make_character(source, {&torso, &eyes});
    const auto prop_character_id = make_character(source, {&prop});
    source.rename(alice_id, "Alice");

    auto saved = source.serialize();
    require(saved.has_value(), "character project serialization failed");

    const auto semantic = extract_project_json(*saved);
    require(semantic.at("version").get<double>() == 7.0, "constraint persistence did not bump project version");
    require(semantic.contains("characters") && semantic.at("characters").is_array(),
        "project.json has no character collection");
    require(semantic.at("characters").size() == 2, "project.json lost a character");

    sm::project loaded;
    require(loaded.deserialize(*saved) == sm::project_result::success, "character project load failed");
    require(character_count(loaded) == 2, "character count did not round-trip");
    require_membership_invariant(loaded);

    auto alice = loaded.character(alice_id);
    require(alice.has_value(), "character identity did not round-trip");
    require(alice->get().name() == "Alice", "character name did not round-trip");
    require(alice->get().rig().size() == 2, "multi-skeleton rig size did not round-trip");
    require(alice->get().rig().contains(torso_id) && alice->get().rig().contains(eyes_id),
        "multi-skeleton rig membership did not round-trip");

    auto prop_character = loaded.character(prop_character_id);
    require(prop_character.has_value(), "second character identity did not round-trip");
    require(prop_character->get().name() == "character-2", "second character name did not round-trip");
    require(prop_character->get().rig().size() == 1 && prop_character->get().rig().contains(prop_id),
        "single-skeleton rig did not round-trip");

    auto loaded_loose = loaded.topology().skeleton(loose_id);
    require(loaded_loose.has_value() && loaded_loose->get().is_loose(),
        "loose skeleton was adopted during load");

    auto& another = loaded.create_skeleton({40, 0});
    const auto next_id = make_character(loaded, {&another});
    require(loaded.character(next_id)->get().name() == "character-3",
        "default character-name counter was not advanced after load");
}

void invalid_character_data_is_atomic() {
    sm::project fixture;
    auto& first = fixture.create_skeleton({0, 0});
    auto& second = fixture.create_skeleton({10, 0});
    const auto first_id = first.id();
    const auto second_id = second.id();
    make_character(fixture, {&first});
    make_character(fixture, {&second});
    auto saved = fixture.serialize();
    require(saved.has_value(), "invalid-data fixture serialization failed");
    const auto good = extract_project_json(*saved);

    auto expect_rejected = [&](json bad, sm::project_result expected, const char* message) {
        sm::project destination;
        auto& survivor = destination.create_skeleton({99, 99});
        const auto survivor_id = survivor.id();
        const auto survivor_character_id = make_character(destination, {&survivor});
        destination.rename(survivor_character_id, "Survivor");
        const auto before_topology = destination.topology().to_json_str();

        auto archive = archive_with_project_json(bad);
        require(destination.deserialize(archive) == expected, message);
        require(destination.topology().to_json_str() == before_topology,
            "rejected load changed existing topology");
        auto survivor_character = destination.character(survivor_character_id);
        require(survivor_character.has_value() && survivor_character->get().name() == "Survivor",
            "rejected load changed existing characters");
        require(destination.topology().skeleton(survivor_id).has_value(),
            "rejected load removed existing skeleton");
        require_membership_invariant(destination);
    };

    auto missing = good;
    missing["characters"][0]["skeletons"][0] = sm::object_id::generate().to_string();
    expect_rejected(missing, sm::project_result::invalid_project_json,
        "missing rig skeleton was accepted");

    auto shared = good;
    shared["characters"][1]["skeletons"][0] = shared["characters"][0]["skeletons"][0];
    expect_rejected(shared, sm::project_result::invalid_project_json,
        "skeleton belonging to two characters was accepted");

    auto duplicate_member = good;
    duplicate_member["characters"][0]["skeletons"].push_back(
        duplicate_member["characters"][0]["skeletons"][0]);
    expect_rejected(duplicate_member, sm::project_result::invalid_project_json,
        "duplicate skeleton within a rig was accepted");

    auto empty = good;
    empty["characters"][0]["skeletons"] = json::array();
    expect_rejected(empty, sm::project_result::invalid_project_json,
        "empty character rig was accepted");

    auto colliding_id = good;
    colliding_id["characters"][0]["id"] = first_id.to_string();
    expect_rejected(colliding_id, sm::project_result::duplicate_object_id,
        "character ID collision with topology was accepted");

    auto duplicate_character_id = good;
    duplicate_character_id["characters"][1]["id"] = duplicate_character_id["characters"][0]["id"];
    expect_rejected(duplicate_character_id, sm::project_result::duplicate_object_id,
        "duplicate character ID was accepted");

    auto malformed_id = good;
    malformed_id["characters"][0]["id"] = "not-an-object-id";
    expect_rejected(malformed_id, sm::project_result::invalid_project_json,
        "malformed character ID was accepted");

    (void)second_id;
}

}

int main() {
    try {
        round_trip_characters();
        invalid_character_data_is_atomic();
        std::cout << "PASS character stage 4\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
