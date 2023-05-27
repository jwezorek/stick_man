#include "tools.h"
#include "move_tool.h"
#include "stick_man.h"
#include "tool_settings_pane.h"
#include "util.h"
#include <QGraphicsScene>
#include <array>
#include <functional>
#include <ranges>

namespace r = std::ranges;
namespace rv = std::ranges::views;

/*------------------------------------------------------------------------------------------------*/

namespace {

    auto k_tool_registry = std::to_array<std::unique_ptr<ui::abstract_tool>>({
        std::make_unique<ui::pan_tool>(),
        std::make_unique<ui::zoom_tool>(),
        std::make_unique<ui::arrow_tool>(),
        std::make_unique<ui::move_tool>(),
        std::make_unique<ui::add_joint_tool>(),
        std::make_unique<ui::add_bone_tool>()
    });

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

/*------------------------------------------------------------------------------------------------*/

ui::abstract_tool::abstract_tool(QString name, QString rsrc, tool_id id) : 
    name_(name),
    rsrc_(rsrc),
    id_(id) {

}

ui::tool_id ui::abstract_tool::id() const {
    return id_;
}

QString ui::abstract_tool::name() const {
    return name_;
}

QString ui::abstract_tool::icon_rsrc() const {
    return rsrc_;
}

void ui::abstract_tool::populate_settings(tool_settings_pane* pane) {
    pane->set_tool(name_, settings_widget());
}

void ui::abstract_tool::activate(canvas& c) {}
void ui::abstract_tool::keyPressEvent(canvas& c, QKeyEvent* event) {}
void ui::abstract_tool::keyReleaseEvent(canvas& c, QKeyEvent* event) {}
void ui::abstract_tool::mousePressEvent(canvas& c, QGraphicsSceneMouseEvent* event) {}
void ui::abstract_tool::mouseMoveEvent(canvas& c, QGraphicsSceneMouseEvent* event) {}
void ui::abstract_tool::mouseReleaseEvent(canvas& c, QGraphicsSceneMouseEvent* event) {}
void ui::abstract_tool::mouseDoubleClickEvent(canvas& c, QGraphicsSceneMouseEvent* event) {}
void ui::abstract_tool::wheelEvent(canvas& c, QGraphicsSceneWheelEvent* event) {}
void ui::abstract_tool::deactivate(canvas& c) {}
QWidget* ui::abstract_tool::settings_widget() { return nullptr; }

/*------------------------------------------------------------------------------------------------*/

ui::zoom_tool::zoom_tool() : 
        abstract_tool("zoom", "zoom_icon.png", ui::tool_id::zoom),
        zoom_level_(0),
        settings_(nullptr) {
}

qreal ui::zoom_tool::scale_from_zoom_level(int zoom_level)  {
    if (zoom_level == 0) {
        return 1.0;
    }
    if (zoom_level > 0) {
        qreal zoom = static_cast<qreal>(zoom_level);
        return zoom + 1.0;
    }
    qreal zoom = -1.0 * zoom_level;
    return 1.0 / (zoom + 1.0);
}

void ui::zoom_tool::mouseReleaseEvent(canvas& c, QGraphicsSceneMouseEvent* event) {
    if (! event->modifiers().testFlag(Qt::AltModifier)) {
        zoom_level_++;
    } else {
        zoom_level_--;
    }
    auto pt = event->scenePos();
    auto zoom = scale_from_zoom_level(zoom_level_);
    c.set_scale(zoom, pt);
}

QWidget* ui::zoom_tool::settings_widget() {
    if (!settings_) {
        settings_ = new QWidget();
        auto* flow = new ui::FlowLayout(settings_);
        flow->addWidget(new QLabel("magnify"));
        flow->addWidget(magnify_ = new QComboBox());
        for (int zoom_level = -4; zoom_level <= 4; zoom_level++) {
            int val = static_cast<int>(scale_from_zoom_level(zoom_level) * 100.0);
            QString txt = QString::number(val) + "%";
            magnify_->addItem(txt, zoom_level);
        }
    }
    return settings_;
}

/*------------------------------------------------------------------------------------------------*/

ui::pan_tool::pan_tool() : abstract_tool("pan", "pan_icon.png", ui::tool_id::pan) {}

void ui::pan_tool::deactivate(canvas& c) {
    c.view().setDragMode(QGraphicsView::NoDrag);
}

void ui::pan_tool::activate(canvas& c) {
    c.view().setDragMode(QGraphicsView::ScrollHandDrag);
}

/*------------------------------------------------------------------------------------------------*/

ui::add_joint_tool::add_joint_tool() : 
    abstract_tool("add joint", "add_joint_icon.png", ui::tool_id::add_joint) {}

void ui::add_joint_tool::mouseReleaseEvent(canvas& canv, QGraphicsSceneMouseEvent* event) {
    auto pt = event->scenePos();
    auto& sandbox = canv.view().main_window().sandbox();
    auto j = sandbox.create_joint({}, pt.x(), pt.y());

    auto item = new ui::joint_item(j->get(), canv.scale());
    canv.addItem(item);
}

/*------------------------------------------------------------------------------------------------*/

ui::add_bone_tool::add_bone_tool() :
    rubber_band_(nullptr),
    abstract_tool("add bone", "add_bone_icon.png", ui::tool_id::add_bone) {
}

void ui::add_bone_tool::mousePressEvent(canvas& c, QGraphicsSceneMouseEvent* event) {
    origin_ = event->scenePos();
    if (!rubber_band_) {
        c.addItem(rubber_band_ = new QGraphicsLineItem());
        rubber_band_->setPen(QPen(Qt::black, 2.0, Qt::DotLine));
    }
    rubber_band_->setLine(QLineF(origin_, origin_));
    rubber_band_->show();
}

void ui::add_bone_tool::mouseMoveEvent(canvas& c, QGraphicsSceneMouseEvent* event) {
    rubber_band_->setLine(QLineF(origin_, event->scenePos()));
}

void ui::add_bone_tool::mouseReleaseEvent(canvas& canv, QGraphicsSceneMouseEvent* event) {
    rubber_band_->hide();
    auto pt = event->scenePos();
    auto parent_joint = canv.top_joint(origin_);
    auto child_joint = canv.top_joint(event->scenePos());

    if (parent_joint && child_joint && parent_joint != child_joint) {
        auto& sandbox = canv.view().main_window().sandbox();
        auto bone = sandbox.create_bone({}, parent_joint->model(), child_joint->model());
        if (bone) {
            canv.addItem(new ui::bone_item(bone->get(), canv.scale()));
        } else {
            auto error = bone.error();
        }
    }
}

/*------------------------------------------------------------------------------------------------*/

ui::arrow_tool::arrow_tool() : 
        abstract_tool("arrow", "arrow_icon.png", ui::tool_id::arrow) {

}

void ui::arrow_tool::activate(canvas& canv) {
    auto& view = canv.view();
    view.setDragMode(QGraphicsView::RubberBandDrag);
    rubber_band_ = {};
    conn_ = view.connect(
        &view, &QGraphicsView::rubberBandChanged, 
        [&](QRect rbr, QPointF from, QPointF to) {
            if (from != QPointF{0, 0}) {
                rubber_band_ = points_to_rect(from, to);
                
            }
            //qDebug() << *rubber_band_;
        }
    );
}

void ui::arrow_tool::mousePressEvent(canvas& canv, QGraphicsSceneMouseEvent* event) {
    rubber_band_ = {};
}

void ui::arrow_tool::mouseReleaseEvent(canvas& canv, QGraphicsSceneMouseEvent* event) {
    bool shift_down = event->modifiers().testFlag(Qt::ShiftModifier);
    bool alt_down = event->modifiers().testFlag(Qt::AltModifier);
    if (rubber_band_) {
        handle_drag(canv, *rubber_band_, shift_down, alt_down);
    } else {
        handle_click(canv, event->scenePos(), shift_down, alt_down);
    }
}

void ui::arrow_tool::handle_click(canvas& canv, QPointF pt, bool shift_down, bool alt_down) {
    auto clicked_item = canv.top_item(pt);
    if (!clicked_item) {
        canv.clear_selection();
        return;
    }

    if (shift_down && !alt_down) {
        canv.add_to_selection({ &clicked_item, 1 });
        return;
    }

    if (alt_down && !shift_down) {
        canv.subtract_from_selection({ &clicked_item, 1 });
        return;
    }

    canv.set_selection({ &clicked_item, 1 });
}

void ui::arrow_tool::handle_drag(canvas& canv, QRectF rect, bool shift_down, bool alt_down) {
    auto clicked_items = canv.items_in_rect(rect);
    if (clicked_items.empty()) {
        canv.clear_selection();
        return;
    }

    if (shift_down && !alt_down) {
        canv.add_to_selection(clicked_items);
        return;
    }

    if (alt_down && !shift_down) {
        canv.subtract_from_selection(clicked_items);
        return;
    }

    canv.set_selection(clicked_items);
}

void ui::arrow_tool::deactivate(canvas& canv) {
    auto& view = canv.view();
    view.setDragMode(QGraphicsView::NoDrag);
    view.disconnect(conn_);
}

/*------------------------------------------------------------------------------------------------*/

ui::tool_manager::tool_manager(stick_man* sm) :
        main_window_(*sm),
        curr_item_index_(-1){
}

void ui::tool_manager::keyPressEvent(canvas& c, QKeyEvent* event) {
    if (has_current_tool()) {
        current_tool().keyPressEvent(c, event);
    }
}

void ui::tool_manager::keyReleaseEvent(canvas& c, QKeyEvent* event) {
    if (has_current_tool()) {
        current_tool().keyReleaseEvent(c, event);
    }
}

void ui::tool_manager::mousePressEvent(canvas& c, QGraphicsSceneMouseEvent* event) {
    if (has_current_tool()) {
        current_tool().mousePressEvent(c, event);
    }
}

void ui::tool_manager::mouseMoveEvent(canvas& c, QGraphicsSceneMouseEvent* event) {
    if (has_current_tool()) {
        current_tool().mouseMoveEvent(c, event);
    }
}

void ui::tool_manager::mouseReleaseEvent(canvas& c, QGraphicsSceneMouseEvent* event) {
    if (has_current_tool()) {
        current_tool().mouseReleaseEvent(c, event);
    }
}

void ui::tool_manager::mouseDoubleClickEvent(canvas& c, QGraphicsSceneMouseEvent* event) {
    if (has_current_tool()) {
        current_tool().mouseDoubleClickEvent(c, event);
    }
}

void ui::tool_manager::wheelEvent(canvas& c, QGraphicsSceneWheelEvent* event) {
    if (has_current_tool()) {
        current_tool().wheelEvent(c, event);
    }
}

std::span<const ui::tool_info> ui::tool_manager::tool_info() const {
    static std::vector<ui::tool_info> tool_records;
    if (tool_records.empty()) {
        tool_records = k_tool_registry |
            rv::transform(
                [](const auto& t)->ui::tool_info {
                    return ui::tool_info{
                        t->id(), t->name(), t->icon_rsrc()
                    };
                }
            ) | r::to<std::vector<ui::tool_info>>();
    }
    return tool_records;
}

bool ui::tool_manager::has_current_tool() const {
    return curr_item_index_ >= 0;
}

ui::abstract_tool& ui::tool_manager::current_tool() const {
    return *k_tool_registry.at(curr_item_index_);
}

void ui::tool_manager::set_current_tool(tool_id id) {
    int new_tool_index = index_from_id(id);
    if (new_tool_index == curr_item_index_) {
        return;
    }
    auto& canvas = main_window_.view().canvas();
    canvas.setFocus();
    if (has_current_tool()) {
        current_tool().deactivate(canvas);
    }
    curr_item_index_ = new_tool_index;
    current_tool().activate(canvas);
    auto& tool_pane = main_window_.tool_pane();
    current_tool().populate_settings(&tool_pane);
}

int ui::tool_manager::index_from_id(tool_id id) const {
    auto iter = r::find_if(k_tool_registry, [id](const auto& t) {return id == t->id(); });
    return std::distance(k_tool_registry.begin(), iter);
}
