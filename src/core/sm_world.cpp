#include "sm_skeleton.h"
#include "sm_world.h"
#include "json.hpp"
#include <ranges>
#include <unordered_set>
#include <functional>
#include <ranges>

using namespace std::placeholders;

namespace r = std::ranges;
namespace rv = std::ranges::views;
using json = nlohmann::json;

/*------------------------------------------------------------------------------------------------*/

namespace {
    bool is_prefix(const std::string& prefix, const std::string& str) {
        auto [lhs, rhs] = r::mismatch(prefix, str);
        return lhs == prefix.end();
    }

    int smallest_excluded_positive_integer(const std::vector<int>& nums) {
        int n = static_cast<int>(nums.size()) + 1;
        std::vector<bool> appears(n, false);
        appears[0] = true;
        for (auto i : nums) {
            if (i < n) {
                appears[i] = true;
            }
        }
        auto first_false = r::find(appears, false);
        if (first_false == appears.end()) {
            return n;
        }
        return std::distance(appears.begin(), first_false);
    }

    // given {foo-3,bar,quux,foo-1,foo-2} return {3, 1, 2}
    std::vector<int> extract_prefixed_integers(
        const std::string& prefix, const std::vector<std::string>& names) {
        auto n = prefix.size();
        return names | rv::filter(
            std::bind(is_prefix, prefix, _1)
        ) | rv::transform(
            [n](const auto& str)->int {
                try {
                    auto num_str = str.substr(n, str.size() - n);
                    return std::stoi(num_str);
                }
                catch (...) {
                    return 0;
                }
            }
        ) | rv::filter(
            [](int v) {return v > 0; }
        ) | r::to< std::vector<int>>();
    }

    std::string unique_name(const std::string& prefix, const std::vector<std::string>& names) {
        std::string prefix_with_hyphen = prefix + "-";
        std::vector<int> ids_taken = extract_prefixed_integers(prefix_with_hyphen, names);
        auto index = smallest_excluded_positive_integer(ids_taken);
        return prefix + "-" + std::to_string(index);
    }
}

sm::world::world() {}

sm::world::world(sm::world&& other) {
    *this = std::move(other);
}

sm::world& sm::world::operator=(world&& other) {
    skeletons_ = std::move(other.skeletons_);
    bones_ = std::move(other.bones_);
    nodes_ = std::move(other.nodes_);

    for (auto& pair : skeletons_) {
        pair.second->set_owner(*this);
    }
    return *this;
}

void sm::world::clear() {
    skeletons_.clear();
    bones_.clear();
    nodes_.clear();
}

bool sm::world::empty() const {
    return skeletons_.empty();
}

sm::skeleton& sm::world::create_skeleton(double x, double y) {
    auto new_name = unique_name("skeleton", skeleton_names());
    skeletons_.emplace(new_name, skeleton::make_unique(*this, new_name, x, y));
    return *skeletons_[new_name];
}

sm::skeleton& sm::world::create_skeleton(const point& pt) {
    return create_skeleton(pt.x, pt.y);
}

sm::expected_skel sm::world::create_skeleton(const std::string& name) {
    if (contains_skeleton(name)) {
        return std::unexpected(result::non_unique_name);
    }
    skeletons_.emplace(name, skeleton::make_unique(*this));
    auto& skel = *skeletons_[name];
    skel.set_name(name);

    return sm::ref(skel);
}

sm::expected_skel sm::world::skeleton(const std::string& name) {
    auto const_this = const_cast<const world*>(this);
    auto skel = const_this->skeleton(name);
    if (!skel) {
        return std::unexpected(skel.error());
    }
    return sm::ref(const_cast<sm::skeleton&>(skel->get()));
}

sm::expected_const_skel sm::world::skeleton(const std::string& name) const {
    auto iter = skeletons_.find(name);
    if (iter == skeletons_.end()) {
        return std::unexpected(sm::result::not_found);
    }
    return *iter->second;
}

template<typename T>
void delete_ptrs_if(std::vector<std::unique_ptr<T>>& vec, std::function<bool(const T&)> predicate) {
    vec.erase(
        std::remove_if(
            vec.begin(), vec.end(),
            [&](const std::unique_ptr<T>& item) {
                return predicate(*item);
            }
        ),
        vec.end()
    );
}

sm::result sm::world::delete_skeleton(const std::string& skel_name) {
    auto skel_ref = this->skeleton(skel_name);
    if (!skel_ref) {
        return sm::result::not_found;
    }
    auto& skel = skel_ref->get();

    auto bone_set = bones_ |
        rv::transform(
            [](const auto& bone_ptr)->bone* {
                return bone_ptr.get();
            }
        ) | rv::filter(
            [&skel](const sm::bone* e)->bool {
                return &e->owner() == &skel;
            }
        ) | r::to<std::unordered_set<const bone*>>();

    auto node_set = nodes_ |
        rv::transform(
            [](const auto& node_ptr)->node* {
                return node_ptr.get();
            }
        ) | rv::filter(
            [&skel](const sm::node* n)->bool {
                return &n->owner() == &skel;
            }
        ) | r::to<std::unordered_set<const node*>>();

    delete_ptrs_if<sm::bone>(bones_,
        [&bone_set](const sm::bone& e)->bool {
            return bone_set.contains(&e);
        }
    );

    delete_ptrs_if<sm::node>(nodes_,
        [&node_set](const sm::node& v)->bool {
            return node_set.contains(&v);
        }
    );

    skeletons_.erase(skel_name);
    return sm::result::success;
}

std::vector<std::string> sm::world::skeleton_names() const {
    return skeletons() |
        rv::transform([](auto skel) {return skel->name(); }) |
        r::to< std::vector<std::string>>();
}

bool sm::world::contains_skeleton(const std::string& name) const {
    return skeletons_.contains(name);
}

sm::result sm::world::set_name(sm::skeleton& skel, const std::string& new_name) {
    if (contains_skeleton(new_name)) {
        return result::non_unique_name;
    }

    auto old_name = skel.name();
    skel.set_name(new_name);
    skeletons_[new_name] = std::move(skeletons_[old_name]);
    skeletons_.erase(old_name);

    return result::success;
}

sm::node_ref sm::world::create_node(sm::skeleton& parent, const std::string& name,
    double x, double y) {
    nodes_.push_back(node::make_unique(parent, name, x, y));
    return *nodes_.back();
}

sm::node_ref sm::world::create_node(sm::skeleton& parent, double x, double y) {
    return create_node(parent, "root", x, y);
}

sm::expected_bone sm::world::create_bone_in_skeleton(
    const std::string& bone_name, node& u, node& v) {
    if (!v.is_root()) {
        return std::unexpected(sm::result::multi_parent_node);
    }
    auto& skel_u = u.owner();
    auto& skel_v = v.owner();
    if (&skel_u != &skel_v) {
        return std::unexpected(sm::result::cross_skeleton_bone);
    }
    bones_.push_back(bone::make_unique(bone_name, u, v));
    return *bones_.back();
}

std::expected<sm::bone_ref, sm::result> sm::world::create_bone(
    const std::string& bone_name, node& u, node& v) {

    if (!v.is_root()) {
        return std::unexpected(sm::result::multi_parent_node);
    }

    auto& skel_u = u.owner();
    auto& skel_v = v.owner();
    if (&skel_u == &skel_v) {
        return std::unexpected(sm::result::cyclic_bones);
    }

    skeletons_.erase(skel_v.name());

    std::string name = (bone_name.empty()) ? "bone-1" : bone_name;
    bones_.push_back(bone::make_unique("bone-1", u, v));
    skel_u.on_new_bone(*bones_.back());

    return *bones_.back();
}

sm::result sm::world::from_json_str(const std::string& str) {
    try {
        auto js = json::parse(str);
        return from_json(js);
    }
    catch (...) {
        return sm::result::invalid_json;
    }
    return sm::result::success;
}

sm::result sm::world::from_json(const json& stick_man)
{
    try {
        skeletons_ = stick_man["skeletons"] |
            rv::transform(
                [this](const json& jobj)->skeleton_tbl::value_type {
                    std::unique_ptr<sm::skeleton> skel = skeleton::make_unique(*this);
                    skel->from_json(*this, jobj);
                    return { skel->name(), std::move(skel) };
                }
            ) | r::to<skeleton_tbl>();
    }
    catch (...) {
        return sm::result::invalid_json;
    }
    return sm::result::success;
}

std::string sm::world::to_json_str() const {
    return to_json().dump(4);
}

json sm::world::to_json() const {
    auto skeleton_json = skeletons_ | rv::transform(
        [](const auto& pair)->const sm::skeleton* {
            return pair.second.get();
        }
    ) | rv::transform(&sm::skeleton::to_json) | r::to<json>();

    json stick_man = {
        {"version", 0.0},
        {"skeletons", skeleton_json}
    };
    return stick_man;
}

void sm::world::apply(matrix& mat) {
    for (auto skel : skeletons()) {
        skel->apply(mat);
    }
}