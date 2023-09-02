#include "selection_tool.h"
#include "skeleton_pane.h"
#include "util.h"
#include "canvas.h"
#include "canvas_item.h"
#include "stick_man.h"
#include "../core/sm_skeleton.h"
#include "../core/sm_types.h"
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

ui::selection_tool::selection_tool(tool_manager* mgr, ui::stick_man* main_wnd) :
	main_wnd_(*main_wnd),
    state_(modal_state::none),
    abstract_tool(mgr, "selection", "arrow_icon.png", ui::tool_id::selection) {

}

void ui::selection_tool::init() {
	auto& canv = main_wnd_.view().canvas();
	canv.connect(&canv, &canvas::selection_changed,
		[this, &canv]() {
			const auto& sel = canv.selection();
			this->handle_sel_changed(canv);
		}
	);
}

void ui::selection_tool::activate(canvas& canv) {
    auto& view = canv.view();
    view.setDragMode(QGraphicsView::RubberBandDrag);
    rubber_band_ = {};
    conn_ = view.connect(
        &view, &QGraphicsView::rubberBandChanged,
        [&](QRect rbr, QPointF from, QPointF to) {
            if (from != QPointF{ 0, 0 }) {
                rubber_band_ = points_to_rect(from, to);
            }
        }
    );
    state_ = modal_state::none;
}

void ui::selection_tool::keyReleaseEvent(canvas & c, QKeyEvent * event) {
    if (is_in_modal_state() && event->key() == Qt::Key_Escape) {
        set_modal(modal_state::none, c);
    }
}

void ui::selection_tool::mousePressEvent(canvas& canv, QGraphicsSceneMouseEvent* event) {
    rubber_band_ = {};
}

void ui::selection_tool::mouseMoveEvent(canvas& canv, QGraphicsSceneMouseEvent* event) {
}

void ui::selection_tool::mouseReleaseEvent(canvas& canv, QGraphicsSceneMouseEvent* event) {
    bool shift_down = event->modifiers().testFlag(Qt::ShiftModifier);
    bool ctrl_down = event->modifiers().testFlag(Qt::ControlModifier);
    if (rubber_band_) {
        handle_drag(canv, *rubber_band_, shift_down, ctrl_down);
    } else {
        handle_click(canv, event->scenePos(), shift_down, ctrl_down);
    }
	canv.sync_to_model();
}

void ui::selection_tool::handle_click(canvas& canv, QPointF pt, bool shift_down, bool ctrl_down) {
    auto clicked_item = canv.top_item(pt);
    if (!clicked_item) {
        canv.clear_selection();
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

void ui::selection_tool::handle_drag(canvas& canv, QRectF rect, bool shift_down, bool ctrl_down) {
    auto clicked_items = canv.items_in_rect(rect);
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

void ui::selection_tool::handle_sel_changed( const ui::canvas& canv) {
    auto type_of_sel = canv.selection_type();
	auto& props = main_wnd_.skel_pane().sel_properties();
    props.set(type_of_sel, canv);
}

void ui::selection_tool::set_modal(modal_state state, canvas& c) {
    static auto text = std::to_array<QString>({
        "",
        "Select angle of joint constraint",
        "Select anchor bone for joint constraint"
    });
    state_ = state;
    if (state == modal_state::none) {
        c.view().setDragMode(QGraphicsView::RubberBandDrag);
        c.hide_status_line();
        return;
    }
    c.view().setDragMode(QGraphicsView::NoDrag);
    c.show_status_line(text[static_cast<int>(state_)]);
}

bool ui::selection_tool::is_in_modal_state() const {
    return state_ != modal_state::none;
}

void ui::selection_tool::deactivate(canvas& canv) {
    canv.hide_status_line();
    auto& view = canv.view();
    view.setDragMode(QGraphicsView::NoDrag);
    view.disconnect(conn_);
}

QWidget* ui::selection_tool::settings_widget() {
	return new QWidget();
}