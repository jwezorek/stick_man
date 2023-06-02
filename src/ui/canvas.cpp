#include "canvas.h"
#include "stick_man.h"
#include "tools.h"
#include "../core/ik_sandbox.h"
#include <boost/geometry.hpp>
#include <ranges>
#include <numbers>

namespace r = std::ranges;
namespace rv = std::ranges::views;

/*------------------------------------------------------------------------------------------------*/

namespace {

    const auto k_sel_color = QColorConstants::Svg::turquoise;
    const auto k_dark_gridline_color = QColor::fromRgb(220, 220, 220);
    const auto k_light_gridline_color = QColor::fromRgb(240, 240, 240);
    constexpr double k_sel_thickness = 3.0;
    constexpr double k_node_radius = 8.0;
    constexpr double k_pin_radius = k_node_radius - 3.0;
    constexpr double k_sel_frame_distance = 4.0;
    constexpr int k_node_zorder = 10;
    constexpr int k_bone_zorder = 5;

    template<typename T, typename U>
    auto as_range_view_of_type(const U& collection) {
        return collection |
            rv::transform(
                [](auto itm)->T* {  return dynamic_cast<T*>(itm); }
            ) | rv::filter(
                [](T* ptr)->bool { return ptr;  }
        );
    }

    template<typename T, typename U> 
    std::vector<T*> to_vector_of_type(const U& collection) {
        return as_range_view_of_type<T>(collection) | r::to<std::vector<T*>>();
    }

    template<typename T>
    std::vector<ui::abstract_stick_man_item*> to_stick_man_items(const T& collection) {
        return to_vector_of_type< ui::abstract_stick_man_item*>(collection);
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

    ui::abstract_stick_man_item* to_stick_man(QGraphicsItem* itm) {
        return dynamic_cast<ui::abstract_stick_man_item*>(itm);
    }

    double radians_to_degrees(double radians) {
        return radians * (180.0 / std::numbers::pi_v<double>);
    }

    QPointF to_qt_pt(const sm::point& pt) {
        return { pt.x, pt.y };
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

    QPolygonF bone_polygon(double length, double node_radius, double scale) {
        auto r = static_cast<float>(node_radius * scale);
        auto d = static_cast<float>(length);
        auto x1 = (r * r) / d;
        auto y1 = r * std::sqrt(1.0f - (r * r) / (d * d));
        QList<QPointF> pts = {
            {0,0}, {x1, y1 }, {d, 0}, {x1, -y1}, {0,0}
        };
        return { pts };
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

    void set_circle(QGraphicsEllipseItem* ei, QPointF pos, double radius, double scale) {
        ei->setPos(0, 0);
        double r = radius * scale;
        ei->setRect(
            QRectF(
                -r, -r, 2.0 * r, 2.0 * r
            )
        );
        ei->setPos(pos);
    }

    void set_bone_item_pos(
            ui::bone_item* itm, double len, const 
            sm::point& pos, double rot, double scale) {
        itm->setPos(0, 0);
        itm->setRotation(0);
        itm->setPolygon(bone_polygon(len, k_node_radius, scale));
        itm->setRotation(radians_to_degrees(rot));
        itm->setPos(to_qt_pt(pos));
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
    auto is_non_null = [](auto* p) {return p; };
    for (auto* child : items() | rv::transform(to_stick_man) | rv::filter(is_non_null)) {
        child->sync_to_model();
    }
}

std::vector<ui::abstract_stick_man_item*> ui::canvas::selection() const {
    return selection_ | r::to<std::vector<ui::abstract_stick_man_item*>>();
}

void ui::canvas::add_to_selection(std::span<ui::abstract_stick_man_item*> itms) {
    selection_.insert(itms.begin(), itms.end());
    sync_selection();
}

void ui::canvas::add_to_selection(ui::abstract_stick_man_item* itm) {
    add_to_selection({ &itm,1 });
}

void ui::canvas::subtract_from_selection(std::span<ui::abstract_stick_man_item*> itms) {
    for (auto itm : itms) {
        selection_.erase(itm);
    }
    sync_selection();
}

void ui::canvas::subtract_from_selection(ui::abstract_stick_man_item* itm) {
    subtract_from_selection({ &itm,1 });
}

void ui::canvas::set_selection(std::span<ui::abstract_stick_man_item*> itms) {
    selection_.clear();
    add_to_selection(itms);
}

void ui::canvas::set_selection(ui::abstract_stick_man_item* itm) {
    set_selection({ &itm,1 });
}

void ui::canvas::clear_selection() {
    selection_.clear();
    sync_selection();
}

void ui::canvas::sync_selection() {
    auto itms = items();
    for (auto* itm : as_range_view_of_type<ui::abstract_stick_man_item>(itms)) {
        bool selected = selection_.contains(itm);
        itm->set_selected(selected);
    }
    emit selection_changed(selection_);
}

/*------------------------------------------------------------------------------------------------*/

ui::canvas_view& ui::canvas::view() const {
    return *static_cast<ui::canvas_view*>(this->views()[0]);
}

ui::node_item* ui::canvas::top_node(const QPointF& pt) const {
    return top_item_of_type<ui::node_item>(*this, pt);
}

ui::abstract_stick_man_item* ui::canvas::top_item(const QPointF& pt) const {
    return top_item_of_type<ui::abstract_stick_man_item>(*this, pt);
}

std::vector<ui::abstract_stick_man_item*> ui::canvas::items_in_rect(const QRectF& r) const {
    return to_vector_of_type<ui::abstract_stick_man_item>(items(r));
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

/*------------------------------------------------------------------------------------------------*/
ui::abstract_stick_man_item::abstract_stick_man_item() : selection_frame_(nullptr)
{}

void ui::abstract_stick_man_item::sync_to_model() {
    sync_item_to_model();
    if (selection_frame_) {
        sync_sel_frame_to_model();
    }
}

ui::canvas* ui::abstract_stick_man_item::canvas() const {
    auto* ptr = dynamic_cast<const QGraphicsItem*>(this);
    return static_cast<ui::canvas*>(ptr->scene());
}

bool ui::abstract_stick_man_item::is_selected() const {
    return selection_frame_ && selection_frame_->isVisible();
}

void ui::abstract_stick_man_item::set_selected(bool selected) {
    if (selected) {
        if (!selection_frame_) {
            selection_frame_ = create_selection_frame();
            selection_frame_->setParentItem(dynamic_cast<QGraphicsItem*>(this));
        }
        selection_frame_->show();
    } else {
        if (selection_frame_) {
            selection_frame_->hide();
        }
    }
}

/*------------------------------------------------------------------------------------------------*/

ui::node_item::node_item(sm::node& node, double scale) :
        has_stick_man_model<ui::node_item, sm::node&>(node),
        pin_(nullptr) {
    auto inv_scale = 1.0 / scale;
    setBrush(Qt::white);
    setPen(QPen(Qt::black, 2.0 * inv_scale));
    set_circle(this, to_qt_pt(model_.world_pos()), k_node_radius, inv_scale);
    setZValue(k_node_zorder);
}

void ui::node_item::set_pinned(bool pinned) {
    if (!pin_) {
        pin_ = new QGraphicsEllipseItem();
        set_circle(pin_, { 0,0 }, k_pin_radius, 1.0 / canvas()->scale());
        pin_->setPen(Qt::NoPen);
        pin_->setBrush(Qt::black);
        pin_->setParentItem(this);
    }
    if (pinned) {
        pin_->show();
    } else {
        pin_->hide();
    }
    model_.set_pinned(pinned);
}

void ui::node_item::sync_item_to_model() {
    auto& canv = *canvas();
    double inv_scale = 1.0 / canv.scale();
    setPen(QPen(Qt::black, 2.0 * inv_scale));
    set_circle(this, to_qt_pt(model_.world_pos()), k_node_radius, inv_scale);
    if (pin_) {
        set_circle(pin_, { 0,0 }, k_pin_radius, inv_scale);
    }
}

void ui::node_item::sync_sel_frame_to_model() {
    auto* sf = static_cast<QGraphicsEllipseItem*>(selection_frame_);
    auto inv_scale = 1.0 / canvas()->scale();
    set_circle( sf, { 0,0}, k_node_radius + k_sel_frame_distance, inv_scale);
    sf->setPen(QPen(k_sel_color, k_sel_thickness * inv_scale, Qt::DotLine));
}

QGraphicsItem* ui::node_item::create_selection_frame() const {
    auto& canv = *canvas();
    auto inv_scale = 1.0 / canvas()->scale();
    auto sf = new QGraphicsEllipseItem();
    set_circle( sf, { 0.0,0.0 }, k_node_radius + k_sel_frame_distance, inv_scale );
    sf->setPen(QPen(k_sel_color, k_sel_thickness * inv_scale, Qt::DotLine));
    sf->setBrush(Qt::NoBrush);
    return sf;
}

/*------------------------------------------------------------------------------------------------*/

ui::bone_item::bone_item(sm::bone& bone, double scale) : 
        has_stick_man_model<ui::bone_item, sm::bone&>(bone) {
    setBrush(Qt::black);
    setPen(QPen(Qt::black, 1.0/scale));
    set_bone_item_pos(
        this,
        bone.scaled_length(),
        bone.parent_node().world_pos(),
        bone.world_rotation(),
        1.0 / scale
    );
    setZValue(k_bone_zorder);
}

ui::node_item&  ui::bone_item::parent_node_item() const {
    auto& parent_node = std::any_cast<std::reference_wrapper<ui::node_item>>(
        model_.parent_node().get_user_data()
    ).get();
    return parent_node;
}

ui::node_item& ui::bone_item::child_node_item() const {
    auto& child_node = std::any_cast<std::reference_wrapper<ui::node_item>>(
        model_.child_node().get_user_data()
    ).get();
    return child_node;
}

void ui::bone_item::sync_item_to_model() {
    auto& canv = *canvas();
    setPen(QPen(Qt::black, 1.0 / canv.scale()));
    set_bone_item_pos(
        this,
        model_.scaled_length(),
        model_.parent_node().world_pos(),
        model_.world_rotation(),
        1.0 / canv.scale()
    );
}

void ui::bone_item::sync_sel_frame_to_model() {
    auto* sf = static_cast<QGraphicsLineItem*>(selection_frame_);
    auto inv_scale = 1.0 / canvas()->scale();
    sf->setLine(0, 0, model_.length(), 0);
    sf->setPen(QPen(k_sel_color, k_sel_thickness * inv_scale, Qt::DotLine));
}

QGraphicsItem* ui::bone_item::create_selection_frame() const {
    auto& canv = *canvas();
    auto inv_scale = 1.0 / canvas()->scale();
    auto sf = new QGraphicsLineItem();
    sf->setLine(0, 0, model_.length(), 0);
    sf->setPen(QPen(k_sel_color, k_sel_thickness * inv_scale, Qt::DotLine));
    return sf;
}