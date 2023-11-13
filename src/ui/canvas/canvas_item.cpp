#include "canvas.h"
#include "canvas_item.h"
#include "../util.h"
#include "../../core/sm_skeleton.h"

namespace r = std::ranges;
namespace rv = std::ranges::views;

/*------------------------------------------------------------------------------------------------*/

ui::canvas::canvas_item::canvas_item() : selection_frame_(nullptr)
{}

void ui::canvas::canvas_item::sync_to_model() {
	sync_item_to_model();
	if (!is_selection_frame_only()) {
		if (selection_frame_) {
			sync_sel_frame_to_model();
		}
	}
}

ui::canvas::canvas* ui::canvas::canvas_item::canvas() const {
	auto* ptr = dynamic_cast<const QGraphicsItem*>(this);
	return static_cast<ui::canvas::canvas*>(ptr->scene());
}

bool ui::canvas::canvas_item::is_selected() const {
	if (!is_selection_frame_only()) {
		return selection_frame_ && selection_frame_->isVisible();
	} else {
		return item_body()->isVisible();
	}
}

void ui::canvas::canvas_item::set_selected(bool selected) {
	if (!is_selection_frame_only()) {
		if (selected) {
			if (!selection_frame_) {
				selection_frame_ = create_selection_frame();
				selection_frame_->setParentItem(dynamic_cast<QGraphicsItem*>(this));
			}
			selection_frame_->show();
		}
		else {
			if (selection_frame_) {
				selection_frame_->hide();
			}
		}
	} else {
		sync_to_model();
		item_body()->setVisible(selected);
	}
}

mdl::skel_piece ui::canvas::canvas_item::to_skeleton_piece() {
    return std::visit(
        []<typename T>(std::reference_wrapper<const T> p)->mdl::skel_piece {
            return const_cast<T&>(p.get());
        },
        const_cast<const canvas_item*>(this)->to_skeleton_piece()
    );
}

ui::canvas::canvas_item::~canvas_item() {
	if (selection_frame_) {
		delete selection_frame_;
	}
}

const QGraphicsItem* ui::canvas::canvas_item::item_body() const {
	auto* nonconst_this = const_cast<canvas_item*>(this);
	return const_cast<const QGraphicsItem*>(nonconst_this->item_body());
}

QGraphicsItem* ui::canvas::canvas_item::selection_frame() {
	return selection_frame_;
}

/*------------------------------------------------------------------------------------------------*/

ui::canvas::has_treeview_item::has_treeview_item() :
	tree_view_item_(nullptr) {
}

void ui::canvas::has_treeview_item::set_treeview_item(QStandardItem* itm) {
	tree_view_item_ = itm;
}

QStandardItem* ui::canvas::has_treeview_item::treeview_item() const {
	return tree_view_item_;
}


