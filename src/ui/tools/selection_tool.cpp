#include "selection_tool.hpp"
#include "../../model/selection.hpp"
#include "select_tool_panel.hpp"
#include "../panes/skeleton_pane.hpp"
#include "../util.hpp"
#include "../canvas/scene.hpp"
#include "../canvas/canvas_item.hpp"
#include "../canvas/skel_item.hpp"
#include "../canvas/node_item.hpp"
#include "../canvas/bone_item.hpp"
#include "../canvas/canvas_manager.hpp"
#include "../stick_man.hpp"
#include "../../core/sm_skeleton.hpp"
#include "../../core/sm_types.hpp"
#include "../../core/sm_visit.hpp"
#include "../../core/sm_fabrik.hpp"
#include <array>
#include <ranges>
#include <unordered_map>
#include <unordered_set>
#include <numbers>
#include <functional>
#include <qDebug>
using namespace std::placeholders;
namespace r = std::ranges;
namespace rv = std::ranges::views;

/*------------------------------------------------------------------------------------------------*/

namespace {

    template<class... Ts> struct overload : Ts... { using Ts::operator()...; };
    bool is_bone_from_u_to_v(sm::bone_ref src_bone, sm::node_ref u, sm::node_ref v) {
        bool found = false;
        sm::visit_bones(
            src_bone.get(),
            [&](sm::bone& visited_bone)->sm::visit_result {
                if (&visited_bone != &src_bone.get() && visited_bone.has_node(u.get())) {
                    return sm::visit_result::terminate_branch;
                }
                if (visited_bone.has_node(v.get())) {
                    found = true;
                    return sm::visit_result::terminate_traversal;
                }
                return sm::visit_result::continue_traversal;
            },
            false
        );
        return found;
    }
    std::vector<sm::skel_ref> skeletons_from_nodes(const std::vector<sm::node_ref>& nodes) {
        auto skels = nodes |
            rv::transform(
                [](auto node) {return &node->owner(); }
            ) | r::to<std::unordered_set>();

        return skels | rv::transform(
            [](auto* ptr)->sm::skel_ref {
                return *ptr;
            }
        ) | r::to<std::vector>();
    }
    sm::maybe_bone_ref find_bone_from_u_to_v(sm::node_ref u, sm::node_ref v) {
        auto adj = u->adjacent_bones();
        auto i = r::find_if(
            adj,
            [&](auto bone)->bool {
                return is_bone_from_u_to_v(bone, u, v);
            }
        );
        return (i != adj.end()) ? *i : sm::maybe_bone_ref{};
    }
    int dist(std::unordered_map<sm::node*, int> visited, sm::node& u) {
        auto adj = u.adjacent_bones();
        for (auto adj_bone : adj) {
            auto& v = adj_bone->opposite_node(u);
            if (visited.contains(&v)) {
                return visited[&v] + 1;
            }
        }
        return -1;
    }
    std::unordered_set<sm::node*> all_pinned_nodes(sm::node& start, const ui::canvas::scene& canv) {
        std::unordered_set<sm::node*> pinned;
        sm::visit_nodes_and_bones(
            start,
            [&pinned, &canv](sm::node& n)->sm::visit_result {
                if (canv.is_node_pinned(n.id())) {
                    pinned.insert(&n);
                }
                return sm::visit_result::continue_traversal;
            },
            [](sm::bone&)->sm::visit_result {
                return sm::visit_result::continue_traversal;
            }
        );
        return pinned;
    }
    bool has_pinned_nodes(mdl::skel_piece piece, const ui::canvas::scene& canv) {
        sm::node_ref start = std::visit(
            overload{
                [](sm::node_ref node)->sm::node_ref {return node; },
                [](sm::bone_ref bone)->sm::node_ref {return bone->parent_node(); },
                [](sm::skel_ref skel)->sm::node_ref {return skel->root_node(); }
            },
            piece
        );
        bool found = false;
        sm::visit_nodes(start.get(),
            [&](sm::node& node)->sm::visit_result {
                if (canv.is_node_pinned(node.id())) {
                    found = true;
                    return sm::visit_result::terminate_traversal;
                }
                return sm::visit_result::continue_traversal;
            },
            false
        );
        return found;
    }
    std::tuple<sm::maybe_node_ref, int> find_closest_pinned_node(
        sm::node_ref start, const ui::canvas::scene& canv) {
        auto pinned_nodes = all_pinned_nodes(start.get(), canv);
        std::unordered_map<sm::node*, int> visited;
        std::unordered_set<sm::node*> candidates;
        sm::visit_nodes_and_bones(
            start.get(),
            [&](sm::node& n)->sm::visit_result {
                if (&n == &start.get()) {
                    visited[&n] = 0;
                    return sm::visit_result::continue_traversal;
                }
                else {
                    visited[&n] = dist(visited, n);
                }
                if (pinned_nodes.contains(&n)) {
                    candidates.insert(&n);
                    return sm::visit_result::terminate_branch;
                }
                return sm::visit_result::continue_traversal;
            },
            [&](sm::bone& b)->sm::visit_result {
                return sm::visit_result::continue_traversal;
            }
        );
        if (candidates.empty()) {
            return { {}, -1 };
        }
        auto min = r::min_element(
            candidates,
            [&](auto&& lhs, auto&& rhs) {
                return visited.at(lhs) < visited.at(rhs);
            }
        );
        return {
            {sm::ref(**min)},
            visited.at(*min)
        };
    }
    using node_pair = std::tuple<sm::node_ref, sm::node_ref>;
    std::optional<node_pair> rot_info_for_rotate_on_pin(
        const mdl::skel_piece& model, const ui::canvas::scene& canv) {
        return std::visit(
            overload{
                [&canv](sm::node_ref node)->std::optional<node_pair> {
                    auto [closest, dist] = find_closest_pinned_node(node, canv);
                    if (!closest) {
                        return {};
                    }
                    return {{
                        *closest,
                        node
                    }};
                },
                [&canv](sm::bone_ref bone)->std::optional<node_pair> {
                    auto& u = bone->parent_node();
                    auto& v = bone->child_node();
                    if (canv.is_node_pinned(u.id()) && canv.is_node_pinned(v.id())) {
                        return { {u, v} };
                    }
                    auto [closest_to_u, u_dist] = find_closest_pinned_node(u, canv);
                    auto [closest_to_v, v_dist] = find_closest_pinned_node(v, canv);
                    if (!closest_to_u && !closest_to_v) {
                        return {};
                    }
                    if ((u_dist > 0 && u_dist < v_dist) || !closest_to_v) {
                        return { {*closest_to_u, v} };
                    }
                    if (&(closest_to_v->get()) != &u) {
                        return { {*closest_to_v, u } };
                    }
                    return {};
                },
                [](sm::skel_ref bone)->std::optional<node_pair> {
                    return {};
                }
            },
            model
        );
        return {};
    }
    template<typename T>
    ui::canvas::item::rubber_band* create_rubber_band(ui::canvas::scene& canv, QPointF pt) {
        auto* rb = new T(pt);
        canv.addItem(dynamic_cast<QGraphicsItem*>(rb));
        return rb;
    }

    void destroy_rubber_band(ui::canvas::scene& canv, ui::canvas::item::rubber_band* rb) {
        if (!rb) return;
        canv.removeItem(dynamic_cast<QGraphicsItem*>(rb));
        delete rb;
    }
    ui::canvas::selection_set topology_items(const auto& items) {
        using namespace ui::canvas;
        selection_set result;
        for (auto* item : items) {
            if (auto* character = dynamic_cast<item::character*>(item)) {
                for (auto skel : character->model().rig().skeletons()) {
                    for (auto node : skel->nodes()) result.insert(&item_from_model<item::node>(node.get()));
                    for (auto bone : skel->bones()) result.insert(&item_from_model<item::bone>(bone.get()));
                }
            } else if (auto* skel = dynamic_cast<item::skeleton*>(item)) {
                for (auto node : skel->model().nodes()) {
                    result.insert(&item_from_model<item::node>(node.get()));
                }
                for (auto bone : skel->model().bones()) {
                    result.insert(&item_from_model<item::bone>(bone.get()));
                }
            } else {
                result.insert(item);
            }
        }
        return result;
    }

    // Apply modifiers to topology first, then promote only a union of complete skeletons.
    void select_topology(ui::canvas::scene& canv,
        std::span<ui::canvas::item::base*> items, bool add, bool subtract) {
        using namespace ui::canvas;
        auto incoming = topology_items(items);
        auto selection = (add != subtract) ? topology_items(canv.selection()) : selection_set{};
        for (auto* item : incoming) {
            if (subtract && !add) selection.erase(item);
            else selection.insert(item);
        }
        mdl::selection objects;
        for (auto* item : selection) objects.push_back(item->to_selection_object());
        std::vector<item::base*> selected;
        for (const auto& object : mdl::infer_selection(objects)) std::visit(overload{
            [&](sm::const_node_ref n) { selected.push_back(&item_from_model<item::node>(n.get())); },
            [&](sm::const_bone_ref b) { selected.push_back(&item_from_model<item::bone>(b.get())); },
            [&](sm::const_skel_ref s) { selected.push_back(&item_from_model<item::skeleton>(s.get())); },
            [&](sm::const_character_ref c) { selected.push_back(canv.character_item(c->id())); }
        }, object);
        canv.set_selection(selected, true);
    }

    std::optional<QRectF> points_to_rect(QPointF pt1, QPointF pt2) {
        auto width = std::abs(pt1.x() - pt2.x());
        auto height = std::abs(pt1.y() - pt2.y());

        if (width == 0.0f && height == 0.0f) {
            return {};
        }
        auto left = std::min(pt1.x(), pt2.x());
        auto bottom = std::min(pt1.y(), pt2.y());
        return QRectF(
            QPointF(left, bottom),
            QSizeF(width, height)
        );
    }

    void do_ragdoll_rotate(double theta, ui::tool::rotation_state& state) {
        sm::point offset = state.radius() * sm::point(std::cos(theta), std::sin(theta));
        auto new_loc = state.axis().world_pos() + offset;
        auto result = sm::perform_fabrik(state.rotating(), new_loc, state.axis());
        //TODO: do something with 'result' here...
    }


    // make a table mapping node's to the offset from their parents incorporating
    // translations by delta on selected nodes but such that no nodes receive
    // "double translations"; that is, if we need to translate both u and v and
    // u is v's predecessor in the bone hierarchy traversal then only u should be
    // translated in the table.
    std::unordered_map<sm::node*, sm::point> rubber_band_translation_table(
        sm::node& src, const sm::point& delta, const std::vector<sm::node_ref>& selected) {

        std::unordered_map<sm::node*, sm::point> tbl;
        std::unordered_set<sm::node*> has_been_translated;
        auto should_be_translated = selected | rv::transform(
            [](auto ref) {return ref.ptr(); }
        ) | r::to<std::unordered_set>();
        auto visit_node = [&](sm::maybe_node_ref prev, sm::node& node) {
            auto prev_pos = (prev) ? prev->get().world_pos() : sm::point{ 0,0 };
            auto trans_offset = node.world_pos() - prev_pos;
            bool was_translated = prev &&
                has_been_translated.contains(&(prev->get()));
            if (should_be_translated.contains(&node) && !was_translated) {
                trans_offset += delta;
                was_translated = true;
            }
            if (was_translated) {
                has_been_translated.insert(&node);
            }
            tbl[&node] = trans_offset;
            };
        sm::visit_bone_hierarchy(src,
            [&](sm::maybe_bone_ref maybe_prev, sm::bone& bone)->sm::visit_result {
                if (!maybe_prev) {
                    visit_node({}, src);
                }

                sm::node_ref prev_node = (maybe_prev) ?
                    *bone.shared_node(maybe_prev->get()) :
                    sm::node_ref{ src };

                visit_node(
                    prev_node,
                    bone.opposite_node(prev_node)
                );
                return sm::visit_result::continue_traversal;
            }
        );

        return tbl;
    }

    void do_rubber_band_translate(sm::node& src,
        const sm::point& delta, const std::vector<sm::node_ref>& sel) {
        auto tbl = rubber_band_translation_table(src, delta, sel);
        sm::visit_bone_hierarchy(src,
            [&](sm::maybe_bone_ref maybe_prev, sm::bone& bone)->sm::visit_result {
                if (!maybe_prev) {
                    src.set_world_pos(tbl.at(&src));
                }

                sm::node_ref prev_node = (maybe_prev) ?
                    *bone.shared_node(maybe_prev->get()) :
                    sm::node_ref{ src };
                auto& curr_node = bone.opposite_node(prev_node);
                auto new_v_pos = prev_node->world_pos() + tbl.at(&curr_node);
                new_v_pos = sm::apply_rotation_constraints(new_v_pos, src, maybe_prev, bone);

                curr_node.set_world_pos(new_v_pos);

                return sm::visit_result::continue_traversal;
            }
        );
    }
    void do_ragdoll_translate(sm::skel_ref& skel,
        const sm::point& delta, const std::vector<sm::node_ref>& sel,
        const std::unordered_set<sm::node*>& pinned) {

        auto effectors = sel | rv::filter(
            [&](auto node) {
                return &node->owner() == skel.ptr();
            }
        ) | rv::transform(
            [&](auto node)->std::tuple <sm::node_ref, sm::point> {
                return {
                    node,
                    node->world_pos() + delta
                };
            }
        ) | r::to<std::vector>();
        auto pinned_nodes = pinned | rv::transform(
            [](auto* node_ptr)->sm::node_ref {
                return *node_ptr;
            }
        ) | r::to<std::vector>();

        auto result = sm::perform_fabrik(effectors, pinned_nodes);

        //TODO: do something with 'result' here...
    }
    std::tuple<sm::node_ref, sm::point> translation_anchor(mdl::skel_piece item, QPointF click_pt) {
        auto anchor_node = std::visit(
            overload{
                [](sm::node_ref node)->sm::node_ref {
                    return node;
                },
                [](sm::bone_ref bone)->sm::node_ref {
                    return bone->parent_node();
                },
                [](sm::skel_ref skel)->sm::node_ref {
                    return skel->root_node();
                }
            },
            item
        );
        return { anchor_node,
            ui::from_qt_pt(click_pt) - anchor_node->world_pos()
        };
    }
    std::vector<sm::node_ref> skel_piece_to_nodes(mdl::skel_piece piece) {
        return std::visit(
            overload{
                [](sm::node_ref node)->std::vector<sm::node_ref> {
                    return {node};
                },
                [](sm::bone_ref bone)->std::vector<sm::node_ref> {
                    return { bone->parent_node(), bone->child_node() };
                },
                [](sm::skel_ref skel)->std::vector<sm::node_ref> {
                    return skel->nodes() | r::to<std::vector>();
                }
            },
            piece
        );
    }
    std::vector<sm::node_ref> selection_to_nodes(ui::canvas::scene& canv) {
        std::unordered_set<sm::node*> unique_nodes;
        for (auto* item : topology_items(canv.selection())) {
            auto nodes_from_piece = skel_piece_to_nodes(item->to_skeleton_piece());
            r::copy(
                nodes_from_piece | rv::transform([](auto node) {return node.ptr(); }),
                std::inserter(unique_nodes, unique_nodes.end())
            );
        }
        return unique_nodes | rv::transform(
            [](auto* node_ptr)->sm::node_ref {
                return *node_ptr;
            }
        ) | r::to<std::vector>();
    }
    std::vector<sm::node_ref> selected_nodes_for_translation(
        ui::canvas::scene& canv, QPointF clicked_pt) {
        auto* clicked_item = canv.top_item(clicked_pt);
        if (!clicked_item) return {};
        if (auto* character = canv.selected_character()) {
            if (clicked_item == character) return selection_to_nodes(canv);
            auto clicked_nodes = skel_piece_to_nodes(clicked_item->to_skeleton_piece());
            if (!clicked_nodes.empty() && character->model().rig().contains(clicked_nodes.front()->owner().id()))
                return selection_to_nodes(canv);
        }
        bool selected = clicked_item->is_selected() || std::visit([](auto piece) {
            using T = std::remove_cvref_t<decltype(piece.get())>;
            if constexpr (std::is_same_v<T, sm::skeleton>) {
                return false;
            } else {
                return ui::canvas::item_from_model<ui::canvas::item::skeleton>(piece->owner()).is_selected();
            }
        }, clicked_item->to_skeleton_piece());
        if (!selected) {
            return skel_piece_to_nodes(clicked_item->to_skeleton_piece());
        }
        return selection_to_nodes(canv);
    }
    std::vector<sm::node_ref> pinned_nodes_for_translation(ui::canvas::scene& canv) {
        return canv.node_items() | rv::filter(
            [&canv](auto* node) { return canv.is_node_pinned(node->model().id()); }
        ) | rv::transform(
            [](auto* node)->sm::node_ref { return node->model();  }
        ) | r::to<std::vector>();
    }
}

ui::tool::select::select() :
    settings_panel_(nullptr),
    project_(nullptr),
    base("selection", "arrow_icon.png", ui::tool::id::selection) {}
void ui::tool::select::init(canvas::manager& canvases, mdl::project& model) {
    project_ = &model;
    canvases_ = &canvases;
}

void ui::tool::select::activate(canvas::manager& canv_mgr) {
    drag_ = {};
}

void ui::tool::select::keyReleaseEvent(canvas::scene& c, QKeyEvent* event) {}

void ui::tool::select::mousePressEvent(canvas::scene& canv, QGraphicsSceneMouseEvent* event) {
    click_pt_ = event->scenePos();
}

bool  ui::tool::select::is_dragging() const {
    return drag_.has_value();
}
std::optional<ui::tool::rubber_band_type> ui::tool::select::kind_of_rubber_band(
    canvas::scene& canv, QPointF pt) {
    auto* selected_item = canv.top_item(pt);
    if (!selected_item) {
        return selection_rb;
    }
    if (!settings_panel_->has_drag_behavior()) {
        return {};
    }
    auto settings = settings_panel_->settings();
    if (auto* character = canv.selected_character()) {
        bool belongs = std::visit([&](auto ref) {
            using T = std::remove_cvref_t<decltype(ref.get())>;
            if constexpr (std::is_same_v<T, sm::character>) return ref->id() == character->id();
            else if constexpr (std::is_same_v<T, sm::skeleton>) return character->model().rig().contains(ref->id());
            else return character->model().rig().contains(ref->owner().id());
        }, selected_item->to_selection_object());
        if (belongs) return translation_rb;
    }
    if (settings.is_in_rotate_mode_) {
        return rotation_rb;
    }
    return translation_rb;
}
std::optional<ui::tool::drag_state> ui::tool::select::create_drag_state(
    rubber_band_type typ, ui::canvas::scene& canv, QPointF pt) const {
    using drag_state_fn = std::function<std::optional<drag_state>()>;
    const std::unordered_map<rubber_band_type, drag_state_fn> tbl = {
        {selection_rb, [&]()->std::optional<drag_state> {
                auto* rb = ::create_rubber_band<canvas::item::rect_rubber_band>(canv, pt);
                return drag_state{
                    from_qt_pt(pt), rb, selection_rb, std::monostate{}
                };
            }
        },
        {translation_rb, [&]()->std::optional<drag_state> {
                auto state = create_translation_state(canv, pt, this->settings_panel_->settings());
                if (!state) {
                    return {};
                }
                return drag_state{
                    from_qt_pt(pt), nullptr, translation_rb, std::move(*state)
                };
            }
        },
        {rotation_rb,
            [&]()->std::optional<drag_state> {
                auto ri = create_rotation_state(canv, pt, this->settings_panel_->settings());
                if (!ri) {
                    return {};
                }
                auto* arb = static_cast<ui::canvas::item::arc_rubber_band*>(
                    ::create_rubber_band<canvas::item::arc_rubber_band>(canv,
                        to_qt_pt(ri->axis().world_pos())
                    )
                );
                auto axis_pt = ui::to_qt_pt(ri->axis().world_pos());
                auto rotating_pt = ui::to_qt_pt(ri->rotating().world_pos());
                arb->set_radius(ui::distance(axis_pt, rotating_pt));
                arb->set_from_theta(ui::angle_through_points(axis_pt, rotating_pt));

                return drag_state{ from_qt_pt(pt), arb, rotation_rb, std::move(*ri) };
            }
        }
    };

    return tbl.at(typ)();
}
void  ui::tool::select::do_dragging(canvas::scene& canv, QPointF pt) {
    bool started = false;
    if (!is_dragging()) {
        auto rb_type = kind_of_rubber_band(canv, *click_pt_);
        if (!rb_type) return;
        drag_ = create_drag_state(*rb_type, canv, *click_pt_);
        started = drag_.has_value();
    }
    if (is_dragging()) {
        drag_->pt = from_qt_pt(pt);
        if (drag_->rubber_band) {
            drag_->rubber_band->handle_drag(pt);
        }
        std::visit(
            overload{
                [](std::monostate) {
                },
                [&](rotation_state& ri) {
                    if (started && animation_authoring_ && animation_authoring_->begin)
                        animation_authoring_->begin(authored_rotation_for(ri));
                    handle_rotation(canv, pt, ri);
                    if (animation_authoring_ && animation_authoring_->update)
                        animation_authoring_->update(authored_rotation_for(ri));
                },
                [&](translation_state& ti) {
                    handle_translation(canv, pt, ti);
                }
            },
            drag_->extra
        );
    }
}
std::optional<ui::tool::rotation_state> ui::tool::select::create_rotation_state(
    ui::canvas::scene& canv, QPointF clicked_pt,
    const ui::tool::sel_drag_settings& settings) {
    std::optional<rotation_state> ri;

    auto* item = canv.top_item(clicked_pt);
    if (!item) {
        return {};
    }

    if (dynamic_cast<canvas::item::character*>(item)) return {};
    auto model = item->to_skeleton_piece();
    if (!settings.rotate_on_pinned_ || !has_pinned_nodes(model, canv)) {
        auto parent_bone = std::visit(
            overload{
                [](sm::node_ref node)->sm::maybe_bone_ref {
                    return node->parent_bone();
                },
                [](sm::bone_ref bone)->sm::maybe_bone_ref {
                    return bone;
                },
                [](sm::skel_ref bone)->sm::maybe_bone_ref {
                    return {};
                }
            },
            model
        );
        if (!parent_bone) {
            return {};
        }
        ri.emplace(
            parent_bone->get().parent_node(),
            parent_bone->get().child_node(),
            *parent_bone,
            settings.rotate_mode_
        );
    }
    else {
        auto nodes = rot_info_for_rotate_on_pin(model, canv);
        if (!nodes) {
            return {};
        }
        auto [axis, rotating] = *nodes;
        auto lead_bone = find_bone_from_u_to_v(axis, rotating);
        if (!lead_bone) {
            return {};
        }
        ri.emplace(
            axis,
            rotating,
            *lead_bone,
            settings.rotate_mode_
        );
    }
    return ri;
}
std::optional<ui::tool::translation_state> ui::tool::select::create_translation_state(
    ui::canvas::scene& canv, QPointF clicked_pt,
    const ui::tool::sel_drag_settings& settings) {
    auto* item = canv.top_item(clicked_pt);
    if (!item) {
        return {};
    }
    auto mode = settings.trans_mode_;
    auto anchor_piece = dynamic_cast<canvas::item::character*>(item)
        ? mdl::skel_piece{sm::ref(canv.resolved_skeletons().front()->model())}
        : item->to_skeleton_piece();
    auto [anchor, offset] = translation_anchor(anchor_piece, clicked_pt);
    auto selected_nodes = selected_nodes_for_translation(canv, clicked_pt);
    auto pinned_nodes = pinned_nodes_for_translation(canv);
    auto selected_skeletons = canv.resolved_skeletons();
    if (!selected_skeletons.empty() && (canv.selected_character() || selected_skeletons.size() == canv.selection().size()) &&
        r::any_of(selected_skeletons, [&](auto* skel) { return &skel->model() == &anchor->owner(); })) {
        mode = sel_drag_mode::rigid;
    }
    node_locs old_locs;
    for (auto skel : skeletons_from_nodes(selected_nodes)) {
        for (auto node : skel->nodes()) old_locs.emplace_back(node->id(), node->world_pos());
    }

    return { {
        std::move(selected_nodes),
        std::move(pinned_nodes),
        anchor,
        offset,
        mode,
        std::move(old_locs)
    } };
}
void ui::tool::select::pin_selection() {
    auto& canvas = canvases_->active_canvas();
    auto nodes = canvas.selected_nodes();
    if (nodes.empty()) {
        return;
    }
    for (auto* selected_node_item : nodes) {
        canvas.toggle_node_pinned(selected_node_item->model().id());
    }
}
void ui::tool::select::mouseMoveEvent(canvas::scene& canv, QGraphicsSceneMouseEvent* event) {
    QPointF pt = event->scenePos();
    if (is_dragging()) {
        do_dragging(canv, pt);
        return;
    }
    if (click_pt_ && distance(*click_pt_, pt) > 3.0) {
        do_dragging(canv, pt);
        return;
    }
}
void ui::tool::select::mouseReleaseEvent(canvas::scene& canv, QGraphicsSceneMouseEvent* event) {
    // A fast gesture may deliver few (or no) move events. Finish at the actual release point.
    if (click_pt_ && (is_dragging() || distance(*click_pt_, event->scenePos()) > 3.0)) {
        do_dragging(canv, event->scenePos());
    }
    bool shift_down = event->modifiers().testFlag(Qt::ShiftModifier);
    bool ctrl_down = event->modifiers().testFlag(Qt::ControlModifier);
    bool alt_down = event->modifiers().testFlag(Qt::AltModifier);
    if (is_dragging()) {
        handle_drag_complete(canv, shift_down, ctrl_down);
        destroy_rubber_band(canv, drag_->rubber_band);
        drag_ = {};
    }
    else {
        handle_click(canv, event->scenePos(), shift_down, ctrl_down, alt_down);
    }
    click_pt_ = {};
    canv.sync_to_model();
}

void ui::tool::select::handle_rotation(canvas::scene& c, QPointF pt, rotation_state& ri) {
    auto theta = sm::normalize_angle(
        sm::angle_from_u_to_v(ri.axis().world_pos(), from_qt_pt(pt))
    );
    ri.update_pointer_theta(theta);
    auto theta_diff = theta -
        sm::angle_from_u_to_v(ri.axis().world_pos(), ri.rotating().world_pos());
    switch (ri.mode()) {
    case sel_drag_mode::rigid:
        ri.bone().rotate_by(theta_diff, ri.axis(), false);
        break;
    case sel_drag_mode::unique:
        ri.bone().rotate_by(theta_diff, ri.axis(), true);
        break;
    case sel_drag_mode::rag_doll:
        do_ragdoll_rotate(theta, ri);
        break;
    }
    c.sync_to_model();
}
void ui::tool::select::handle_translation(canvas::scene& c, QPointF pt, translation_state& state) {
    auto delta = from_qt_pt(pt) - (state.anchor->world_pos() + state.anchor_offset);
    auto active_skeletons = skeletons_from_nodes(state.moving);

    switch (state.mode) {
    case sel_drag_mode::rigid: {
        auto translate = sm::translation_matrix(delta);
        for (auto skel : active_skeletons) {
            skel->apply(translate);
        }
    }
                             break;
    case sel_drag_mode::rubber_band: {
        for (auto skel : active_skeletons) {
            do_rubber_band_translate(skel->root_node(), delta, state.moving);
        }
    }
                                   break;
    case sel_drag_mode::rag_doll:
        for (auto skel : active_skeletons) {
            do_ragdoll_translate(
                skel,
                delta,
                state.moving,
                all_pinned_nodes(skel->root_node(), c)
            );
        }
        break;
    }
    c.sync_to_model();
}

void ui::tool::select::handle_click(
    canvas::scene& canv, QPointF pt, bool shift_down, bool ctrl_down, bool alt_down) {
    auto clicked_item = canv.top_item(pt);
    if (!clicked_item) {
        canv.clear_selection();
        return;
    }

    if (alt_down) {
        auto clicked_node = dynamic_cast<ui::canvas::item::node*>(clicked_item);
        if (!clicked_node) {
            return;
        }
        canv.toggle_node_pinned(clicked_node->model().id());
        return;
    }
    select_topology(canv, {&clicked_item, 1}, shift_down, ctrl_down);
}
void ui::tool::select::do_rotation_complete(canvas::scene& canv, const rotation_state& ri) {
    if (animation_authoring_) {
        if (animation_authoring_->complete) animation_authoring_->complete(authored_rotation_for(ri));
        canv.sync_selection();
        return;
    }
    const auto& new_locs = ri.current_node_locs();
    project_->transform_node_positions(
        ri.old_node_locs(),
        new_locs
    );
    canv.sync_selection();
}

void ui::tool::select::do_translation_complete(canvas::scene& canv, const translation_state& ri) {
    if (animation_authoring_) {
        for (const auto& [id, old_pos] : ri.old_locs)
            for (auto* node : canv.node_items()) if (node->model().id() == id) { node->model().set_world_pos(old_pos); break; }
        canv.sync_to_model();
        if (animation_authoring_->reject) animation_authoring_->reject("Translation action authoring is not enabled in this pass; use Rotate in the Selection settings.");
        return;
    }
    node_locs new_locs;
    bool changed = false;
    for (const auto& [id, old_pos] : ri.old_locs) {
        auto pos = std::get<sm::node_ref>(project_->get(id))->world_pos();
        new_locs.emplace_back(id, pos);
        changed = changed || sm::distance(pos, old_pos) > 0;
    }
    if (changed) project_->transform_node_positions(ri.old_locs, new_locs);
    canv.sync_selection();
}
void ui::tool::select::handle_drag_complete(canvas::scene& c, bool shift_down, bool alt_down) {
    switch (drag_->type) {
    case selection_rb:
        handle_select_drag(c, QRectF(*click_pt_, to_qt_pt(drag_->pt)), shift_down, alt_down);
        return;
    case rotation_rb:
        do_rotation_complete(c, std::get<rotation_state>(drag_->extra));
        return;
    case translation_rb:
        do_translation_complete(c, std::get<translation_state>(drag_->extra));
        return;
    }
}
void ui::tool::select::handle_select_drag(canvas::scene& canv, QRectF rect, bool shift_down, bool ctrl_down) {
    auto clicked_items = canv.items_in_rect(rect) | rv::filter([](auto* item) {
        return dynamic_cast<canvas::item::node*>(item) || dynamic_cast<canvas::item::bone*>(item);
    }) | r::to<std::vector<canvas::item::base*>>();
    select_topology(canv, clicked_items, shift_down, ctrl_down);
}
void ui::tool::select::deactivate(canvas::manager& canv_mgr) {
    if (animation_authoring_) cancel_animation_drag(canv_mgr.active_canvas());
    canv_mgr.set_drag_mode(ui::canvas::drag_mode::none);
}

QWidget* ui::tool::select::settings_widget() {
    if (!settings_panel_) {
        settings_panel_ = new select_tool_panel();
        settings_panel_->connect(&(settings_panel_->pin_button()), &QPushButton::clicked,
            [&]() {
                pin_selection();
            }
        );
    }
    return settings_panel_;
}

ui::tool::select::authored_rotation ui::tool::select::authored_rotation_for(const rotation_state& state) const {
    if (state.mode() == sel_drag_mode::rag_doll) {
        return sm::ik_rotation{state.rotating().id(), state.axis().id(), state.gesture_angle()};
    }
    const auto pivot = &state.axis() == &state.bone().parent_node() ?
        sm::rotation_pivot::root : sm::rotation_pivot::tip;
    const auto propagation = state.mode() == sel_drag_mode::unique ?
        sm::rotation_propagation::bone_only : sm::rotation_propagation::hierarchy;
    return sm::rigid_rotation{state.bone().id(), pivot, state.gesture_angle(), propagation};
}

void ui::tool::select::cancel_animation_drag(canvas::scene& canv) {
    if (drag_) {
        const node_locs* old = nullptr;
        if (auto* rotation = std::get_if<rotation_state>(&drag_->extra)) old = &rotation->old_node_locs();
        if (auto* translation = std::get_if<translation_state>(&drag_->extra)) old = &translation->old_locs;
        if (old) for (const auto& [id, pt] : *old)
            for (auto* node : canv.node_items()) if (node->model().id() == id) { node->model().set_world_pos(pt); break; }
        destroy_rubber_band(canv, drag_->rubber_band);
        drag_.reset();
        canv.sync_to_model();
    }
    click_pt_.reset();
    if (animation_authoring_ && animation_authoring_->cancel) animation_authoring_->cancel();
}

void ui::tool::select::set_animation_authoring(std::optional<animation_authoring> authoring) {
    if (animation_authoring_ && canvases_) cancel_animation_drag(canvases_->active_canvas());
    animation_authoring_ = std::move(authoring);
}

void ui::tool::select::keyPressEvent(canvas::scene& c, QKeyEvent* event) {
    if (animation_authoring_) {
        if (event->key() == Qt::Key_Escape) { cancel_animation_drag(c); return; }
        if (event->matches(QKeySequence::Undo)) { cancel_animation_drag(c); project_->undo(); return; }
        if (event->matches(QKeySequence::Redo)) { cancel_animation_drag(c); project_->redo(); return; }
    }
    base::keyPressEvent(c, event);
}
