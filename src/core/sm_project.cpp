#include "sm_project.hpp"
#include "json.hpp"
#include "miniz.h"
#include <cstring>
#include <stdexcept>
#include <string_view>
#include <unordered_set>
#include <utility>

using json = nlohmann::json;

/*------------------------------------------------------------------------------------------------*/

namespace {
    template<class... Ts> struct overloaded : Ts... { using Ts::operator()...; };

    constexpr std::string_view project_json_name = "project.json";
    constexpr double project_json_version = 3.0;

    template<typename Object>
    sm::object_id object_id_of(const Object& object) {
        return std::visit(
            [](auto ref) { return ref->id(); },
            object
        );
    }

    template<typename Object>
    bool build_object_index(
            sm::topology& topology,
            std::unordered_map<sm::object_id, Object>& objects) {
        objects.clear();
        auto insert = [&objects](Object object) {
            return objects.emplace(object_id_of(object), object).second;
        };

        for (auto skel : topology.skeletons()) {
            if (!insert(skel)) {
                objects.clear();
                return false;
            }
            for (auto node : skel->nodes()) {
                if (!insert(node)) {
                    objects.clear();
                    return false;
                }
            }
            for (auto bone : skel->bones()) {
                if (!insert(bone)) {
                    objects.clear();
                    return false;
                }
            }
        }
        return true;
    }
}

/*------------------------------------------------------------------------------------------------*/

void sm::project::invalidate_object_index() noexcept {
    object_index_dirty_ = true;
}

bool sm::project::rebuild_object_index() {
    if (!build_object_index(topology_, objects_)) {
        return false;
    }
    object_index_dirty_ = false;
    return true;
}

bool sm::project::ensure_object_index() const {
    if (!object_index_dirty_) {
        return true;
    }
    return const_cast<project*>(this)->rebuild_object_index();
}

const sm::topology& sm::project::topology() const {
    return topology_;
}

sm::skeleton& sm::project::create_skeleton(const point& pt) {
    auto& created = topology_.create_skeleton(pt);
    invalidate_object_index();
    if (!ensure_object_index()) {
        throw std::runtime_error("creating skeleton produced duplicate object IDs");
    }
    return created;
}

sm::expected_skel sm::project::copy_skeleton(
        const skeleton& source,
        const std::unordered_map<object_id, object_id>& id_remap) {
    if (!ensure_object_index()) {
        return std::unexpected(result::duplicate_id);
    }

    auto mapped_id = [&id_remap](const object_id& id) {
        auto it = id_remap.find(id);
        return it == id_remap.end() ? id : it->second;
    };
    auto collides = [this, &mapped_id](const object_id& id) {
        return objects_.contains(mapped_id(id));
    };

    if (collides(source.id())) {
        return std::unexpected(result::duplicate_id);
    }
    for (auto node : source.nodes()) {
        if (collides(node->id())) {
            return std::unexpected(result::duplicate_id);
        }
    }
    for (auto bone : source.bones()) {
        if (collides(bone->id())) {
            return std::unexpected(result::duplicate_id);
        }
    }

    auto copied = source.copy_to(topology_, id_remap);
    if (!copied) {
        return copied;
    }
    invalidate_object_index();
    if (!ensure_object_index()) {
        throw std::runtime_error("copying skeleton produced duplicate object IDs");
    }
    return copied;
}

sm::result sm::project::delete_skeleton(const object_id& id) {
    auto deleted = topology_.delete_skeleton(id);
    if (deleted != result::success) {
        return deleted;
    }
    invalidate_object_index();
    if (!ensure_object_index()) {
        throw std::runtime_error("deleting skeleton left duplicate object IDs");
    }
    return result::success;
}

sm::expected_bone sm::project::create_bone(const std::string& name, node& u, node& v) {
    auto created = topology_.create_bone(name, u, v);
    if (!created) {
        return created;
    }
    invalidate_object_index();
    if (!ensure_object_index()) {
        throw std::runtime_error("creating bone produced duplicate object IDs");
    }
    return created;
}

sm::expected_bone sm::project::create_bone(
        object_id id, const std::string& name, node& u, node& v) {
    if (!ensure_object_index()) {
        return std::unexpected(result::duplicate_id);
    }
    if (objects_.contains(id)) {
        return std::unexpected(result::duplicate_id);
    }
    auto created = topology_.create_bone(id, name, u, v);
    if (!created) {
        return created;
    }
    invalidate_object_index();
    if (!ensure_object_index()) {
        throw std::runtime_error("creating bone produced duplicate object IDs");
    }
    return created;
}

sm::topology_change sm::project::replace_skeletons(
        const std::vector<object_id>& replacees,
        const std::vector<skel_ref>& replacements,
        const std::unordered_set<object_id>& regenerate_ids) {
    topology_change change;
    change.removed_skeleton_ids.reserve(replacees.size());
    change.added_skeleton_ids.reserve(replacements.size());

    for (const auto& replacee : replacees) {
        if (delete_skeleton(replacee) == result::success) {
            change.removed_skeleton_ids.push_back(replacee);
        }
    }

    if (!ensure_object_index()) {
        throw std::runtime_error("project contains duplicate object IDs");
    }
    std::unordered_set<object_id> used_ids;
    used_ids.reserve(objects_.size());
    for (const auto& [id, object] : objects_) {
        used_ids.insert(id);
    }

    auto allocation_guard = used_ids;
    for (auto replacement : replacements) {
        allocation_guard.insert(replacement->id());
        for (auto node : replacement->nodes()) {
            allocation_guard.insert(node->id());
        }
        for (auto bone : replacement->bones()) {
            allocation_guard.insert(bone->id());
        }
    }
    auto unused_object_id = [&allocation_guard]() {
        while (true) {
            auto id = object_id::generate();
            if (allocation_guard.insert(id).second) {
                return id;
            }
        }
    };

    for (auto replacement : replacements) {
        std::unordered_map<object_id, object_id> id_remap;
        auto reserve_id = [&](const object_id& id) {
            if (regenerate_ids.contains(id) || used_ids.contains(id)) {
                auto new_id = unused_object_id();
                used_ids.insert(new_id);
                id_remap[id] = new_id;
            } else {
                used_ids.insert(id);
            }
        };

        reserve_id(replacement->id());
        for (auto node : replacement->nodes()) {
            reserve_id(node->id());
        }
        for (auto bone : replacement->bones()) {
            reserve_id(bone->id());
        }

        auto copied = copy_skeleton(replacement.get(), id_remap);
        if (!copied) {
            throw std::runtime_error("skeleton copy failed");
        }
        change.added_skeleton_ids.push_back(copied->get().id());
    }

    return change;
}

const sm::project::mutable_object& sm::project::get_mutable(const object_id& id) const {
    if (!ensure_object_index()) {
        throw std::runtime_error("project contains duplicate object IDs");
    }
    auto it = objects_.find(id);
    if (it == objects_.end()) {
        throw std::runtime_error("project object ID not found");
    }
    return it->second;
}

sm::mutable_project_object sm::project::get(const object_id& id) {
    return std::visit(overloaded{
        [](node_ref ref) -> mutable_project_object { return ref; },
        [](bone_ref ref) -> mutable_project_object { return ref; },
        [](skel_ref) -> mutable_project_object {
            throw std::runtime_error("project object does not support mutable lookup");
        }
    }, get_mutable(id));
}

void sm::project::rename(object_id id, std::string name) {
    std::visit([&name](auto ref) { ref->set_name(name); }, get_mutable(id));
}

sm::const_project_object sm::project::get(const object_id& id) const {
    return std::visit(
        overloaded{
            [](sm::node_ref ref) -> sm::const_project_object {
                return sm::const_node_ref(std::as_const(ref.get()));
            },
            [](sm::bone_ref ref) -> sm::const_project_object {
                return sm::const_bone_ref(std::as_const(ref.get()));
            },
            [](sm::skel_ref ref) -> sm::const_project_object {
                return sm::const_skel_ref(std::as_const(ref.get()));
            }
        },
        get_mutable(id)
    );
}

bool sm::project::has_unique_object_ids() const {
    return ensure_object_index();
}

void sm::project::clear() {
    topology_.clear();
    objects_.clear();
    object_index_dirty_ = false;
}

std::expected<sm::project_buffer, sm::project_result> sm::project::serialize() const {
    if (!ensure_object_index()) {
        return std::unexpected(project_result::duplicate_object_id);
    }

    json semantic_project = {
        {"version", project_json_version},
        {"topology", topology_.to_json()}
    };
    auto project_json = semantic_project.dump(4);

    mz_zip_archive archive{};
    if (!mz_zip_writer_init_heap(&archive, 0, 0)) {
        return std::unexpected(project_result::archive_error);
    }

    if (!mz_zip_writer_add_mem(
            &archive,
            project_json_name.data(),
            project_json.data(),
            project_json.size(),
            MZ_DEFAULT_COMPRESSION)) {
        mz_zip_writer_end(&archive);
        return std::unexpected(project_result::archive_error);
    }

    void* archive_data = nullptr;
    size_t archive_size = 0;
    if (!mz_zip_writer_finalize_heap_archive(&archive, &archive_data, &archive_size)) {
        mz_zip_writer_end(&archive);
        return std::unexpected(project_result::archive_error);
    }

    project_buffer buffer(archive_size);
    if (archive_size != 0) {
        std::memcpy(buffer.data(), archive_data, archive_size);
    }
    mz_free(archive_data);
    mz_zip_writer_end(&archive);
    return buffer;
}

sm::project_result sm::project::deserialize(std::span<const std::uint8_t> buffer) {
    if (buffer.empty()) {
        return project_result::invalid_archive;
    }

    mz_zip_archive archive{};
    if (!mz_zip_reader_init_mem(&archive, buffer.data(), buffer.size(), 0)) {
        return project_result::invalid_archive;
    }

    const int file_index = mz_zip_reader_locate_file(
        &archive, project_json_name.data(), nullptr, 0);
    if (file_index < 0) {
        mz_zip_reader_end(&archive);
        return project_result::missing_project_json;
    }

    size_t project_json_size = 0;
    void* project_json_data = mz_zip_reader_extract_to_heap(
        &archive, static_cast<mz_uint>(file_index), &project_json_size, 0);
    if (!project_json_data) {
        mz_zip_reader_end(&archive);
        return project_result::archive_error;
    }

    std::string project_json(
        static_cast<const char*>(project_json_data), project_json_size);
    mz_free(project_json_data);
    mz_zip_reader_end(&archive);

    sm::topology new_topology;
    try {
        auto semantic_project = json::parse(project_json);
        if (semantic_project.at("version").get<double>() != project_json_version) {
            return project_result::invalid_project_json;
        }
        if (new_topology.from_json(semantic_project.at("topology")) != result::success) {
            return project_result::invalid_project_json;
        }
    }
    catch (...) {
        return project_result::invalid_project_json;
    }

    std::unordered_map<object_id, mutable_object> new_objects;
    if (!build_object_index(new_topology, new_objects)) {
        return project_result::duplicate_object_id;
    }

    topology_ = std::move(new_topology);
    objects_ = std::move(new_objects);
    object_index_dirty_ = false;
    return project_result::success;
}
