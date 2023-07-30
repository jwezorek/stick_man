#include "canvas.h"
#include "canvas_item.h"
#include "stick_man.h"
#include "tools.h"
#include "util.h"
#include "../core/ik_sandbox.h"
#include <boost/geometry.hpp>
#include <ranges>
#include <numbers>

namespace r = std::ranges;
namespace rv = std::ranges::views;

/*------------------------------------------------------------------------------------------------*/

namespace {

    const auto k_dark_gridline_color = QColor::fromRgb(220, 220, 220);
    const auto k_light_gridline_color = QColor::fromRgb(240, 240, 240);
    constexpr int k_ribbon_height = 35;

    template<typename T>
    std::vector<ui::abstract_canvas_item*> to_stick_man_items(const T& collection) {
        return ui::to_vector_of_type<ui::abstract_canvas_item*>(collection);
    }

    template<typename T>
    T* top_item_of_type(const QGraphicsScene& scene, QPointF pt) {
        auto itms = scene.items(pt);
        auto itms_at_pt = itms |
            rv::transform(
                [](auto itm)->T* {return dynamic_cast<T*>(itm); }
        );
        auto first = r::find_if(itms_at_pt, [](auto ptr)->bool { return ptr; });
        if (first == itms_at_pt.end()) {
            return nullptr;
        }
        return *first;
    }

    void draw_ribbon(QPainter* painter, QRect rr, QString txt) {
        QRect ribbon_rect = rr;

        // cosmetic slop, ... idk.
        ribbon_rect.setLeft(rr.left() - 1);
        ribbon_rect.setWidth(rr.width() + 2);

        painter->fillRect(ribbon_rect, ui::k_accent_color);

        painter->setPen(Qt::white);
		auto fnt = painter->font();
		fnt.setWeight(QFont::Bold);
		painter->setFont(fnt);
        auto text_rect = ribbon_rect;
        text_rect.setRight(ribbon_rect.right() - 15);
        painter->drawText(text_rect, Qt::AlignRight | Qt::AlignVCenter, txt);

		painter->setPen(Qt::black);
        painter->drawLine(ribbon_rect.topLeft(), ribbon_rect.topRight());
    }

    ui::abstract_canvas_item* to_stick_man(QGraphicsItem* itm) {
        return dynamic_cast<ui::abstract_canvas_item*>(itm);
    }

    void draw_grid_lines(QPainter* painter, const QRectF& r, double line_spacing) {
        painter->fillRect(r, Qt::white);
        QPen dark_pen(k_dark_gridline_color, 0);
        QPen pen(k_light_gridline_color, 0);
        qreal x1, y1, x2, y2;
        r.getCoords(&x1, &y1, &x2, &y2);

        int left_gridline_index = static_cast<int>(std::ceil(x1 / line_spacing));
        int right_gridline_index = static_cast<int>(std::floor(x2 / line_spacing));
        for (auto i : rv::iota(left_gridline_index, right_gridline_index + 1)) {
            auto x = i * line_spacing;
            painter->setPen((i % 5) ? pen : dark_pen);
            painter->drawLine(x, y1, x, y2);
        }

        int top_gridline_index = static_cast<int>(std::ceil(y1 / line_spacing));
        int bottom_gridline_index = static_cast<int>(std::floor(y2 / line_spacing));
        for (auto i : rv::iota(top_gridline_index, bottom_gridline_index + 1)) {
            auto y = i * line_spacing;
            painter->setPen((i % 5) ? pen : dark_pen);
            painter->drawLine(x1, y, x2, y);
        }
    }

    auto child_bones(ui::node_item* node) {
        auto bones = node->model().child_bones();
        return bones |
            rv::transform(
                [](sm::const_bone_ref bone)->ui::bone_item& {
                    return std::any_cast<std::reference_wrapper<ui::bone_item>>(
                        bone.get().get_user_data()
                    ).get();
                }
        );
    }

	sm::node* find_shared_node(sm::bone* b1, sm::bone* b2) {
		std::array< sm::node*, 4> nodes = { {
			&(b1->parent_node()),
			&(b1->child_node()),
			&(b2->parent_node()),
			&(b2->child_node()),
		} };
		r::sort(nodes);
		auto adj_dup = r::adjacent_find(nodes, [](auto n1, auto n2) {return n1 == n2; });
		return (adj_dup != nodes.end()) ? *adj_dup : nullptr;
	}

	sm::bone* parent_bone(sm::bone* bone_1, sm::bone* bone_2) {
		return (&(bone_1->parent_node()) == find_shared_node(bone_1, bone_2)) ? bone_2 : bone_1;
	}
}
/*------------------------------------------------------------------------------------------------*/

ui::canvas::canvas(){
    setSceneRect(QRectF(-1500, -1500, 3000, 3000));
    
}

void ui::canvas::drawBackground(QPainter* painter, const QRectF& dirty_rect) {
    painter->fillRect(dirty_rect, QColor::fromRgb(53,53,53));
    painter->setRenderHint(QPainter::Antialiasing, true);
    auto scene_rect = sceneRect();
    auto rect = dirty_rect.intersected(scene_rect);

    draw_grid_lines(painter, rect, k_grid_line_spacing);
}

QRect client_rectangle(const QGraphicsView* view) {
    auto view_rect = view->contentsRect();
    if (view->verticalScrollBar()->isVisible()) {
        auto wd = view_rect.width();
        view_rect.setWidth(wd - view->verticalScrollBar()->width());
    }
    if (view->horizontalScrollBar()->isVisible()) {
        auto hgt = view_rect.height();
        view_rect.setHeight(hgt - view->horizontalScrollBar()->height());
    }
    return view_rect;
}

void ui::canvas::drawForeground(QPainter* painter, const QRectF& rect) {
    QGraphicsScene::drawForeground(painter, rect);

    if (is_status_line_visible()) {
        QGraphicsView* view = views().first();
        if (!view) {
            return;
        }

        painter->setTransform(QTransform());

        auto view_rect = client_rectangle(view);
        QRect ribbon_rect = {
            view_rect.bottomLeft() - QPoint(0, 35),
            QSize(view_rect.width(), 35)
        };
        
        draw_ribbon(painter, ribbon_rect, status_line_);

        painter->resetTransform();
    }
}

void ui::canvas::focusOutEvent(QFocusEvent* focusEvent) {
    if (is_status_line_visible()) {
        hide_status_line();
    }
}

ui::tool_manager& ui::canvas::tool_mgr() {
    return view().main_window().tool_mgr();
}


void ui::canvas::set_scale(double scale, std::optional<QPointF> center) {
    auto& view = this->view();
    view.resetTransform();
    view.scale(scale, -scale);
    if (center) {
        view.centerOn(*center);
    }
    sync_to_model();
}

double ui::canvas::scale() const {
    auto& view = this->view();
    return view.transform().m11();
}

void ui::canvas::sync_joint_constraint_to_model() {
	auto existing_jcis = to_vector_of_type<joint_constraint_adornment>(items());
	joint_constraint_adornment* jci = !existing_jcis.empty() ?
		existing_jcis.front() :
		nullptr;

	auto sel_joint = selected_joint();
	if (!sel_joint) {
		if (jci) {
			jci->setVisible(false);
		}
		return;
	}
	auto sel_constraint = sel_joint->shared_node->constraint_angles(
			*sel_joint->anchor_bone, *sel_joint->dependent_bone
	);
	if (!sel_constraint) {
		if (jci) {
			jci->setVisible(false);
		}
		return;
	}
	
	jci = (!jci) ? new joint_constraint_adornment() : jci;
	auto parent_rot = sel_joint->anchor_bone->world_rotation();
	auto span = sel_constraint->max - sel_constraint->min;
	auto min_angle = normalize_angle(parent_rot + sel_constraint->min);
	auto max_angle = min_angle + span;

	jci->set(sel_joint->shared_node, min_angle, max_angle, scale_ );
	if (existing_jcis.empty()) {
		addItem(jci);
	}
	jci->setVisible(true);
}

void ui::canvas::sync_to_model() {
    auto is_non_null = [](auto* p) {return p; };
    for (auto* child : items() | rv::transform(to_stick_man) | rv::filter(is_non_null)) {
        child->sync_to_model();
    }
	sync_joint_constraint_to_model();
}

const ui::selection_set&  ui::canvas::selection() const {
	return selection_;
}

ui::sel_type ui::canvas::selection_type() const {
	const auto& sel = selection_;

	if (sel.empty()) {
		return sel_type::none;
	}
	auto joint = selected_joint();
	if (joint) {
		return joint->joint_type;
	}
	bool has_node = false;
	bool has_bone = false;
	for (auto itm_ptr : sel) {
		has_node = dynamic_cast<ui::node_item*>(itm_ptr) || has_node;
		has_bone = dynamic_cast<ui::bone_item*>(itm_ptr) || has_bone;
		if (has_node && has_bone) {
			return sel_type::mixed;
		}
	}
	bool multi = sel.size() > 1;
	if (has_node) {
		return multi ? sel_type::nodes : sel_type::node;
	}
	else {
		return multi ? sel_type::bones : sel_type::bone;
	}
}

std::optional<ui::joint_info> ui::canvas::selected_joint() const {
	const auto& sel = selection_;
	if (sel.size() != 3) {
		return {};
	}
	auto bones = to_models_of_item_type<ui::bone_item>(sel);
	if (bones.size() != 2) {
		return {};
	}
	auto shared_node = find_shared_node(bones[0], bones[1]);
	if (!shared_node) {
		return {};
	}
	auto nodes = to_models_of_item_type<ui::node_item>(sel);
	if (nodes.size() != 1) {
		return {};
	}

	joint_info ji;
	ji.shared_node = shared_node;
	ji.anchor_bone = parent_bone(bones[0], bones[1]);
	ji.dependent_bone = (ji.anchor_bone == bones[0]) ? bones[1] : bones[0];
	auto* parent_1 = &(ji.anchor_bone->parent_node());
	auto* parent_2 = &(ji.dependent_bone->parent_node());

	ji.joint_type = (shared_node == parent_1 && shared_node == parent_2) ?
		sel_type::sibling_joint :
		sel_type::parent_child_joint;

	return ji;
}

std::vector<ui::bone_item*> ui::canvas::selected_bones() const {
    return to_vector_of_type<ui::bone_item>(selection_);
}

std::vector<ui::node_item*> ui::canvas::selected_nodes() const {
    return to_vector_of_type<ui::node_item>(selection_);
}

bool ui::canvas::is_status_line_visible() const {
    return !status_line_.isEmpty();
}

void ui::canvas::transform_selection(item_transform trans) {
    r::for_each(selection_, trans);
}

void ui::canvas::transform_selection(node_transform trans) {
    r::for_each(as_range_view_of_type<node_item>(selection_), trans);
}

void ui::canvas::transform_selection(bone_transform trans) {
    r::for_each(as_range_view_of_type<bone_item>(selection_), trans);
}

void ui::canvas::add_to_selection(std::span<ui::abstract_canvas_item*> itms) {
    selection_.insert(itms.begin(), itms.end());
    sync_selection();
}

void ui::canvas::add_to_selection(ui::abstract_canvas_item* itm) {
    add_to_selection({ &itm,1 });
}

void ui::canvas::subtract_from_selection(std::span<ui::abstract_canvas_item*> itms) {
    for (auto itm : itms) {
        selection_.erase(itm);
    }
    sync_selection();
}

void ui::canvas::subtract_from_selection(ui::abstract_canvas_item* itm) {
    subtract_from_selection({ &itm,1 });
}

void ui::canvas::set_selection(std::span<ui::abstract_canvas_item*> itms) {
    selection_.clear();
    add_to_selection(itms);
}

void ui::canvas::set_selection(ui::abstract_canvas_item* itm) {
    set_selection({ &itm,1 });
}

void ui::canvas::clear_selection() {
    selection_.clear();
    sync_selection();
}

void ui::canvas::sync_selection() {
    auto itms = items();
    for (auto* itm : as_range_view_of_type<ui::abstract_canvas_item>(itms)) {
        bool selected = selection_.contains(itm);
        itm->set_selected(selected);
    }
    emit selection_changed();
}

void ui::canvas::show_status_line(const QString& txt) {
    status_line_ = txt;
    update();
    setFocus();
}

void ui::canvas::hide_status_line() {
    status_line_.clear();
    update();
}

/*------------------------------------------------------------------------------------------------*/

ui::canvas_view& ui::canvas::view() const {
    return *static_cast<ui::canvas_view*>(this->views()[0]);
}

ui::node_item* ui::canvas::top_node(const QPointF& pt) const {
    return top_item_of_type<ui::node_item>(*this, pt);
}

ui::abstract_canvas_item* ui::canvas::top_item(const QPointF& pt) const {
    return top_item_of_type<ui::abstract_canvas_item>(*this, pt);
}

std::vector<ui::abstract_canvas_item*> ui::canvas::items_in_rect(const QRectF& r) const {
    return to_vector_of_type<ui::abstract_canvas_item>(items(r));
}

std::vector<ui::node_item*> ui::canvas::root_node_items() const {
    auto nodes = node_items();
    return nodes |
        rv::filter(
            [](auto j)->bool {
                return !(j->parentItem());
            }
        ) | r::to< std::vector<ui::node_item*>>();
}

std::vector<ui::node_item*> ui::canvas::node_items() const {
    return to_vector_of_type<node_item>(items());
}

std::vector<ui::bone_item*> ui::canvas::bone_items() const {
    return to_vector_of_type<bone_item>(items());
}

void ui::canvas::keyPressEvent(QKeyEvent* event) {
    tool_mgr().keyPressEvent(*this, event);
}

void ui::canvas::keyReleaseEvent(QKeyEvent* event) {
    tool_mgr().keyReleaseEvent(*this, event);
}

void ui::canvas::mousePressEvent(QGraphicsSceneMouseEvent* event) {
    tool_mgr().mousePressEvent(*this, event);
}

void ui::canvas::mouseMoveEvent(QGraphicsSceneMouseEvent* event) {
    tool_mgr().mouseMoveEvent(*this, event);
}

void ui::canvas::mouseReleaseEvent(QGraphicsSceneMouseEvent* event) {
    tool_mgr().mouseReleaseEvent(*this, event);
}

void ui::canvas::mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) {
    tool_mgr().mouseDoubleClickEvent(*this, event);
}

void ui::canvas::wheelEvent(QGraphicsSceneWheelEvent* event) {
    tool_mgr().wheelEvent(*this, event);
}

/*------------------------------------------------------------------------------------------------*/

ui::canvas_view::canvas_view() {
    setRenderHint(QPainter::Antialiasing, true);
    setViewportUpdateMode(QGraphicsView::FullViewportUpdate);
    setScene(new ui::canvas()); 
    scale(1, -1);
}

ui::canvas& ui::canvas_view::canvas() {
    return *static_cast<ui::canvas*>(this->scene());
}

ui::stick_man& ui::canvas_view::main_window() {
    return *static_cast<stick_man*>( parentWidget() );
}
