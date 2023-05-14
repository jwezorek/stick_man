#include "tools.h"
#include "stick_man.h"
#include <QGraphicsScene>
#include <array>
#include <functional>
#include <ranges>

namespace r = std::ranges;
namespace rv = std::ranges::views;

/*------------------------------------------------------------------------------------------------*/

namespace {
    /*
    class placeholder : public ui::abstract_tool {
    public:
        placeholder(QString name, QString rsrc, ui::tool_id id) : abstract_tool(name, rsrc, id) {}
        void activate(ui::canvas& c) override {}
        void keyPressEvent(ui::canvas& c, QKeyEvent* event) override {}
        void keyReleaseEvent(ui::canvas& c, QKeyEvent* event) override {}
        void mousePressEvent(ui::canvas& c, QGraphicsSceneMouseEvent* event) override {}
        void mouseMoveEvent(ui::canvas& c, QGraphicsSceneMouseEvent* event) override {}
        void mouseReleaseEvent(ui::canvas& c, QGraphicsSceneMouseEvent* event) override {}
        void mouseDoubleClickEvent(ui::canvas& c, QGraphicsSceneMouseEvent* event) override {}
        void wheelEvent(ui::canvas& c, QGraphicsSceneWheelEvent* event) override {}
        void deactivate(ui::canvas& c) override {}
    };
    */

    auto k_tool_registry = std::to_array<std::unique_ptr<ui::abstract_tool>>({
        std::make_unique<ui::pan_tool>(),
        std::make_unique<ui::zoom_tool>(),
        std::make_unique<ui::arrow_tool>(),
        std::make_unique<ui::move_tool>(),
        std::make_unique<ui::add_joint_tool>(),
        std::make_unique<ui::add_bone_tool>()
    });

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

void ui::abstract_tool::activate(canvas& c) {}
void ui::abstract_tool::keyPressEvent(canvas& c, QKeyEvent* event) {}
void ui::abstract_tool::keyReleaseEvent(canvas& c, QKeyEvent* event) {}
void ui::abstract_tool::mousePressEvent(canvas& c, QGraphicsSceneMouseEvent* event) {}
void ui::abstract_tool::mouseMoveEvent(canvas& c, QGraphicsSceneMouseEvent* event) {}
void ui::abstract_tool::mouseReleaseEvent(canvas& c, QGraphicsSceneMouseEvent* event) {}
void ui::abstract_tool::mouseDoubleClickEvent(canvas& c, QGraphicsSceneMouseEvent* event) {}
void ui::abstract_tool::wheelEvent(canvas& c, QGraphicsSceneWheelEvent* event) {}
void ui::abstract_tool::deactivate(canvas& c) {}

/*------------------------------------------------------------------------------------------------*/

ui::zoom_tool::zoom_tool() : 
        abstract_tool("zoom", "zoom_icon.png", ui::tool_id::zoom),
        zoom_level_(0) {
}

qreal ui::zoom_tool::scale_from_zoom_level() const {
    if (zoom_level_ == 0) {
        return 1.0;
    }
    if (zoom_level_ > 0) {
        qreal zoom = static_cast<qreal>(zoom_level_);
        return zoom + 1.0;
    }
    qreal zoom = -1.0 * zoom_level_;
    return 1.0 / (zoom + 1.0);
}

void ui::zoom_tool::mouseReleaseEvent(canvas& c, QGraphicsSceneMouseEvent* event) {
    if (! event->modifiers().testFlag(Qt::AltModifier)) {
        zoom_level_++;
    } else {
        zoom_level_--;
    }
    auto pt = event->scenePos();
    auto zoom = scale_from_zoom_level();
    auto& view = c.view();
    view.resetTransform();
    view.scale(zoom, -zoom);
    view.centerOn(pt);
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
    abstract_tool("add joint tool", "add_joint_icon.png", ui::tool_id::add_joint) {}

void ui::add_joint_tool::mouseReleaseEvent(canvas& canv, QGraphicsSceneMouseEvent* event) {
    auto pt = event->scenePos();
    auto& sandbox = canv.view().main_window().sandbox();
    auto j = sandbox.create_joint({}, pt.x(), pt.y());

    auto item = new ui::joint_item(j->get());
    canv.addItem(item);
}

/*------------------------------------------------------------------------------------------------*/

ui::add_bone_tool::add_bone_tool() :
    rubber_band_(nullptr),
    abstract_tool("add bone tool", "add_bone_icon.png", ui::tool_id::add_bone) {
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

    if (parent_joint && child_joint) {
        auto& sandbox = canv.view().main_window().sandbox();
        auto bone = sandbox.create_bone({}, parent_joint->joint(), child_joint->joint());
        if (bone) {
            auto item = new ui::bone_item(bone->get());
        } else {
            auto error = bone.error();
        }
    }
}


/*
void ui::add_bone_tool::mouseReleaseEvent(canvas& canv, QGraphicsSceneMouseEvent* event) {
    auto pt = event->scenePos();
    auto joint = canv.top_joint(pt);
    if (!joint) {
        if (parent_joint_) {
            parent_joint_->setBrush(QBrush(Qt::white));
            parent_joint_ = nullptr;
        }
        return;
    }
    if (parent_joint_ == nullptr) {
        parent_joint_ = joint;
        parent_joint_->setBrush(QBrush(Qt::blue));
    } else {
        auto& sandbox = canv.view().main_window().sandbox();
        auto bone = sandbox.create_bone({}, parent_joint_->joint(), joint->joint());
        if (bone) {
            auto item = new ui::bone_item(bone->get());
        } else {
            auto error = bone.error();
        }
        parent_joint_->setBrush(QBrush(Qt::white));
        parent_joint_ = nullptr;
    }
}
*/

/*------------------------------------------------------------------------------------------------*/

ui::arrow_tool::arrow_tool() : 
        abstract_tool("arrow tool", "arrow_icon.png", ui::tool_id::arrow) {

}

/*------------------------------------------------------------------------------------------------*/

ui::move_tool::move_tool() : abstract_tool("move tool", "move_icon.png", ui::tool_id::move) {

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
    if (has_current_tool()) {
        current_tool().deactivate(canvas);
    }
    curr_item_index_ = new_tool_index;
    current_tool().activate(canvas);
}

int ui::tool_manager::index_from_id(tool_id id) const {
    auto iter = r::find_if(k_tool_registry, [id](const auto& t) {return id == t->id(); });
    return std::distance(k_tool_registry.begin(), iter);
}
