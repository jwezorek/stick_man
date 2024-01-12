#include "selection_tool.h"
#include "select_tool_panel.h"
#include "../panes/skeleton_pane.h"
#include "../util.h"
#include "../canvas/scene.h"
#include "../canvas/skel_item.h"
#include "../canvas/node_item.h"
#include "../canvas/bone_item.h"
#include "../canvas/canvas_manager.h"
#include "../stick_man.h"
#include "../../core/sm_skeleton.h"
#include "../../core/sm_types.h"
#include <array>
#include <ranges>
#include <unordered_map>
#include <unordered_set>
#include <numbers>
#include <functional>

using namespace std::placeholders;
namespace r = std::ranges;
namespace rv = std::ranges::views;

/*------------------------------------------------------------------------------------------------*/

namespace {

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

		sm::dfs(**node_set.begin(), visit_node, visit_bone);
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
}

ui::tool::select::select() :
    settings_(nullptr),
    base("selection", "arrow_icon.png", ui::tool::id::selection) {
}

void ui::tool::select::init(canvas::manager& canvases, mdl::project& model) { /*
    canvases.connect(
        &canvases, &canvas::manager::rubber_band_change,
        [&](canvas::scene& canv, QRect rbr, QPointF from, QPointF to) {
            if (from != QPointF{ 0, 0 }) {
                rubber_band_ = points_to_rect(from, to);
            }
        }
    );
    */
}

void ui::tool::select::activate(canvas::manager& canv_mgr) {
    rubber_band_ = {};
}

void ui::tool::select::keyReleaseEvent(canvas::scene & c, QKeyEvent * event) {
}

void ui::tool::select::mousePressEvent(canvas::scene& canv, QGraphicsSceneMouseEvent* event) {
    rubber_band_ = {};
    click_pt_ = event->scenePos();
}

bool  ui::tool::select::is_dragging() const {
    return rubber_band_ != nullptr;
}

ui::canvas::item::rubber_band* ui::tool::select::rubber_band_from_drag_settings(canvas::scene& canv) {
    return nullptr;
}

void ui::tool::select::update_rubber_band(canvas::scene& canv, QPointF pt) {

}

void  ui::tool::select::do_dragging(canvas::scene& canv, QPointF pt) {

}

void ui::tool::select::mouseMoveEvent(canvas::scene& canv, QGraphicsSceneMouseEvent* event) {
    QPointF pt = event->scenePos();
    if (is_dragging()) {
        do_dragging(canv, pt);
        return;
    }
    if (click_pt_ && distance(*click_pt_, pt) <= 3.0 && settings_->has_drag_behavior()) {
         do_dragging(canv, pt);
         return;
    }
}

void ui::tool::select::mouseReleaseEvent(canvas::scene& canv, QGraphicsSceneMouseEvent* event) {
    click_pt_ = {};
    bool shift_down = event->modifiers().testFlag(Qt::ShiftModifier);
    bool ctrl_down = event->modifiers().testFlag(Qt::ControlModifier);

    if (is_dragging()) {
       //handle_drag(canv, rubber_band_, shift_down, ctrl_down);
    } else {
        handle_click(canv, event->scenePos(), shift_down, ctrl_down);
    }

	canv.sync_to_model();
}

void ui::tool::select::handle_click(canvas::scene& canv, QPointF pt, bool shift_down, bool ctrl_down) {
    auto clicked_item = canv.top_item(pt);
    if (!clicked_item) {
        canv.clear_selection();
        return;
    }

	deselect_skeleton(canv);

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

void ui::tool::select::handle_drag(canvas::scene& canv, QRectF rect, bool shift_down, bool ctrl_down) {
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
    if (!settings_) {
        settings_ = new select_tool_panel();
    }
    return settings_;
}