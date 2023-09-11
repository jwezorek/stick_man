#include "canvas.h"
#include "canvas_item.h"
#include "stick_man.h"
#include "tools.h"
#include "util.h"
#include "../core/sm_skeleton.h"
#include <boost/geometry.hpp>
#include <ranges>
#include <numbers>

namespace r = std::ranges;
namespace rv = std::ranges::views;

/*------------------------------------------------------------------------------------------------*/

namespace {

	const auto k_darker_gridline_color = QColor::fromRgb(180, 180, 180);
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
        QPen light_pen(k_light_gridline_color, 0);
        qreal x1, y1, x2, y2;
        r.getCoords(&x1, &y1, &x2, &y2);

        int left_gridline_index = static_cast<int>(std::ceil(x1 / line_spacing));
        int right_gridline_index = static_cast<int>(std::floor(x2 / line_spacing));
        for (auto i : rv::iota(left_gridline_index, right_gridline_index + 1)) {
            auto x = i * line_spacing;
            painter->setPen((i % 5) ? light_pen : dark_pen);
            painter->drawLine(x, y1, x, y2);
        }

        int top_gridline_index = static_cast<int>(std::ceil(y1 / line_spacing));
        int bottom_gridline_index = static_cast<int>(std::floor(y2 / line_spacing));
        for (auto i : rv::iota(top_gridline_index, bottom_gridline_index + 1)) {
            auto y = i * line_spacing;
            painter->setPen((i % 5) ? light_pen : dark_pen);
            painter->drawLine(x1, y, x2, y);
        }

		QPen darker_pen(k_darker_gridline_color, 0);
		painter->setPen(darker_pen);
		painter->drawLine(0, y1, 0, y2);
		painter->drawLine(x1, 0, x2, 0);
    }

    auto child_bones(ui::node_item* node) {
        auto bones = node->model().child_bones();
        return bones |
            rv::transform(
                [](sm::const_bone_ref bone)->ui::bone_item& {
                    return std::any_cast<std::reference_wrapper<ui::bone_item>>(
                        bone.get().get_user_data()
                    );
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

void ui::canvas::sync_to_model() {
	auto itms = items();
	if (itms.isEmpty()) { 
		//TODO
		/*
		auto& sandbox = view().main_window().sandbox();
		for (auto node : sandbox.nodes()) {
			addItem(new ui::node_item(node.get(), scale()));
		}
		for (auto bone : sandbox.bones()) {
			addItem(new ui::bone_item(bone.get(), scale()));
		}
		*/
	}
    auto is_non_null = [](auto* p) {return p; };
    for (auto* child : itms | rv::transform(to_stick_man) | rv::filter(is_non_null)) {
        child->sync_to_model();
    }
}

const ui::selection_set&  ui::canvas::selection() const {
	return selection_;
}

ui::skeleton_item* ui::canvas::selected_skeleton() const {
	auto skeletons = to_vector_of_type<ui::skeleton_item>(selection_);
	return (skeletons.size() == 1) ? skeletons.front() : nullptr;
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

ui::node_item* ui::canvas::insert_item(sm::node& node) {
	ui::node_item* ni;
	addItem(ni = new node_item(node, 1.0 / scale()));
	emit contents_changed();
	return ni;
}

ui::bone_item* ui::canvas::insert_item(sm::bone& bone) {
	ui::bone_item* bi;
	addItem(bi = new bone_item(bone, 1.0 / scale()));
	emit contents_changed();
	return bi;
}

ui::skeleton_item* ui::canvas::insert_item(sm::skeleton& skel) {
	ui::skeleton_item* si;
	addItem(si = new skeleton_item(skel, 1.0 / scale()));
	return si;
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

void ui::canvas::add_to_selection(std::span<ui::abstract_canvas_item*> itms, bool sync) {
    selection_.insert(itms.begin(), itms.end());
	if (sync) {
		sync_selection();
	}
}

void ui::canvas::add_to_selection(ui::abstract_canvas_item* itm, bool sync) {
    add_to_selection({ &itm,1 }, sync);
}

void ui::canvas::subtract_from_selection(std::span<ui::abstract_canvas_item*> itms, bool sync) {
    for (auto itm : itms) {
        selection_.erase(itm);
    }
	if (sync) {
		sync_selection();
	}
}

void ui::canvas::subtract_from_selection(ui::abstract_canvas_item* itm, bool sync) {
    subtract_from_selection({ &itm,1 }, sync);
}

void ui::canvas::set_selection(std::span<ui::abstract_canvas_item*> itms, bool sync) {
    selection_.clear();
    add_to_selection(itms,sync);
}

void ui::canvas::set_selection(ui::abstract_canvas_item* itm, bool sync) {
    set_selection({ &itm,1 },sync);
}

void ui::canvas::clear_selection() {
    selection_.clear();
    sync_selection();
}

void ui::canvas::clear() {
	QList<QGraphicsItem*> itemList = this->items();
	for (QGraphicsItem* item : itemList) {
		this->removeItem(item);
		delete item;
	}
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

void ui::canvas::filter_selection(std::function<bool(abstract_canvas_item*)> filter) {
	selection_set sel;
	for (auto* aci : selection_) {
		if (filter(aci)) {
			sel.insert(aci);
		} else {
			aci->set_selected(false);
		}
	}
	selection_ = sel;
}

void ui::canvas::delete_item(abstract_canvas_item* deletee, bool emit_signals) {
	bool was_selected = deletee->is_selected();
	if (was_selected) {
		filter_selection(
			[deletee](abstract_canvas_item* item)->bool {
				return item != deletee;
			}
		);
	}

	removeItem(deletee->item_body());
	removeItem(deletee->selection_frame());
	delete deletee;

	if (emit_signals) {
		if (was_selected) {
			emit selection_changed();
		}
		emit contents_changed();
	}
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

ui::canvas_view::canvas_view() : 
	main_window_(nullptr) {
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

std::optional<ui::model_variant> ui::selected_single_model(const ui::canvas& canv) {
	auto* skel_item = canv.selected_skeleton();
	if (skel_item) {
		return model_variant{ std::ref(skel_item->model()) };
	}
	auto bones = canv.selected_bones();
	auto nodes = canv.selected_nodes();
	if (bones.size() == 1 && nodes.empty()) {
		return model_variant{ std::ref(bones.front()->model()) };
	}
	if (nodes.size() == 1 && bones.empty()) {
		return model_variant{ std::ref(nodes.front()->model()) };
	}
	return {};
}