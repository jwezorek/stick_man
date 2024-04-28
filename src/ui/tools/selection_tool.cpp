#include "selection_tool.h"
#include "select_tool_panel.h"
#include "../panes/skeleton_pane.h"
#include "../util.h"
#include "../canvas/scene.h"
#include "../canvas/canvas_item.h"
#include "../canvas/skel_item.h"
#include "../canvas/node_item.h"
#include "../canvas/bone_item.h"
#include "../canvas/canvas_manager.h"
#include "../stick_man.h"
#include "../../core/sm_skeleton.h"
#include "../../core/sm_types.h"
#include "../../core/sm_visit.h"
#include "../../core/sm_fabrik.h"
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

    sm::maybe_bone_ref find_bone_from_u_to_v(sm::node_ref u, sm::node_ref v) {
        auto adj = u.get().adjacent_bones();
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
            auto& v = adj_bone.get().opposite_node(u);
            if (visited.contains(&v)) {
                return visited[&v] + 1;
            }
        }
        return -1;
    }

    std::unordered_set<sm::node*> all_pinned_nodes(sm::node& start) {
        using namespace ui::canvas;
        std::unordered_set<sm::node*> pinned;
        sm::visit_nodes_and_bones(
            start,
            [&pinned](sm::node& n)->sm::visit_result {
                if (item_from_model<item::node>(n).is_pinned()) {
                    pinned.insert(&n);
                }
                return sm::visit_result::continue_traversal;
            },
            [&pinned](sm::bone& b)->sm::visit_result {
                if (item_from_model<item::bone>(b).is_selected()) {
                    pinned.insert(&b.parent_node());
                    pinned.insert(&b.child_node());
                }
                return sm::visit_result::continue_traversal;
            }
        );
        return pinned;
    }

    bool has_pinned_nodes(mdl::skel_piece piece) {
        sm::node_ref start = std::visit(
            overload{
                [](sm::node_ref node)->sm::node_ref {return node; },
                [](sm::bone_ref bone)->sm::node_ref {return bone.get().parent_node(); },
                [](sm::skel_ref skel)->sm::node_ref {return skel.get().root_node(); }
            },
            piece
        );
        bool found = false;
        sm::visit_nodes( start.get(), 
            [&](sm::node& node)->sm::visit_result {
                if (ui::canvas::item_from_model<ui::canvas::item::node>(node).is_pinned()) {
                    found = true;
                    return sm::visit_result::terminate_traversal;
                }
                return sm::visit_result::continue_traversal;
            }, 
            false
        );
        return found;
    }

    std::tuple<sm::maybe_node_ref, int> find_closest_pinned_node(sm::node_ref start) {
        auto pinned_nodes = all_pinned_nodes(start.get());
        std::unordered_map<sm::node*, int> visited;
        std::unordered_set<sm::node*> candidates;
        sm::visit_nodes_and_bones(
            start.get(),
            [&](sm::node& n)->sm::visit_result {
                if (&n == &start.get()) {
                    visited[&n] = 0;
                    return sm::visit_result::continue_traversal;
                } else {
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
            {std::ref(**min)},
            visited.at(*min)
        };
    }

    using node_pair = std::tuple<sm::node_ref, sm::node_ref>;
    std::optional<node_pair> rot_info_for_rotate_on_pin(const mdl::skel_piece& model) {
        return std::visit(
            overload{
                [](sm::node_ref node)->std::optional<node_pair> {
                    auto [closest, dist] = find_closest_pinned_node(node);
                    if (!closest) {
                        return {};
                    }
                    return {{
                        *closest,
                        node
                    }};
                },
                [](sm::bone_ref bone)->std::optional<node_pair> {
                    using namespace ui::canvas;

                    auto& u = bone.get().parent_node();
                    auto& v = bone.get().child_node();
                    auto& item_u = item_from_model<item::node>(u);
                    auto& item_v = item_from_model<item::node>(v);

                    if (item_u.is_pinned() && item_v.is_pinned()) {
                        return { {u, v} };
                    }
                    auto [closest_to_u, u_dist] = find_closest_pinned_node(u);
                    auto [closest_to_v, v_dist] = find_closest_pinned_node(v);
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
        canv.removeItem(dynamic_cast<QGraphicsItem*>(rb));
        delete rb;
    }

	void deselect_skeleton(ui::canvas::scene& canv) {
		canv.filter_selection(
			[](ui::canvas::item::base* itm)->bool {
				return dynamic_cast<ui::canvas::item::skeleton*>(itm) == nullptr;
			}
		);
	}

	auto just_nodes_and_bones(std::span<ui::canvas::item::base*> itms) {
		return itms | rv::filter(
			[](auto* ptr)->bool {
				return dynamic_cast<ui::canvas::item::node*>(ptr) ||
					dynamic_cast<ui::canvas::item::bone*>(ptr);
			}
		);
	}

	template<typename T>
	auto items_to_model_set(auto abstract_items) {
		using U = typename T::model_type;
		return abstract_items | rv::transform(
			[](ui::canvas::item::base* aci)->U* {
				auto item_ptr = dynamic_cast<T*>(aci);
				if (!item_ptr) {
					return nullptr;
				}
				return &(item_ptr->model());
			}
		) | rv::filter(
			[](auto* ptr) { return ptr;  }
		) | r::to<std::unordered_set<U*>>();
	}

	std::optional<sm::skel_ref> as_skeleton(auto itms) {

		auto node_set = items_to_model_set<ui::canvas::item::node>(itms);
		auto bone_set = items_to_model_set<ui::canvas::item::bone>(itms);

		if (node_set.empty() || bone_set.empty()) {
			return {};
		}

		// mathematics!
		if (bone_set.size() != node_set.size() - 1) {
			return {};
		}

		bool possibly_a_skeleton = true;
		size_t bone_count = 0;
		size_t node_count = 0;

		auto visit_node = [&](sm::node& node)->sm::visit_result {
			++node_count;
			if (!node_set.contains(&node)) {
				possibly_a_skeleton = false;
				return sm::visit_result::terminate_traversal;
			}
			return sm::visit_result::continue_traversal;
			};

		auto visit_bone = [&](sm::bone& bone)->sm::visit_result {
			++bone_count;
			if (!bone_set.contains(&bone)) {
				possibly_a_skeleton = false;
				return sm::visit_result::terminate_traversal;
			}
			return sm::visit_result::continue_traversal;
			};

		sm::visit_nodes_and_bones(**node_set.begin(), visit_node, visit_bone);
		if (!possibly_a_skeleton) {
			return {};
		}

		if (node_set.size() == node_count && bone_set.size() == bone_count) {
			return (*node_set.begin())->owner();
		}

		return {};
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
        sm::point offset = state.radius() * sm::point( std::cos(theta), std::sin(theta) );
        auto new_loc = state.axis().world_pos() + offset;
        auto result = sm::perform_fabrik( state.rotating(), new_loc, state.axis());

        //TODO: do something with 'result' here...
    }
}

ui::tool::select::select() :
    settings_panel_(nullptr),
    project_(nullptr),
    base("selection", "arrow_icon.png", ui::tool::id::selection) {
}

void ui::tool::select::init(canvas::manager& canvases, mdl::project& model) { 
    project_ = &model;
    canvases_ = &canvases;
}

void ui::tool::select::activate(canvas::manager& canv_mgr) {
    drag_ = {};
}

void ui::tool::select::keyReleaseEvent(canvas::scene & c, QKeyEvent * event) {
}

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

                auto* rb = ::create_rubber_band<canvas::item::line_rubber_band>(canv, pt);
                return drag_state{
                    from_qt_pt(pt), rb, translation_rb, std::move(*state)
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
                arb->set_radius( ui::distance( axis_pt, rotating_pt ) );
                arb->set_from_theta( ui::angle_through_points(axis_pt, rotating_pt )  );

                return drag_state{ from_qt_pt(pt), arb, rotation_rb, std::move(*ri) };
            }
        }
    };
    
    return tbl.at(typ)();
}

void  ui::tool::select::do_dragging(canvas::scene& canv, QPointF pt) {
    auto rb_type = kind_of_rubber_band(canv, pt);
    if (!rb_type) {
        return;
    }
    if (!is_dragging()) {
        drag_ = create_drag_state( *rb_type, canv, pt);
    }
    if (is_dragging()) {
        drag_->pt =from_qt_pt(pt);
        drag_->rubber_band->handle_drag(pt);
        std::visit(
            overload{
                [](std::monostate) {
                },
                [&](rotation_state& ri) {
                    handle_rotation(canv, pt, ri);
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

    auto model = item->to_skeleton_piece();

    if (!settings.rotate_on_pinned_ || !has_pinned_nodes(model)) {
        auto parent_bone = std::visit(
            overload{
                [](sm::node_ref node)->sm::maybe_bone_ref {
                    return node.get().parent_bone();
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
    } else {
        auto nodes = rot_info_for_rotate_on_pin(model);
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
    return {};
}

void ui::tool::select::pin_selection() {
    auto& canvas = canvases_->active_canvas();
    auto nodes = canvas.selected_nodes();
    if (nodes.empty()) {
        return;
    }
    for (auto* selected_node_item : nodes) {
        auto pinned = selected_node_item->is_pinned();
        selected_node_item->set_pinned(!pinned);
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
    
    bool shift_down = event->modifiers().testFlag(Qt::ShiftModifier);
    bool ctrl_down = event->modifiers().testFlag(Qt::ControlModifier);
    bool alt_down = event->modifiers().testFlag(Qt::AltModifier);

    if (is_dragging()) {
        handle_drag_complete(canv, shift_down, ctrl_down);
        destroy_rubber_band(canv, drag_->rubber_band);
        drag_ = {};
    } else {
        handle_click(canv, event->scenePos(), shift_down, ctrl_down, alt_down);
    }
    click_pt_ = {};
	canv.sync_to_model();
}

void ui::tool::select::handle_rotation(canvas::scene& c, QPointF pt, rotation_state& ri) {

    auto theta = sm::normalize_angle(
        sm::angle_from_u_to_v(ri.axis().world_pos(), from_qt_pt(pt))
    );
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
}

void ui::tool::select::handle_click(
        canvas::scene& canv, QPointF pt, bool shift_down, bool ctrl_down, bool alt_down) {

    auto clicked_item = canv.top_item(pt);
    if (!clicked_item) {
        canv.clear_selection();
        return;
    }

	deselect_skeleton(canv);

    if (alt_down) {
        auto clicked_node = dynamic_cast<ui::canvas::item::node*>(clicked_item);
        if (!clicked_node) {
            return;
        }
        if (!clicked_node->is_pinned()) {
            clicked_node->set_pinned(true);
        } else {
            clicked_node->set_pinned(false);
        }
        return;
    }

    if (shift_down && !ctrl_down) {
        canv.add_to_selection(clicked_item, true);
        return;
    }

    if (ctrl_down && !shift_down) {
        canv.subtract_from_selection(clicked_item, true);
        return;
    }

    canv.set_selection(clicked_item, true);
}

void ui::tool::select::do_rotation_complete(const rotation_state& ri) {
    const auto& new_locs = ri.current_node_locs();
    project_->transform_node_positions(
        ri.old_node_locs(),
        new_locs
    );
}

void ui::tool::select::do_translation_complete(const translation_state& ri) {

}

void ui::tool::select::handle_drag_complete(canvas::scene& c, bool shift_down, bool alt_down) {
    switch (drag_->type) {
        case selection_rb:
            handle_select_drag(c, QRectF(*click_pt_, to_qt_pt(drag_->pt)), shift_down, alt_down);
            return;
        case rotation_rb:
            do_rotation_complete(std::get<rotation_state>(drag_->extra));
            return;
        case translation_rb:
            do_translation_complete(std::get<translation_state>(drag_->extra));
            return;
    }
}

void ui::tool::select::handle_select_drag(canvas::scene& canv, QRectF rect, bool shift_down, bool ctrl_down) {
    auto clicked_items = canv.items_in_rect(rect);
    if (clicked_items.empty()) {
        canv.clear_selection();
        return;
    }

	auto maybe_skeleton = as_skeleton(just_nodes_and_bones(clicked_items));
	if (maybe_skeleton) {
		ui::canvas::item::skeleton* skel_item = nullptr;
		if (maybe_skeleton->get().get_user_data().has_value()) {
			skel_item = &(canvas::item_from_model<canvas::item::skeleton>(maybe_skeleton->get()));
		} else {
			skel_item = canv.insert_item(maybe_skeleton->get());
		}
		canv.set_selection(skel_item, true);
		return;
	}

	clicked_items = just_nodes_and_bones(clicked_items) |
		r::to<std::vector<ui::canvas::item::base*>>();
	if (clicked_items.empty()) {
		canv.clear_selection();
		return;
	}

    if (shift_down && !ctrl_down) {
        canv.add_to_selection(clicked_items, true);
        return;
    }

    if (ctrl_down && !shift_down) {
        canv.subtract_from_selection(clicked_items, true);
        return;
    }

    canv.set_selection( clicked_items, true );
}

void ui::tool::select::deactivate(canvas::manager& canv_mgr) {
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
