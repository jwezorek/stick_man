#include "sm_project.hpp"
#include "json.hpp"
#include "miniz.h"
#include <cstring>
#include <stdexcept>
#include <string_view>
#include <utility>

using json = nlohmann::json;

/*------------------------------------------------------------------------------------------------*/

namespace {
    template<class... Ts> struct overloaded : Ts... { using Ts::operator()...; };

    constexpr std::string_view project_json_name = "project.json";
    constexpr double project_json_version = 2.0;

    sm::object_id object_id_of(const sm::project_object& object) {
        return std::visit(
            [](auto ref) { return ref->id(); },
            object
        );
    }

    bool build_object_index(
            sm::topology& world,
            std::unordered_map<sm::object_id, sm::project_object>& objects) {
        objects.clear();
        auto insert = [&objects](sm::project_object object) {
            return objects.emplace(object_id_of(object), object).second;
        };

        for (auto skel : world.skeletons()) {
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
    if (!build_object_index(world_, objects_)) {
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

sm::topology& sm::project::world() {
    // Core topology is still mutated directly by the editor model. Conservatively
    // invalidate the global object index whenever mutable topology access is granted.
    invalidate_object_index();
    return world_;
}

const sm::topology& sm::project::world() const {
    return world_;
}

sm::project_object sm::project::get(const object_id& id) {
    if (!ensure_object_index()) {
        throw std::runtime_error("project contains duplicate object IDs");
    }
    auto it = objects_.find(id);
    if (it == objects_.end()) {
        throw std::runtime_error("project object ID not found");
    }
    return it->second;
}

sm::const_project_object sm::project::get(const object_id& id) const {
    if (!ensure_object_index()) {
        throw std::runtime_error("project contains duplicate object IDs");
    }
    auto it = objects_.find(id);
    if (it == objects_.end()) {
        throw std::runtime_error("project object ID not found");
    }
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
        it->second
    );
}

bool sm::project::has_unique_object_ids() const {
    return ensure_object_index();
}

void sm::project::clear() {
    world_.clear();
    objects_.clear();
    object_index_dirty_ = false;
}

std::expected<sm::project_buffer, sm::project_result> sm::project::serialize() const {
    if (!ensure_object_index()) {
        return std::unexpected(project_result::duplicate_object_id);
    }

    json semantic_project = {
        {"version", project_json_version},
        {"world", world_.to_json()}
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

    sm::topology new_world;
    try {
        auto semantic_project = json::parse(project_json);
        if (semantic_project.at("version").get<double>() != project_json_version) {
            return project_result::invalid_project_json;
        }
        if (new_world.from_json(semantic_project.at("world")) != result::success) {
            return project_result::invalid_project_json;
        }
    } catch (...) {
        return project_result::invalid_project_json;
    }

    std::unordered_map<object_id, project_object> new_objects;
    if (!build_object_index(new_world, new_objects)) {
        return project_result::duplicate_object_id;
    }

    world_ = std::move(new_world);
    objects_ = std::move(new_objects);
    object_index_dirty_ = false;
    return project_result::success;
}
