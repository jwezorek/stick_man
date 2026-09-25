#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <numbers>
#include <numeric>
#include <optional>
#include <set>
#include <span>
#include <stdexcept>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <nlopt.hpp>

#include "sm_fabrik.hpp"
#include "sm_angle_set.hpp"
#include "sm_constraint_geometry.hpp"
#include "sm_geometry_batch.hpp"
#include "sm_skeleton.hpp"

namespace {

constexpr int k_max_iter = 100;
constexpr double k_tolerance = 0.005;
constexpr double k_two_pi = 2.0 * std::numbers::pi;
constexpr double k_angular_feasibility_tolerance = 1e-8;
constexpr double k_position_feasibility_scale = 1e-8;
constexpr double k_pose_angle_weight = 0.02;
constexpr double k_escape_angle = 0.05;
constexpr int k_evaluations_per_iteration = 8;
constexpr int k_max_escape_seeds = 4;
constexpr int k_max_branch_attempts = 4;
constexpr double k_optimizer_ftol_abs = 1e-7;
// Small interactive moves have normalized pose costs well below 1e-4. A 1e-7
// absolute stop leaves visible articulation changes unnecessarily unrefined.
constexpr double k_optimizer_pose_ftol_abs = 1e-9;
constexpr double k_optimizer_angle_xtol = 1e-7;
constexpr double k_pose_score_roundoff = 1e-12;

bool finite(sm::point p) {
    return std::isfinite(p.x) && std::isfinite(p.y);
}

sm::node* mutable_ptr(const sm::node_ref& node) {
    return const_cast<sm::node*>(node.ptr());
}

sm::node* mutable_ptr(const sm::const_node_ref& node) {
    return const_cast<sm::node*>(node.ptr());
}

double normalize_positive(double angle) {
    double result = std::fmod(angle, k_two_pi);
    if (result < 0.0) result += k_two_pi;
    return result == k_two_pi ? 0.0 : result;
}

double lift_near(double angle, double reference) {
    return angle + k_two_pi * std::round((reference - angle) / k_two_pi);
}

struct fabrik_neighborhood {
    sm::node& start_node;
    sm::maybe_bone_ref prev;
    sm::bone& current_bone;
};

sm::node& current_node(const fabrik_neighborhood& neighborhood) {
    if (!neighborhood.prev) return neighborhood.start_node;
    auto shared = neighborhood.current_bone.shared_node(neighborhood.prev->get());
    if (!shared) throw std::runtime_error("invalid fabrik neighborhood");
    return shared->get();
}

struct constraint_failure {
    sm::result code;
};

sm::point apply_all_constraints(
    const sm::point& proposed,
    const fabrik_neighborhood& neighborhood,
    bool use_constraints,
    double max_delta,
    double old_rotation,
    const sm::constraint_geometry& geometry) {

    if (geometry.status() != sm::result::success) throw constraint_failure{geometry.status()};

    auto& leader = current_node(neighborhood);
    const bool forward = &leader == &neighborhood.current_bone.parent_node();
    double theta = forward
        ? sm::angle_from_u_to_v(leader.world_pos(), proposed)
        : sm::angle_from_u_to_v(proposed, leader.world_pos());

    auto allowed = geometry.allowed_angles(neighborhood.current_bone, use_constraints);
    if (std::isfinite(max_delta) && max_delta > 0.0 && max_delta < std::numbers::pi) {
        allowed = allowed.intersect(sm::angle_set({old_rotation - max_delta, 2.0 * max_delta}));
    }
    auto clamped = allowed.closest_angle(theta);
    if (!clamped) throw constraint_failure{sm::result::unsatisfiable_constraints};

    const double direction = *clamped + (forward ? 0.0 : std::numbers::pi);
    const double length = sm::distance(leader.world_pos(), proposed);
    return {
        leader.world_x() + length * std::cos(direction),
        leader.world_y() + length * std::sin(direction)
    };
}

struct circular_set {
    struct interval {
        double low;
        double high;
    };

    std::vector<interval> parts{{0.0, k_two_pi}};

    static std::vector<interval> arc(double start, double span) {
        if (span >= k_two_pi - 1e-14) return {{0.0, k_two_pi}};
        const double s = normalize_positive(start);
        std::vector<interval> out;
        if (span <= 1e-14) {
            out.push_back({s, s});
        } else if (s + span <= k_two_pi) {
            out.push_back({s, s + span});
        } else {
            out.push_back({s, k_two_pi});
            out.push_back({0.0, s + span - k_two_pi});
        }

        bool has_zero = false;
        bool has_tau = false;
        for (const auto& part : out) {
            has_zero |= std::abs(part.low) <= 1e-14 || std::abs(part.high) <= 1e-14;
            has_tau |= std::abs(part.low - k_two_pi) <= 1e-14 || std::abs(part.high - k_two_pi) <= 1e-14;
        }
        if (has_zero && !has_tau) out.push_back({k_two_pi, k_two_pi});
        if (has_tau && !has_zero) out.push_back({0.0, 0.0});
        return out;
    }

    void canonicalize() {
        std::ranges::sort(parts, [](const interval& a, const interval& b) {
            if (a.low != b.low) return a.low < b.low;
            return a.high < b.high;
        });
        std::vector<interval> merged;
        for (auto part : parts) {
            if (merged.empty() || part.low > merged.back().high + 1e-13) {
                merged.push_back(part);
            } else {
                merged.back().high = std::max(merged.back().high, part.high);
            }
        }
        parts = std::move(merged);
    }

    void intersect(double start, double span) {
        const auto rhs = arc(start, span);
        std::vector<interval> result;
        for (auto left : parts) {
            for (auto right : rhs) {
                const double low = std::max(left.low, right.low);
                const double high = std::min(left.high, right.high);
                if (low <= high + 1e-13) result.push_back({low, std::max(low, high)});
            }
        }
        parts = std::move(result);
        canonicalize();
    }

    bool empty() const { return parts.empty(); }

    bool full() const {
        return parts.size() == 1 && parts.front().low <= 1e-13
            && parts.front().high >= k_two_pi - 1e-13;
    }

    std::vector<interval> circular_components() const {
        if (parts.empty()) return {};
        if (full()) return {{0.0, k_two_pi}};

        std::vector<interval> result;
        std::size_t begin = 0;
        std::size_t end = parts.size();
        const bool seam = parts.size() >= 2
            && parts.front().low <= 1e-13
            && parts.back().high >= k_two_pi - 1e-13;
        if (seam) {
            result.push_back({parts.back().low, k_two_pi + parts.front().high});
            begin = 1;
            end = parts.size() - 1;
        }
        for (std::size_t i = begin; i < end; ++i) result.push_back(parts[i]);
        return result;
    }
};

struct linear_key {
    std::vector<std::pair<std::size_t, int>> terms;
    auto operator<=>(const linear_key&) const = default;
};

double linear_value(const linear_key& key, std::span<const double> x) {
    double value = 0.0;
    for (auto [index, coefficient] : key.terms) value += coefficient * x[index];
    return value;
}

struct lifted_interval {
    double low;
    double high;
    double distance;
};

std::vector<lifted_interval> lifted_choices(const circular_set& set, double incoming) {
    if (set.full()) return {{-std::numeric_limits<double>::infinity(),
                            std::numeric_limits<double>::infinity(), 0.0}};

    std::vector<lifted_interval> choices;
    for (auto component : set.circular_components()) {
        const double center = 0.5 * (component.low + component.high);
        const long nearest = std::lround((incoming - center) / k_two_pi);
        for (long shift = nearest - 1; shift <= nearest + 1; ++shift) {
            const double low = component.low + shift * k_two_pi;
            const double high = component.high + shift * k_two_pi;
            double distance = 0.0;
            if (incoming < low) distance = low - incoming;
            else if (incoming > high) distance = incoming - high;
            choices.push_back({low, high, distance});
        }
    }
    std::ranges::sort(choices, [](const lifted_interval& a, const lifted_interval& b) {
        if (std::abs(a.distance - b.distance) > 1e-13) return a.distance < b.distance;
        if (a.low != b.low) return a.low < b.low;
        return a.high < b.high;
    });
    choices.erase(std::unique(choices.begin(), choices.end(), [](const auto& a, const auto& b) {
        return std::abs(a.low - b.low) <= 1e-12 && std::abs(a.high - b.high) <= 1e-12;
    }), choices.end());
    if (choices.size() > 4) choices.resize(4);
    return choices;
}

struct region {
    std::unordered_set<sm::node*> nodes;
    std::unordered_set<sm::bone*> bones;
    std::vector<sm::node_ref> pins;
    std::vector<std::tuple<sm::node_ref, sm::point>> effectors;
};

struct bone_coordinate {
    std::size_t variable = 0;
    double offset = 0.0;
    double incoming_rotation = 0.0;
};

struct angle_variable {
    std::vector<sm::bone*> members;
    std::vector<double> offsets;
    double incoming = 0.0;
};

struct kinematic_step {
    std::size_t parent_node = 0;
    std::size_t child_node = 0;
    std::size_t variable = 0;
    double offset_cos = 1.0;
    double offset_sin = 0.0;
    double signed_length = 0.0;
};

enum class translation_mode {
    pin_anchor,
    effector_anchor,
    free_translation
};

struct kinematic_model {
    const region* source = nullptr;
    const sm::topology* topology = nullptr;
    std::vector<sm::bone*> bones;
    std::vector<sm::node*> nodes;
    std::unordered_map<sm::bone*, bone_coordinate> bone_coordinates;
    std::unordered_map<sm::node*, std::size_t> node_indices;
    std::vector<angle_variable> angle_variables;
    std::vector<std::pair<std::size_t, std::size_t>> articulations;
    std::vector<kinematic_step> steps;
    std::vector<sm::point> incoming_positions;
    std::vector<double> lower_bounds;
    std::vector<double> upper_bounds;
    translation_mode mode = translation_mode::free_translation;
    std::size_t anchor_node = 0;
    sm::point anchor_position{};
    double characteristic_length = 1.0;

    std::size_t angle_count() const { return angle_variables.size(); }
    std::size_t variable_count() const {
        return angle_count() + (mode == translation_mode::free_translation ? 2u : 0u);
    }
};

bool bone_id_less(const sm::bone* a, const sm::bone* b) {
    return a->id() < b->id();
}

bool node_id_less(const sm::node* a, const sm::node* b) {
    return a->id() < b->id();
}

sm::result build_angle_variables(kinematic_model& model) {
    std::unordered_set<sm::bone*> active(model.bones.begin(), model.bones.end());
    std::unordered_map<sm::bone*, std::vector<std::pair<sm::bone*, double>>> graph;

    for (const auto& [id, constraint] : model.topology->constraints()) {
        const auto* triangle = constraint.triangle();
        if (!triangle) continue;
        auto first_ref = model.topology->get<sm::bone>(triangle->first_bone);
        auto second_ref = model.topology->get<sm::bone>(triangle->second_bone);
        if (!first_ref || !second_ref) return sm::result::invalid_constraint;
        auto* first = first_ref->ptr();
        auto* second = second_ref->ptr();
        const bool first_active = active.contains(first);
        const bool second_active = active.contains(second);
        if (first_active != second_active) {
            // A fan is an indivisible rigid angular group. Region discovery should have
            // brought all sibling members on the movable side of a pin into the region.
            return sm::result::unsatisfiable_constraints;
        }
        if (!first_active) continue;
        graph[first].push_back({second, triangle->relative_angle});
        graph[second].push_back({first, -triangle->relative_angle});
    }

    for (auto& [bone, edges] : graph) {
        std::ranges::sort(edges, [](const auto& a, const auto& b) {
            return a.first->id() < b.first->id();
        });
    }

    std::unordered_set<sm::bone*> assigned;
    for (auto* seed : model.bones) {
        if (assigned.contains(seed)) continue;

        std::vector<sm::bone*> component;
        if (!graph.contains(seed)) {
            component.push_back(seed);
        } else {
            std::vector<sm::bone*> pending{seed};
            std::unordered_set<sm::bone*> seen;
            while (!pending.empty()) {
                auto* current = pending.back();
                pending.pop_back();
                if (!seen.insert(current).second) continue;
                component.push_back(current);
                for (auto [other, delta] : graph[current]) pending.push_back(other);
            }
            std::ranges::sort(component, bone_id_less);
        }

        auto* root = *std::ranges::min_element(component, bone_id_less);
        std::unordered_map<sm::bone*, double> offsets;
        offsets[root] = 0.0;
        std::vector<sm::bone*> pending{root};
        while (!pending.empty()) {
            auto* current = pending.back();
            pending.pop_back();
            if (!graph.contains(current)) continue;
            for (auto [other, delta] : graph[current]) {
                const double candidate = sm::normalize_angle(offsets[current] + delta);
                auto it = offsets.find(other);
                if (it == offsets.end()) {
                    offsets.emplace(other, candidate);
                    pending.push_back(other);
                } else if (std::abs(sm::angular_distance(it->second, candidate))
                           > k_angular_feasibility_tolerance) {
                    return sm::result::inconsistent_constraints;
                }
            }
        }

        angle_variable variable;
        variable.incoming = root->world_rotation();
        const std::size_t variable_index = model.angle_variables.size();
        for (auto* member : component) {
            const double offset = offsets.contains(member) ? offsets.at(member) : 0.0;
            variable.members.push_back(member);
            variable.offsets.push_back(offset);
            model.bone_coordinates[member] = {
                variable_index,
                offset,
                member->world_rotation()
            };
            assigned.insert(member);
        }
        model.angle_variables.push_back(std::move(variable));
    }

    return sm::result::success;
}

sm::result build_kinematics(kinematic_model& model) {
    std::unordered_set<sm::node*> node_set;
    double total_length = 0.0;
    for (auto* bone : model.bones) {
        node_set.insert(&bone->parent_node());
        node_set.insert(&bone->child_node());
        total_length += bone->scaled_length();
    }
    for (const auto& [node, target] : model.source->effectors) node_set.insert(mutable_ptr(node));
    for (auto pin : model.source->pins) node_set.insert(mutable_ptr(pin));
    model.nodes.assign(node_set.begin(), node_set.end());
    std::ranges::sort(model.nodes, node_id_less);
    for (std::size_t i = 0; i < model.nodes.size(); ++i) {
        model.node_indices[model.nodes[i]] = i;
        model.incoming_positions.push_back(model.nodes[i]->world_pos());
    }
    model.characteristic_length = model.bones.empty()
        ? 1.0
        : std::max(total_length / static_cast<double>(model.bones.size()), 1e-6);

    if (model.nodes.empty()) return sm::result::success;

    std::unordered_set<sm::bone*> active(model.bones.begin(), model.bones.end());
    std::vector<bool> visited(model.nodes.size(), false);
    std::vector<std::size_t> pending{0};
    visited[0] = true;

    while (!pending.empty()) {
        const auto parent_index = pending.back();
        pending.pop_back();
        auto* parent = model.nodes[parent_index];
        auto adjacent = parent->adjacent_bones();
        std::ranges::sort(adjacent, [](const sm::bone_ref& a, const sm::bone_ref& b) {
            return a->id() < b->id();
        });
        for (auto bone_ref : adjacent) {
            auto* bone = bone_ref.ptr();
            if (!active.contains(bone)) continue;
            auto* child = &bone->opposite_node(*parent);
            const auto child_index = model.node_indices.at(child);
            if (visited[child_index]) continue;
            visited[child_index] = true;
            pending.push_back(child_index);
            const auto coord = model.bone_coordinates.at(bone);
            const bool forward = parent == &bone->parent_node();
            model.steps.push_back({
                parent_index,
                child_index,
                coord.variable,
                std::cos(coord.offset),
                std::sin(coord.offset),
                (forward ? 1.0 : -1.0) * bone->scaled_length()
            });
        }
    }

    if (std::ranges::find(visited, false) != visited.end()) {
        return sm::result::fabrik_no_solution_found;
    }
    return sm::result::success;
}

struct evaluation {
    std::vector<sm::point> angle_directions;
    std::vector<sm::point> local;
    std::vector<sm::point> local_jacobian;
    std::vector<sm::point> world;
    std::vector<sm::point> world_jacobian;
};

void evaluate_model(const kinematic_model& model, std::span<const double> x, evaluation& out) {
    const std::size_t node_count = model.nodes.size();
    const std::size_t angle_count = model.angle_count();
    const std::size_t variable_count = model.variable_count();

    for (std::size_t j = 0; j < angle_count; ++j) {
        out.angle_directions[j] = {std::cos(x[j]), std::sin(x[j])};
    }
    // Scratch storage is sized once per solve. The root is the only local row
    // that the traversal does not overwrite from its parent on every evaluation.
    if (node_count != 0) out.local[0] = {0.0, 0.0};
    std::fill_n(out.local_jacobian.begin(), angle_count, sm::point{0.0, 0.0});

    for (const auto& step : model.steps) {
        const auto direction = out.angle_directions[step.variable];
        const sm::point edge{
            step.signed_length * (direction.x * step.offset_cos - direction.y * step.offset_sin),
            step.signed_length * (direction.y * step.offset_cos + direction.x * step.offset_sin)
        };
        out.local[step.child_node] = out.local[step.parent_node] + edge;

        for (std::size_t j = 0; j < angle_count; ++j) {
            out.local_jacobian[step.child_node * angle_count + j]
                = out.local_jacobian[step.parent_node * angle_count + j];
        }
        auto& derivative = out.local_jacobian[step.child_node * angle_count + step.variable];
        derivative += sm::point{-edge.y, edge.x};
    }

    sm::point translation{};
    if (model.mode == translation_mode::free_translation) {
        translation = {x[angle_count], x[angle_count + 1]};
    } else {
        translation = model.anchor_position - out.local[model.anchor_node];
    }

    for (std::size_t i = 0; i < node_count; ++i) {
        out.world[i] = translation + out.local[i];
        for (std::size_t j = 0; j < angle_count; ++j) {
            auto derivative = out.local_jacobian[i * angle_count + j];
            if (model.mode != translation_mode::free_translation) {
                derivative -= out.local_jacobian[model.anchor_node * angle_count + j];
            }
            out.world_jacobian[i * variable_count + j] = derivative;
        }
        if (model.mode == translation_mode::free_translation) {
            out.world_jacobian[i * variable_count + angle_count] = {1.0, 0.0};
            out.world_jacobian[i * variable_count + angle_count + 1] = {0.0, 1.0};
        }
    }
}

struct target_entry {
    std::size_t node_index = 0;
    sm::point target{};
};

struct pin_entry {
    std::size_t node_index = 0;
    sm::point position{};
    bool implied_by_bounds = false;
};

struct angular_group {
    linear_key key;
    circular_set allowed;
    double incoming = 0.0;
    std::vector<lifted_interval> choices;
};

struct solve_context {
    const kinematic_model* model = nullptr;
    std::vector<target_entry> targets;
    std::vector<pin_entry> pins;
    std::vector<double> incoming_x;
    std::vector<double> last_x;
    evaluation state;
    bool cached = false;
    double target_tolerance = k_tolerance;

    void ensure(const std::vector<double>& x) {
        if (cached && x == last_x) return;
        evaluate_model(*model, x, state);
        last_x = x;
        cached = true;
    }
};

struct affine_constraint_data {
    linear_key key;
    double bound = 0.0;
    double sign = 1.0;
};

double affine_constraint_callback(
    unsigned dimension,
    const double* x,
    double* gradient,
    void* opaque) {
    const auto& data = *static_cast<affine_constraint_data*>(opaque);
    if (gradient) {
        std::fill_n(gradient, dimension, 0.0);
        for (auto [index, coefficient] : data.key.terms) {
            gradient[index] = data.sign * coefficient;
        }
    }
    return data.sign * (linear_value(data.key, std::span<const double>{x, dimension}) - data.bound);
}

struct pin_constraint_data {
    solve_context* context = nullptr;
    std::size_t pin_index = 0;
    bool x_axis = true;
};

double pin_constraint_callback(
    const std::vector<double>& x,
    std::vector<double>& gradient,
    void* opaque) {
    auto& data = *static_cast<pin_constraint_data*>(opaque);
    data.context->ensure(x);
    const auto& pin = data.context->pins[data.pin_index];
    const auto& model = *data.context->model;
    const auto& state = data.context->state;
    if (!gradient.empty()) {
        for (std::size_t j = 0; j < model.variable_count(); ++j) {
            const auto derivative = state.world_jacobian[pin.node_index * model.variable_count() + j];
            gradient[j] = data.x_axis ? derivative.x : derivative.y;
        }
    }
    const auto p = state.world[pin.node_index];
    return data.x_axis ? p.x - pin.position.x : p.y - pin.position.y;
}

struct target_cap_data {
    solve_context* context = nullptr;
    std::size_t target_index = 0;
    double cap_squared = 0.0;
    double scale = 1.0;
};

double target_cap_callback(
    const std::vector<double>& x,
    std::vector<double>& gradient,
    void* opaque) {
    auto& data = *static_cast<target_cap_data*>(opaque);
    data.context->ensure(x);
    const auto& target = data.context->targets[data.target_index];
    const auto& model = *data.context->model;
    const auto& state = data.context->state;
    const sm::point delta = state.world[target.node_index] - target.target;
    if (!gradient.empty()) {
        for (std::size_t j = 0; j < model.variable_count(); ++j) {
            const auto derivative = state.world_jacobian[target.node_index * model.variable_count() + j];
            gradient[j] = 2.0 * (delta.x * derivative.x + delta.y * derivative.y) / data.scale;
        }
    }
    return (delta.x * delta.x + delta.y * delta.y - data.cap_squared) / data.scale;
}

double target_objective(
    const std::vector<double>& x,
    std::vector<double>& gradient,
    void* opaque) {
    auto& context = *static_cast<solve_context*>(opaque);
    context.ensure(x);
    const auto& model = *context.model;
    const double scale2 = model.characteristic_length * model.characteristic_length;
    const double denominator = scale2 * std::max<std::size_t>(context.targets.size(), 1);
    if (!gradient.empty()) std::fill(gradient.begin(), gradient.end(), 0.0);

    double objective = 0.0;
    for (const auto& target : context.targets) {
        const auto delta = context.state.world[target.node_index] - target.target;
        objective += (delta.x * delta.x + delta.y * delta.y) / denominator;
        if (!gradient.empty()) {
            for (std::size_t j = 0; j < model.variable_count(); ++j) {
                const auto derivative = context.state.world_jacobian[target.node_index * model.variable_count() + j];
                gradient[j] += 2.0 * (delta.x * derivative.x + delta.y * derivative.y) / denominator;
            }
        }
    }
    return objective;
}

double pose_objective(
    const std::vector<double>& x,
    std::vector<double>& gradient,
    void* opaque) {
    auto& context = *static_cast<solve_context*>(opaque);
    context.ensure(x);
    const auto& model = *context.model;
    const double scale2 = model.characteristic_length * model.characteristic_length;
    const double node_denominator = scale2 * std::max<std::size_t>(model.nodes.size(), 1);
    const double angle_denominator = std::numbers::pi * std::numbers::pi
        * std::max<std::size_t>(model.articulations.size(), 1);
    if (!gradient.empty()) std::fill(gradient.begin(), gradient.end(), 0.0);

    double objective = 0.0;
    for (std::size_t i = 0; i < model.nodes.size(); ++i) {
        const auto delta = context.state.world[i] - model.incoming_positions[i];
        objective += (delta.x * delta.x + delta.y * delta.y) / node_denominator;
        if (!gradient.empty()) {
            for (std::size_t j = 0; j < model.variable_count(); ++j) {
                const auto derivative = context.state.world_jacobian[i * model.variable_count() + j];
                gradient[j] += 2.0 * (delta.x * derivative.x + delta.y * derivative.y) / node_denominator;
            }
        }
    }

    // Penalize articulation changes, not a common rigid rotation. The chord
    // metric is smooth through circular seams and invariant under 2*pi lifts.
    // A rigid fan contributes one orientation, regardless of its member count.
    for (auto [a, b] : model.articulations) {
        const double delta = (x[a] - context.incoming_x[a])
                           - (x[b] - context.incoming_x[b]);
        objective += 2.0 * k_pose_angle_weight * (1.0 - std::cos(delta)) / angle_denominator;
        if (!gradient.empty()) {
            const double derivative = 2.0 * k_pose_angle_weight * std::sin(delta) / angle_denominator;
            gradient[a] += derivative;
            gradient[b] -= derivative;
        }
    }
    return objective;
}

struct candidate {
    std::vector<double> x;
    std::vector<sm::point> positions;
    std::vector<double> target_errors;
    double target_score = std::numeric_limits<double>::infinity();
    double pose_score = std::numeric_limits<double>::infinity();
};

bool angle_satisfies(sm::angle_range range, double value) {
    sm::angle_set allowed(range);
    if (allowed.contains(value)) return true;
    auto closest = allowed.closest_angle(value);
    return closest && std::abs(sm::angular_distance(value, *closest)) <= k_angular_feasibility_tolerance;
}

std::optional<candidate> validate_candidate(
    solve_context& context,
    const std::vector<double>& x,
    const sm::fabrik_options& opts) {

    const auto& model = *context.model;
    if (x.size() != model.variable_count()) return std::nullopt;
    if (!std::ranges::all_of(x, [](double value) { return std::isfinite(value); })) return std::nullopt;

    context.ensure(x);
    const auto& state = context.state;
    const double position_tolerance = k_position_feasibility_scale;

    for (const auto& p : state.world) if (!finite(p)) return std::nullopt;

    for (const auto& pin : context.pins) {
        if (sm::distance(state.world[pin.node_index], pin.position) > position_tolerance) return std::nullopt;
    }

    for (auto* bone : model.bones) {
        const auto u = model.node_indices.at(&bone->parent_node());
        const auto v = model.node_indices.at(&bone->child_node());
        const double roundoff = 16.0 * std::numeric_limits<double>::epsilon()
            * std::max({bone->scaled_length(), std::abs(state.world[u].x), std::abs(state.world[u].y),
                        std::abs(state.world[v].x), std::abs(state.world[v].y)});
        if (std::abs(sm::distance(state.world[u], state.world[v]) - bone->scaled_length())
            > std::max(position_tolerance, roundoff)) return std::nullopt;
    }

    const bool max_delta_enabled = std::isfinite(opts.max_ang_delta)
        && opts.max_ang_delta > 0.0 && opts.max_ang_delta < std::numbers::pi;
    if (max_delta_enabled) {
        for (auto* bone : model.bones) {
            const auto coord = model.bone_coordinates.at(bone);
            const double theta = x[coord.variable] + coord.offset;
            if (std::abs(sm::angular_distance(coord.incoming_rotation, theta))
                > opts.max_ang_delta + k_angular_feasibility_tolerance) return std::nullopt;
        }
    }

    auto candidate_rotation = [&](sm::bone* bone) {
        auto it = model.bone_coordinates.find(bone);
        return it == model.bone_coordinates.end()
            ? bone->world_rotation()
            : x[it->second.variable] + it->second.offset;
    };

    std::unordered_set<sm::bone*> active(model.bones.begin(), model.bones.end());
    for (const auto& [id, constraint] : model.topology->constraints()) {
        const auto* rotation = constraint.rotation();
        if (!rotation) continue;
        auto target_ref = model.topology->get<sm::bone>(rotation->target_bone);
        if (!target_ref) return std::nullopt;
        auto* target = target_ref->ptr();
        sm::bone* reference = nullptr;
        if (rotation->reference.kind == sm::rotation_reference_kind::parent) {
            auto parent = target->parent_bone();
            if (!parent) return std::nullopt;
            reference = parent->ptr();
        } else if (rotation->reference.kind == sm::rotation_reference_kind::bone) {
            auto ref = model.topology->get<sm::bone>(rotation->reference.bone_id);
            if (!ref) return std::nullopt;
            reference = ref->ptr();
        }
        if (!active.contains(target) && (!reference || !active.contains(reference))) continue;
        const double relative = candidate_rotation(target)
            - (reference ? candidate_rotation(reference) : 0.0);
        if (!angle_satisfies(rotation->allowed, relative)) return std::nullopt;
    }

    candidate result;
    result.x = x;
    result.positions = state.world;
    result.target_errors.reserve(context.targets.size());
    result.target_score = 0.0;
    for (const auto& target : context.targets) {
        const double error = sm::distance(state.world[target.node_index], target.target);
        result.target_errors.push_back(error);
        result.target_score += error * error;
    }
    result.target_score /= std::max<std::size_t>(context.targets.size(), 1);

    std::vector<double> scratch_gradient;
    result.pose_score = pose_objective(result.x, scratch_gradient, &context);
    return result;
}

bool candidate_better(const candidate& lhs, const candidate& rhs, double tolerance) {
    const auto lhs_max = lhs.target_errors.empty() ? 0.0
        : *std::ranges::max_element(lhs.target_errors);
    const auto rhs_max = rhs.target_errors.empty() ? 0.0
        : *std::ranges::max_element(rhs.target_errors);
    // Once every effector is reached, sub-tolerance residuals are not a reason
    // to change IK branch. Every seed uses the same incoming-pose reference.
    const bool lhs_reached = lhs_max <= tolerance;
    const bool rhs_reached = rhs_max <= tolerance;
    if (lhs_reached != rhs_reached) return lhs_reached;
    if (lhs_reached) {
        if (lhs.pose_score != rhs.pose_score) return lhs.pose_score < rhs.pose_score;
        return lhs.target_score < rhs.target_score;
    }
    const double target_slop = std::max(tolerance * 0.05, 1e-9);
    if (lhs_max + target_slop < rhs_max) return true;
    if (rhs_max + target_slop < lhs_max) return false;
    // Compare RMS distances, not squared errors against a squared distance
    // band: the latter makes the effective band shrink as residuals grow.
    if (std::sqrt(lhs.target_score) + target_slop < std::sqrt(rhs.target_score)) return true;
    if (std::sqrt(rhs.target_score) + target_slop < std::sqrt(lhs.target_score)) return false;
    return lhs.pose_score < rhs.pose_score;
}

std::optional<candidate> validate_branch_candidate(
    solve_context& context, const std::vector<double>& x, const sm::fabrik_options& opts,
    const std::vector<angular_group>& groups, const std::vector<std::size_t>& branch) {
    // An interrupted optimizer may satisfy circular constraints in another lift.
    // A candidate must also belong to the chart this attempt was solving.
    for (std::size_t i = 0; i < groups.size(); ++i) {
        const auto& interval = groups[i].choices[branch[i]];
        const double angle = linear_value(groups[i].key, x);
        if (angle < interval.low - k_angular_feasibility_tolerance
            || angle > interval.high + k_angular_feasibility_tolerance) return std::nullopt;
    }
    return validate_candidate(context, x, opts);
}

sm::result build_angular_groups(
    const kinematic_model& model,
    std::span<const double> incoming_x,
    std::vector<angular_group>& groups) {

    struct accumulation {
        circular_set allowed;
        bool initialized = false;
    };
    std::map<linear_key, accumulation> accumulated;

    auto add_term = [](std::map<std::size_t, int>& terms, std::size_t index, int coefficient) {
        terms[index] += coefficient;
        if (terms[index] == 0) terms.erase(index);
    };

    auto coordinate = [&](sm::bone* bone, int coefficient,
                          std::map<std::size_t, int>& terms, double& constant,
                          bool& active) {
        auto it = model.bone_coordinates.find(bone);
        if (it == model.bone_coordinates.end()) {
            constant += coefficient * bone->world_rotation();
            return;
        }
        active = true;
        add_term(terms, it->second.variable, coefficient);
        constant += coefficient * it->second.offset;
    };

    for (const auto& [id, constraint] : model.topology->constraints()) {
        const auto* rotation = constraint.rotation();
        if (!rotation) continue;
        auto target_ref = model.topology->get<sm::bone>(rotation->target_bone);
        if (!target_ref) return sm::result::invalid_constraint;
        auto* target = target_ref->ptr();
        sm::bone* reference = nullptr;
        if (rotation->reference.kind == sm::rotation_reference_kind::parent) {
            auto parent = target->parent_bone();
            if (!parent) return sm::result::invalid_constraint;
            reference = parent->ptr();
        } else if (rotation->reference.kind == sm::rotation_reference_kind::bone) {
            auto ref = model.topology->get<sm::bone>(rotation->reference.bone_id);
            if (!ref) return sm::result::invalid_constraint;
            reference = ref->ptr();
        }

        bool involves_active = false;
        double constant = 0.0;
        std::map<std::size_t, int> term_map;
        coordinate(target, +1, term_map, constant, involves_active);
        if (reference) coordinate(reference, -1, term_map, constant, involves_active);
        if (!involves_active) continue;

        if (term_map.empty()) {
            if (!angle_satisfies(rotation->allowed, constant)) {
                return sm::result::unsatisfiable_constraints;
            }
            continue;
        }

        linear_key key;
        for (auto [index, coefficient] : term_map) key.terms.push_back({index, coefficient});
        auto& entry = accumulated[key];
        if (!entry.initialized) {
            entry.allowed = circular_set{};
            entry.initialized = true;
        }
        entry.allowed.intersect(rotation->allowed.start_angle - constant, rotation->allowed.span_angle);
        if (entry.allowed.empty()) return sm::result::unsatisfiable_constraints;
    }

    for (auto& [key, entry] : accumulated) {
        if (entry.allowed.full()) continue;
        angular_group group;
        group.key = key;
        group.allowed = entry.allowed;
        group.incoming = linear_value(key, incoming_x);
        group.choices = lifted_choices(group.allowed, group.incoming);
        if (group.choices.empty()) return sm::result::unsatisfiable_constraints;
        groups.push_back(std::move(group));
    }
    return sm::result::success;
}

struct optimizer_constraints {
    std::vector<affine_constraint_data> affine;
    std::vector<pin_constraint_data> pins;
    std::vector<target_cap_data> target_caps;
};

void add_hard_constraints(
    nlopt::opt& optimizer,
    solve_context& context,
    const std::vector<angular_group>& angular_groups,
    const std::vector<std::size_t>& branch_selection,
    optimizer_constraints& storage) {

    storage.affine.clear();
    storage.pins.clear();

    for (std::size_t i = 0; i < angular_groups.size(); ++i) {
        const auto& group = angular_groups[i];
        const auto& interval = group.choices[branch_selection[i]];
        if (!std::isfinite(interval.low) || !std::isfinite(interval.high)) continue;
        if (std::abs(interval.high - interval.low) <= k_angular_feasibility_tolerance) {
            storage.affine.push_back({group.key, 0.5 * (interval.low + interval.high), 1.0});
            optimizer.add_equality_constraint(
                affine_constraint_callback,
                &storage.affine.back(),
                k_angular_feasibility_tolerance);
        } else {
            storage.affine.push_back({group.key, interval.low, -1.0});
            optimizer.add_inequality_constraint(
                affine_constraint_callback,
                &storage.affine.back(),
                k_angular_feasibility_tolerance);
            storage.affine.push_back({group.key, interval.high, 1.0});
            optimizer.add_inequality_constraint(
                affine_constraint_callback,
                &storage.affine.back(),
                k_angular_feasibility_tolerance);
        }
    }

    // The first pin is the translation anchor and is therefore satisfied by construction.
    // Remaining pins use coordinate equalities; no squared-distance equality is introduced.
    const std::size_t first_explicit_pin = context.model->mode == translation_mode::pin_anchor ? 1u : 0u;
    for (std::size_t i = first_explicit_pin; i < context.pins.size(); ++i) {
        if (context.pins[i].implied_by_bounds) continue;
        storage.pins.push_back({&context, i, true});
        optimizer.add_equality_constraint(
            pin_constraint_callback,
            &storage.pins.back(),
            k_position_feasibility_scale * 0.1);
        storage.pins.push_back({&context, i, false});
        optimizer.add_equality_constraint(
            pin_constraint_callback,
            &storage.pins.back(),
            k_position_feasibility_scale * 0.1);
    }
}

void add_target_caps(
    nlopt::opt& optimizer,
    solve_context& context,
    const std::vector<double>& caps,
    optimizer_constraints& storage) {
    storage.target_caps.clear();
    for (std::size_t i = 0; i < caps.size(); ++i) {
        const double scale = std::max(2.0 * caps[i] * context.model->characteristic_length, 1e-12);
        storage.target_caps.push_back({&context, i, caps[i] * caps[i], scale});
        optimizer.add_inequality_constraint(
            target_cap_callback,
            &storage.target_caps.back(),
            std::max(1e-12, caps[i] * caps[i] * 1e-8) / scale);
    }
}

struct prepared_optimizer {
    // NLopt callbacks retain pointers into storage and to their owning opt.
    // Keep both at stable addresses and destroy opt before its callback data.
    optimizer_constraints storage;
    std::optional<nlopt::opt> opt;

    prepared_optimizer() = default;
    prepared_optimizer(const prepared_optimizer&) = delete;
    prepared_optimizer& operator=(const prepared_optimizer&) = delete;
    prepared_optimizer(prepared_optimizer&&) = delete;
    prepared_optimizer& operator=(prepared_optimizer&&) = delete;

    void prepare(
        solve_context& context,
        const std::vector<angular_group>& angular_groups,
        const std::vector<std::size_t>& branch_selection,
        bool pose_stage,
        const std::vector<double>& target_caps) {
        if (opt) return;

        storage.affine.reserve(angular_groups.size() * 2);
        storage.pins.reserve(context.pins.size() * 2);
        storage.target_caps.reserve(target_caps.size());
        opt.emplace(nlopt::LD_SLSQP, static_cast<unsigned>(context.model->variable_count()));
        try {
            opt->set_lower_bounds(context.model->lower_bounds);
            opt->set_upper_bounds(context.model->upper_bounds);
            const double normalized_tolerance = context.target_tolerance / context.model->characteristic_length;
            opt->set_ftol_abs(pose_stage ? k_optimizer_pose_ftol_abs
                : std::min(k_optimizer_ftol_abs, normalized_tolerance * normalized_tolerance * 0.01));
            // Absolute angle steps avoid dependence on angular lifts. Translation
            // steps use positional units, never the absolute world-space origin.
            std::vector<double> step_tolerances(context.model->variable_count(), k_optimizer_angle_xtol);
            for (std::size_t i = context.model->angle_count(); i < step_tolerances.size(); ++i) {
                step_tolerances[i] = context.target_tolerance * 0.01;
            }
            opt->set_xtol_abs(step_tolerances);
            add_hard_constraints(*opt, context, angular_groups, branch_selection, storage);
            if (!target_caps.empty()) add_target_caps(*opt, context, target_caps, storage);
            if (pose_stage) opt->set_min_objective(pose_objective, &context);
            else opt->set_min_objective(target_objective, &context);
        } catch (...) {
            opt.reset();
            storage.affine.clear();
            storage.pins.clear();
            storage.target_caps.clear();
            throw;
        }
    }
};

bool run_optimizer(
    solve_context& context,
    const std::vector<angular_group>& angular_groups,
    const std::vector<std::size_t>& branch_selection,
    std::vector<double>& x,
    int& remaining_budget,
    int requested_budget,
    bool pose_stage,
    const std::vector<double>& target_caps = {},
    prepared_optimizer* reusable = nullptr) {

    if (x.empty() || remaining_budget <= 0) return false;
    if (context.model->lower_bounds == context.model->upper_bounds) return true;

    const int budget = std::max(1, std::min(requested_budget, remaining_budget));
    prepared_optimizer local;
    auto& prepared = reusable ? *reusable : local;
    bool ran = false;
    bool optimize_started = false;
    try {
        prepared.prepare(context, angular_groups, branch_selection, pose_stage, target_caps);
        auto& optimizer = *prepared.opt;
        optimizer.set_maxeval(budget);
        double minimum = 0.0;
        optimize_started = true;
        optimizer.optimize(x, minimum);
        ran = true;
    } catch (const nlopt::roundoff_limited&) {
        ran = true;
    } catch (const nlopt::forced_stop&) {
        ran = true;
    } catch (const std::exception&) {
        ran = false;
    }
    if (optimize_started) {
        auto& optimizer = *prepared.opt;
        remaining_budget -= std::min(budget, std::max(optimizer.get_numevals(), 0));
    }
    return ran;
}

std::vector<std::vector<std::size_t>> make_branch_selections(
    const std::vector<angular_group>& groups) {
    std::vector<std::vector<std::size_t>> result;
    std::vector<std::size_t> preferred(groups.size(), 0);
    result.push_back(preferred);
    for (std::size_t i = 0; i < groups.size() && result.size() < k_max_branch_attempts; ++i) {
        if (groups[i].choices.size() <= 1) continue;
        auto alternative = preferred;
        alternative[i] = 1;
        result.push_back(std::move(alternative));
    }
    return result;
}

std::vector<std::vector<double>> make_seeds(const kinematic_model& model, const std::vector<double>& base) {
    std::vector<std::vector<double>> seeds{base};
    for (std::size_t i = 0; i < model.angle_count() && seeds.size() < k_max_escape_seeds; ++i) {
        for (double sign : {1.0, -1.0}) {
            if (seeds.size() >= k_max_escape_seeds) break;
            auto seed = base;
            seed[i] = std::clamp(
                seed[i] + sign * k_escape_angle,
                model.lower_bounds[i],
                model.upper_bounds[i]);
            if (seed != base) seeds.push_back(std::move(seed));
        }
    }
    return seeds;
}

double chart_pose_lower_bound(
    const kinematic_model& model,
    const std::vector<angular_group>& groups,
    const std::vector<std::size_t>& branch) {
    double bound = 0.0;
    for (std::size_t i = 0; i < groups.size(); ++i) {
        double incoming = 0.0, coefficients = 0.0;
        for (auto [index, coefficient] : groups[i].key.terms) {
            incoming += coefficient * model.angle_variables[index].incoming;
            coefficients += std::abs(coefficient);
        }
        if (coefficients == 0.0) continue;
        const auto& interval = groups[i].choices[branch[i]];
        const double distance = std::max({0.0, interval.low - incoming, incoming - interval.high});
        const double angle = std::min(std::numbers::pi,
            std::max(0.0, distance - k_angular_feasibility_tolerance) / coefficients);
        if (angle == 0.0) continue;

        // At least one term's orientation must change by this angle. Variables
        // stay within incoming +/- pi, so the chord grows monotonically. For
        // that bone, |du|^2 + |dv|^2 >= |du-dv|^2 / 2. This bounds the Cartesian
        // pose score independently of FK translations, pins and other joints.
        const double chord = 2.0 * std::sin(angle * 0.5);
        double minimum_edge_change = std::numeric_limits<double>::infinity();
        for (auto [index, coefficient] : groups[i].key.terms) {
            const auto& variable = model.angle_variables[index];
            double member_bound = 0.0;
            for (std::size_t m = 0; m < variable.members.size(); ++m) {
                auto* bone = variable.members[m];
                const auto u = model.incoming_positions[model.node_indices.at(&bone->parent_node())];
                const auto v = model.incoming_positions[model.node_indices.at(&bone->child_node())];
                const double length = bone->scaled_length();
                const double theta = variable.incoming + variable.offsets[m];
                const sm::point reference_edge{length * std::cos(theta), length * std::sin(theta)};
                const double mismatch = sm::distance(reference_edge, v - u);
                const double roundoff = 32.0 * std::numeric_limits<double>::epsilon()
                    * std::max({length, std::abs(u.x), std::abs(u.y), std::abs(v.x), std::abs(v.y)});
                member_bound = std::max(member_bound, chord * length - mismatch - roundoff);
            }
            minimum_edge_change = std::min(minimum_edge_change, member_bound);
        }
        const double normalized = minimum_edge_change / model.characteristic_length;
        // Groups can share endpoints; their bounds cannot be added together.
        bound = std::max(bound, normalized * normalized / (2.0 * model.nodes.size()));
    }
    return bound;
}

sm::result eliminate_rigid_pin_coordinates(kinematic_model& model, solve_context& context) {
    if (model.mode != translation_mode::pin_anchor || context.pins.size() < 2) {
        return sm::result::success;
    }

    // Along a path sharing one angular variable, the pin displacement is R(theta) * v.
    // Its radius is fixed and its direction fixes theta, so two coordinate equalities
    // would be redundant. This structural reduction does not discard other pin rows.
    constexpr auto no_variable = std::numeric_limits<std::size_t>::max();
    struct path_info {
        std::size_t variable = std::numeric_limits<std::size_t>::max();
        sm::point offset{};
        bool multiple_variables = false;
    };
    std::vector<std::vector<std::pair<std::size_t, const kinematic_step*>>> adjacent(model.nodes.size());
    for (const auto& step : model.steps) {
        adjacent[step.parent_node].push_back({step.child_node, &step});
        adjacent[step.child_node].push_back({step.parent_node, &step});
    }
    std::vector<path_info> paths(model.nodes.size());
    std::vector<bool> visited(model.nodes.size(), false);
    std::vector<std::size_t> pending{model.anchor_node};
    visited[model.anchor_node] = true;
    while (!pending.empty()) {
        const auto current = pending.back();
        pending.pop_back();
        for (auto [next, step] : adjacent[current]) {
            if (visited[next]) continue;
            visited[next] = true;
            pending.push_back(next);
            auto& path = paths[next];
            path = paths[current];
            if (path.variable == no_variable) path.variable = step->variable;
            else if (path.variable != step->variable) path.multiple_variables = true;
            const double length = current == step->parent_node ? step->signed_length : -step->signed_length;
            path.offset += sm::point{length * step->offset_cos, length * step->offset_sin};
        }
    }

    const double position_tolerance = std::max(1e-9, k_position_feasibility_scale);
    for (auto& pin : context.pins) {
        if (pin.node_index == model.anchor_node) continue;
        const auto& path = paths[pin.node_index];
        if (path.multiple_variables || path.variable == no_variable) continue;
        const auto target = pin.position - model.anchor_position;
        const double radius = std::hypot(path.offset.x, path.offset.y);
        const double target_radius = std::hypot(target.x, target.y);
        if (std::abs(radius - target_radius) > position_tolerance) {
            return sm::result::unsatisfiable_constraints;
        }
        if (radius + target_radius <= position_tolerance) {
            // Coincident endpoints already satisfy the pin for every orientation.
            pin.implied_by_bounds = true;
            continue;
        }
        const auto variable = path.variable;
        double angle = model.lower_bounds[variable];
        if (model.lower_bounds[variable] != model.upper_bounds[variable]) {
            angle = lift_near(std::atan2(target.y, target.x) - std::atan2(path.offset.y, path.offset.x),
                              context.incoming_x[variable]);
            if (angle < model.lower_bounds[variable] - k_angular_feasibility_tolerance
                || angle > model.upper_bounds[variable] + k_angular_feasibility_tolerance) {
                return sm::result::unsatisfiable_constraints;
            }
            angle = std::clamp(angle, model.lower_bounds[variable], model.upper_bounds[variable]);
        }
        const double c = std::cos(angle), s = std::sin(angle);
        const sm::point displacement{c * path.offset.x - s * path.offset.y,
                                     s * path.offset.x + c * path.offset.y};
        if (sm::distance(displacement, target) > position_tolerance) {
            return sm::result::unsatisfiable_constraints;
        }
        model.lower_bounds[variable] = model.upper_bounds[variable] = angle;
        context.incoming_x[variable] = angle;
        pin.implied_by_bounds = true;
    }
    return sm::result::success;
}

sm::result configure_model(
    const region& source,
    const sm::fabrik_options& opts,
    kinematic_model& model,
    solve_context& context) {

    model.source = &source;
    model.topology = &std::get<0>(source.effectors.front())->owner().owner();
    model.bones.assign(source.bones.begin(), source.bones.end());
    std::ranges::sort(model.bones, bone_id_less);

    auto status = build_angle_variables(model);
    if (status != sm::result::success) return status;
    status = build_kinematics(model);
    if (status != sm::result::success) return status;

    for (auto* bone : model.bones) {
        auto parent = bone->parent_bone();
        if (!parent) continue;
        auto found = model.bone_coordinates.find(parent->ptr());
        if (found == model.bone_coordinates.end()) continue;
        const auto a = model.bone_coordinates.at(bone).variable;
        const auto b = found->second.variable;
        if (a != b) model.articulations.emplace_back(std::min(a, b), std::max(a, b));
    }
    std::ranges::sort(model.articulations);
    model.articulations.erase(std::unique(model.articulations.begin(), model.articulations.end()),
                             model.articulations.end());

    context.model = &model;
    context.target_tolerance = opts.tolerance;
    context.incoming_x.reserve(model.angle_count() + 2);
    for (const auto& variable : model.angle_variables) context.incoming_x.push_back(variable.incoming);

    if (!source.pins.empty()) {
        model.mode = translation_mode::pin_anchor;
        model.anchor_node = model.node_indices.at(mutable_ptr(source.pins.front()));
        model.anchor_position = source.pins.front()->world_pos();
    } else if (source.effectors.size() == 1) {
        model.mode = translation_mode::effector_anchor;
        auto effector = std::get<0>(source.effectors.front());
        model.anchor_node = model.node_indices.at(mutable_ptr(effector));
        model.anchor_position = std::get<1>(source.effectors.front());
    } else {
        model.mode = translation_mode::free_translation;
        const auto root = model.nodes.front();
        context.incoming_x.push_back(root->world_x());
        context.incoming_x.push_back(root->world_y());
    }

    model.lower_bounds.assign(model.variable_count(), -std::numeric_limits<double>::infinity());
    model.upper_bounds.assign(model.variable_count(), std::numeric_limits<double>::infinity());
    for (std::size_t i = 0; i < model.angle_count(); ++i) {
        model.lower_bounds[i] = model.angle_variables[i].incoming - std::numbers::pi;
        model.upper_bounds[i] = model.angle_variables[i].incoming + std::numbers::pi;
    }

    const bool max_delta_enabled = std::isfinite(opts.max_ang_delta)
        && opts.max_ang_delta > 0.0 && opts.max_ang_delta < std::numbers::pi;
    if (max_delta_enabled) {
        for (std::size_t i = 0; i < model.angle_variables.size(); ++i) {
            const auto& variable = model.angle_variables[i];
            double low = model.lower_bounds[i];
            double high = model.upper_bounds[i];
            for (std::size_t m = 0; m < variable.members.size(); ++m) {
                const double member_center = lift_near(
                    variable.members[m]->world_rotation() - variable.offsets[m],
                    variable.incoming);
                low = std::max(low, member_center - opts.max_ang_delta);
                high = std::min(high, member_center + opts.max_ang_delta);
            }
            if (low > high + k_angular_feasibility_tolerance) return sm::result::unsatisfiable_constraints;
            model.lower_bounds[i] = low;
            model.upper_bounds[i] = high;
            context.incoming_x[i] = std::clamp(context.incoming_x[i], low, high);
        }
    }

    context.targets.reserve(source.effectors.size());
    for (const auto& [node, target] : source.effectors) {
        context.targets.push_back({model.node_indices.at(mutable_ptr(node)), target});
    }
    context.pins.reserve(source.pins.size());
    for (auto pin : source.pins) {
        context.pins.push_back({model.node_indices.at(mutable_ptr(pin)), pin->world_pos()});
    }

    status = eliminate_rigid_pin_coordinates(model, context);
    if (status != sm::result::success) return status;

    context.last_x.resize(model.variable_count());
    context.state.angle_directions.resize(model.angle_count());
    context.state.local.resize(model.nodes.size());
    context.state.local_jacobian.resize(model.nodes.size() * model.angle_count());
    context.state.world.resize(model.nodes.size());
    context.state.world_jacobian.resize(model.nodes.size() * model.variable_count());

    return sm::result::success;
}

std::optional<candidate> exact_rigid_pose(solve_context& context, const sm::fabrik_options& opts) {
    const auto& model = *context.model;
    if (model.mode != translation_mode::effector_anchor || model.angle_count() != 1
        || !model.articulations.empty()) {
        return std::nullopt;
    }

    // A single rigid coordinate leaves only a rotation about the eliminated
    // effector anchor. Its Cartesian least-squares minimum is given by the
    // dot/cross correlation, without subtracting nearly equal objective values.
    context.ensure(context.incoming_x);
    const auto anchor = model.anchor_position;
    const double inverse_scale = 1.0 / model.characteristic_length;
    double dot = 0.0, cross = 0.0;
    for (std::size_t i = 0; i < model.nodes.size(); ++i) {
        const auto v = inverse_scale * (context.state.world[i] - anchor);
        const auto q = inverse_scale * (model.incoming_positions[i] - anchor);
        dot += v.x * q.x + v.y * q.y;
        cross += v.x * q.y - v.y * q.x;
    }
    if (!std::isfinite(dot) || !std::isfinite(cross)) return std::nullopt;

    auto exact_x = context.incoming_x;
    if (dot != 0.0 || cross != 0.0) {
        exact_x[0] = lift_near(exact_x[0] + std::atan2(cross, dot), context.incoming_x[0]);
    }
    if (exact_x[0] < model.lower_bounds[0] || exact_x[0] > model.upper_bounds[0]) {
        return std::nullopt;
    }
    return validate_candidate(context, exact_x, opts);
}

sm::result solve_region(
    const region& source,
    const sm::fabrik_options& opts,
    std::vector<std::pair<sm::node*, sm::point>>& writes) {

    if (source.effectors.empty()) return sm::result::fabrik_target_reached;

    kinematic_model model;
    solve_context context;
    auto status = configure_model(source, opts, model, context);
    if (status != sm::result::success) return status;

    std::vector<angular_group> angular_groups;
    status = build_angular_groups(model, context.incoming_x, angular_groups);
    if (status != sm::result::success) return status;

    std::optional<candidate> best;
    if (auto initial = validate_candidate(context, context.incoming_x, opts)) best = std::move(*initial);
    bool incoming_valid = best.has_value();
    // FK can repair an inconsistent incoming fan. Such a repair is not a
    // continuity anchor: allow every chart when restoring an invalid pose.
    for (std::size_t i = 0; incoming_valid && i < model.nodes.size(); ++i) {
        const auto p = model.incoming_positions[i];
        const double roundoff = 32.0 * std::numeric_limits<double>::epsilon()
            * std::max({model.characteristic_length, std::abs(p.x), std::abs(p.y)});
        incoming_valid = sm::distance(best->positions[i], p)
            <= std::max(k_position_feasibility_scale, roundoff);
    }
    std::optional<double> preferred_chart_pose;
    auto rigid_optimum = opts.max_iterations > 0
        ? exact_rigid_pose(context, opts) : std::optional<candidate>{};
    if (rigid_optimum) best = std::move(*rigid_optimum);

    if (!rigid_optimum && model.variable_count() != 0 && opts.max_iterations > 0) {
        int remaining_budget = std::max(1, opts.max_iterations) * k_evaluations_per_iteration;
        const auto branches = make_branch_selections(angular_groups);
        const auto seeds = make_seeds(model, context.incoming_x);

        for (std::size_t branch_index = 0;
             branch_index < branches.size() && remaining_budget > 0;
            ++branch_index) {
            const auto& branch = branches[branch_index];
            if (preferred_chart_pose
                && chart_pose_lower_bound(model, angular_groups, branch)
                    > *preferred_chart_pose + k_pose_score_roundoff) {
                continue;
            }
            std::optional<candidate> branch_best;
            std::optional<candidate> continuous_best;
            bool pose_refined = false;
            auto consider_continuous = [&](const candidate& value) {
                if (preferred_chart_pose
                    && value.pose_score <= *preferred_chart_pose + k_pose_score_roundoff
                    && (!continuous_best || candidate_better(value, *continuous_best, opts.tolerance))) {
                    continuous_best = value;
                }
            };

            // A target-only solve can travel along a redundant chain's feasible
            // manifold before refinement ever sees it. Start a competing local
            // pose solve from the incoming configuration for nearby targets.
            // All escape seeds below still run and compete with this candidate.
            if (model.mode != translation_mode::effector_anchor && best
                && std::ranges::all_of(best->target_errors, [&](double error) {
                    return error <= model.characteristic_length * 0.25;
                })) {
                std::vector<double> caps(context.targets.size(), opts.tolerance * 0.5);
                auto warm_x = context.incoming_x;
                const int allocation = std::min(80, std::max(1, remaining_budget / 4));
                const int budget_before = remaining_budget;
                run_optimizer(context, angular_groups, branch, warm_x,
                              remaining_budget, allocation, true, caps);
                if (auto warm = validate_branch_candidate(context, warm_x, opts, angular_groups, branch)) {
                    consider_continuous(*warm);
                    const bool reached = std::ranges::all_of(warm->target_errors,
                        [&](double error) { return error <= opts.tolerance; });
                    branch_best = std::move(*warm);
                    pose_refined = reached && budget_before - remaining_budget < allocation;
                }
            }

            prepared_optimizer seed_optimizer;
            for (std::size_t seed_index = 0;
                 seed_index < seeds.size() && remaining_budget > 0;
                 ++seed_index) {
                auto x = seeds[seed_index];
                const int attempts_left = static_cast<int>(
                    (branches.size() - branch_index) * (seeds.size() - seed_index) + 1);
                const int allocation = std::max(1, remaining_budget / std::max(attempts_left, 1));
                const bool pose_first = model.mode == translation_mode::effector_anchor;
                run_optimizer(
                    context,
                    angular_groups,
                    branch,
                    x,
                    remaining_budget,
                    allocation,
                    pose_first,
                    {},
                    &seed_optimizer);
                if (auto solved = validate_branch_candidate(context, x, opts, angular_groups, branch)) {
                    consider_continuous(*solved);
                    if (!branch_best || candidate_better(*solved, *branch_best, opts.tolerance)) {
                        branch_best = std::move(*solved);
                        pose_refined = false;
                    }
                }
            }

            if (branch_best && !pose_refined
                && model.mode != translation_mode::effector_anchor && remaining_budget > 0) {
                std::vector<double> caps;
                caps.reserve(branch_best->target_errors.size());
                for (double error : branch_best->target_errors) {
                    const double preservation_slop = std::max(
                        opts.tolerance * 0.05,
                        model.characteristic_length * 1e-7);
                    caps.push_back(error <= opts.tolerance
                        ? opts.tolerance * 0.5
                        : error + preservation_slop);
                }
                auto refined_x = branch_best->x;
                const int allocation = std::max(1, remaining_budget / 2);
                run_optimizer(
                    context,
                    angular_groups,
                    branch,
                    refined_x,
                    remaining_budget,
                    allocation,
                    true,
                    caps);
                if (auto refined = validate_branch_candidate(context, refined_x, opts, angular_groups, branch)) {
                    bool within_caps = refined->target_errors.size() == caps.size();
                    for (std::size_t i = 0; within_caps && i < caps.size(); ++i) {
                        within_caps = refined->target_errors[i] <= caps[i] + 1e-9;
                    }
                    if (within_caps) consider_continuous(*refined);
                    if (within_caps && refined->pose_score < branch_best->pose_score) {
                        branch_best = std::move(*refined);
                    }
                }
            }

            // The incoming chart is the continuity anchor when the incoming pose
            // is legal. At a joint limit, keep its nearby legal result rather
            // than jumping to a distant chart merely to reach the target. Other
            // charts still compete if they are at least as close to that pose
            // reference; retain eligible candidates from every seed, not only
            // the unrestricted branch winner. Invalid incoming poses may use
            // any chart to restore feasibility.
            if (preferred_chart_pose) branch_best = std::move(continuous_best);
            if (branch_best && (!best || candidate_better(*branch_best, *best, opts.tolerance))) {
                best = std::move(*branch_best);
            }
            if (branch_index == 0 && incoming_valid && best) {
                preferred_chart_pose = best->pose_score;
            }
            if (best && branch_index == 0
                && std::ranges::all_of(best->target_errors,
                    [&](double error) { return error <= opts.tolerance; })) {
                break;
            }
        }
    }

    if (!best) return sm::result::fabrik_no_solution_found;

    for (std::size_t i = 0; i < model.nodes.size(); ++i) {
        writes.push_back({model.nodes[i], best->positions[i]});
    }

    const auto reached = std::ranges::count_if(best->target_errors,
        [&](double error) { return error <= opts.tolerance; });
    if (reached == static_cast<std::ptrdiff_t>(best->target_errors.size())) {
        return sm::result::fabrik_target_reached;
    }
    if (reached != 0) return sm::result::fabrik_mixed;
    return sm::result::fabrik_converged;
}

sm::result validate_fabrik_inputs(
    const std::vector<std::tuple<sm::node_ref, sm::point>>& effectors,
    const std::vector<sm::node_ref>& pins,
    const sm::fabrik_options& opts) {

    if (effectors.empty()) return sm::result::fabrik_no_solution_found;
    if (opts.max_iterations < 0 || !std::isfinite(opts.tolerance) || opts.tolerance <= 0.0) {
        return sm::result::fabrik_no_solution_found;
    }

    auto& owner = std::get<0>(effectors.front())->owner();
    for (const auto& [node, target] : effectors) {
        if (&node->owner() != &owner) return sm::result::cross_skeleton_bone;
        if (!finite(target) || !finite(node->world_pos())) return sm::result::out_of_bounds;
    }
    for (auto node : pins) {
        if (&node->owner() != &owner) return sm::result::cross_skeleton_bone;
        if (!finite(node->world_pos())) return sm::result::out_of_bounds;
    }
    return sm::result::success;
}

sm::result normalize_inputs(
    const std::vector<std::tuple<sm::node_ref, sm::point>>& effectors,
    const std::vector<sm::node_ref>& pins,
    double tolerance,
    std::vector<std::tuple<sm::node_ref, sm::point>>& normalized_effectors,
    std::vector<sm::node_ref>& normalized_pins) {

    std::unordered_map<sm::node*, sm::point> target_by_node;
    for (const auto& [node, target] : effectors) {
        auto [it, inserted] = target_by_node.emplace(mutable_ptr(node), target);
        if (!inserted && sm::distance(it->second, target) > tolerance) {
            return sm::result::fabrik_no_solution_found;
        }
        if (inserted) normalized_effectors.push_back({node, target});
    }

    std::unordered_set<sm::node*> seen_pins;
    for (auto pin : pins) {
        if (seen_pins.insert(mutable_ptr(pin)).second) normalized_pins.push_back(pin);
    }
    return sm::result::success;
}

std::vector<region> discover_regions(
    const std::vector<std::tuple<sm::node_ref, sm::point>>& effectors,
    const std::vector<sm::node_ref>& pins,
    sm::constraint_geometry& geometry) {

    std::unordered_set<sm::node*> boundaries;
    for (auto pin : pins) boundaries.insert(mutable_ptr(pin));

    std::vector<region> regions;
    std::unordered_set<sm::node*> assigned;

    for (const auto& [effector, target] : effectors) {
        if (boundaries.contains(mutable_ptr(effector)) || assigned.contains(mutable_ptr(effector))) continue;
        auto& component = regions.emplace_back();
        std::vector<std::pair<sm::node*, sm::bone*>> pending{{mutable_ptr(effector), nullptr}};
        std::unordered_set<sm::node*> found_pins;

        while (!pending.empty()) {
            auto [node, incoming] = pending.back();
            pending.pop_back();

            if (boundaries.contains(node)) {
                if (found_pins.insert(node).second) component.pins.push_back(*node);
                if (incoming) {
                    if (auto fan = geometry.fan_for(incoming)) {
                        auto members = geometry.fan_members(*fan);
                        std::ranges::sort(members, bone_id_less);
                        for (auto* member : members) {
                            if (&member->parent_node() == node && component.bones.insert(member).second) {
                                pending.push_back({&member->child_node(), member});
                            }
                        }
                    }
                }
                continue;
            }

            if (!component.nodes.insert(node).second) continue;
            assigned.insert(node);
            auto adjacent = node->adjacent_bones();
            std::ranges::sort(adjacent, [](const sm::bone_ref& a, const sm::bone_ref& b) {
                return a->id() < b->id();
            });
            for (auto edge : adjacent) {
                if (component.bones.insert(edge.ptr()).second) {
                    pending.push_back({&edge->opposite_node(*node), edge.ptr()});
                }
            }
        }

        std::ranges::sort(component.pins, [](const sm::node_ref& a, const sm::node_ref& b) {
            return a->id() < b->id();
        });
        for (const auto& entry : effectors) {
            if (component.nodes.contains(mutable_ptr(std::get<0>(entry)))) component.effectors.push_back(entry);
        }
    }
    return regions;
}

struct pose_restore_guard {
    std::vector<std::pair<sm::node*, sm::point>> saved;
    bool committed = false;
    ~pose_restore_guard() {
        if (!committed) {
            for (auto [node, position] : saved) node->set_world_pos(position);
        }
    }
};

} // namespace

sm::fabrik_options::fabrik_options()
    : max_iterations{k_max_iter},
      tolerance{k_tolerance},
      forw_reaching_constraints{false},
      max_ang_delta{0.0} {}

sm::result sm::perform_fabrik(
    const std::vector<std::tuple<node_ref, point>>& effectors,
    const std::vector<node_ref>& pins,
    const fabrik_options& opts) {

    auto validation = validate_fabrik_inputs(effectors, pins, opts);
    if (validation != result::success) return validation;

    std::vector<std::tuple<node_ref, point>> normalized_effectors;
    std::vector<node_ref> normalized_pins;
    validation = normalize_inputs(
        effectors, pins, opts.tolerance, normalized_effectors, normalized_pins);
    if (validation != result::success) return validation;

    std::unordered_set<node*> boundaries;
    for (auto pin : normalized_pins) boundaries.insert(mutable_ptr(pin));
    for (const auto& [effector, target] : normalized_effectors) {
        if (boundaries.contains(mutable_ptr(effector))
            && distance(effector->world_pos(), target) > opts.tolerance) {
            return result::fabrik_no_solution_found;
        }
    }

    auto& topology = std::get<0>(normalized_effectors.front())->owner().owner();
    constraint_geometry geometry(topology);
    if (geometry.status() != result::success) return geometry.status();

    auto regions = discover_regions(normalized_effectors, normalized_pins, geometry);
    geometry_batch batch(topology);
    pose_restore_guard invocation_guard;
    for (auto skeleton : topology.skeletons()) {
        for (auto node : skeleton->nodes()) {
            invocation_guard.saved.push_back({mutable_ptr(node), node->world_pos()});
        }
    }

    std::vector<std::pair<node*, point>> writes;
    for (const auto& component : regions) {
        writes.clear();
        const auto outcome = solve_region(component, opts, writes);
        if (outcome == result::invalid_constraint
            || outcome == result::inconsistent_constraints
            || outcome == result::unsatisfiable_constraints
            || outcome == result::fabrik_no_solution_found
            || outcome == result::out_of_bounds) {
            return outcome;
        }
        for (auto [node, position] : writes) node->set_world_pos(position);

        auto active = component.bones;
        auto valid = geometry.validate(
            std::max(k_angular_feasibility_tolerance,
                     component.bones.empty() ? k_angular_feasibility_tolerance
                                             : 1e-8),
            true,
            &active);
        if (valid != result::success) return valid;
        for (auto pin : component.pins) {
            auto saved = std::ranges::find_if(invocation_guard.saved,
                [&](const auto& item) { return item.first == mutable_ptr(pin); });
            if (saved == invocation_guard.saved.end()
                || distance(pin->world_pos(), saved->second)
                    > std::max(1e-9, k_position_feasibility_scale)) {
                return result::unsatisfiable_constraints;
            }
        }
    }

    if (auto status = batch.commit(); status != result::success) return status;
    invocation_guard.committed = true;

    std::size_t reached = 0;
    for (const auto& [effector, target] : normalized_effectors) {
        if (distance(effector->world_pos(), target) <= opts.tolerance) ++reached;
    }
    if (reached == normalized_effectors.size()) return result::fabrik_target_reached;
    if (reached != 0) return result::fabrik_mixed;
    return result::fabrik_converged;
}

sm::result sm::perform_fabrik(
    node_ref effector,
    point effector_target,
    std::optional<sm::node_ref> pin,
    const fabrik_options& opts) {

    std::vector<std::tuple<sm::node_ref, sm::point>> one_effector{{effector, effector_target}};
    std::vector<sm::node_ref> pinned;
    if (pin) pinned.push_back(*pin);
    return sm::perform_fabrik(one_effector, pinned, opts);
}

double sm::constrain_rotation(sm::bone& bone, double theta) {
    constraint_geometry geometry(bone.owner().owner());
    auto clamped = geometry.allowed_angles(bone).closest_angle(theta);
    if (!clamped) throw std::invalid_argument("unsatisfiable rotation constraints");
    return *clamped;
}

sm::point sm::apply_rotation_constraints(
    const sm::point& curr_pos,
    sm::node& start_node,
    sm::maybe_bone_ref prev,
    sm::bone& current_bone,
    bool apply_rot_constraints,
    double max_ang_delta,
    double old_bone_rotation) {

    fabrik_neighborhood neighborhood{start_node, prev, current_bone};
    return apply_all_constraints(
        curr_pos,
        neighborhood,
        apply_rot_constraints,
        max_ang_delta,
        old_bone_rotation,
        constraint_geometry(current_bone.owner().owner()));
}
