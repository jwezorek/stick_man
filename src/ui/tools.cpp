#include "tools.h"
#include "move_tool.h"
#include "selection_tool.h"
#include "stick_man.h"
#include "tool_settings_pane.h"
#include "util.h"
#include "stick_man.h"
#include "canvas_item.h"
#include <QGraphicsScene>
#include <array>
#include <functional>
#include <ranges>

namespace r = std::ranges;
namespace rv = std::ranges::views;

/*------------------------------------------------------------------------------------------------*/

ui::abstract_tool::abstract_tool(tool_manager* mgr, QString name, QString rsrc, tool_id id) :
    mgr_(mgr),
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

ui::tool_manager* ui::abstract_tool::manager() const {
    return mgr_;
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
void ui::abstract_tool::init() {}
QWidget* ui::abstract_tool::settings_widget() { return nullptr; }
ui::abstract_tool::~abstract_tool() {}

/*------------------------------------------------------------------------------------------------*/

ui::zoom_tool::zoom_tool(tool_manager* mgr) :
        abstract_tool(mgr, "zoom", "zoom_icon.png", ui::tool_id::zoom),
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

ui::pan_tool::pan_tool(tool_manager* mgr) : 
    abstract_tool(mgr, "pan", "pan_icon.png", ui::tool_id::pan) 
{}

void ui::pan_tool::deactivate(canvas& c) {
    c.manager().set_drag_mode(ui::drag_mode::none);
}

void ui::pan_tool::activate(canvas& c) {
    c.manager().set_drag_mode(ui::drag_mode::pan);
}

/*------------------------------------------------------------------------------------------------*/

ui::add_node_tool::add_node_tool(tool_manager* mgr) :
    abstract_tool(mgr, "add node", "add_node_icon.png", ui::tool_id::add_node) 
{}

void ui::add_node_tool::mouseReleaseEvent(canvas& canv, QGraphicsSceneMouseEvent* event) {
    auto pt = event->scenePos();
    auto& sandbox = manager()->main_window().sandbox();
    auto new_skeleton = sandbox.create_skeleton(pt.x(), pt.y());
    canv.insert_item(new_skeleton.get().root_node().get());
}

/*------------------------------------------------------------------------------------------------*/

void ui::add_bone_tool::init_rubber_band(canvas& c) {
    if (!rubber_band_) {
        c.addItem(rubber_band_ = new QGraphicsLineItem());
        rubber_band_->setPen(QPen(Qt::black, 2.0, Qt::DotLine));
        return;
    } else if (rubber_band_->scene() != &c) {
        rubber_band_->scene()->removeItem(rubber_band_);
        c.addItem(rubber_band_);
    }
}

ui::add_bone_tool::add_bone_tool(tool_manager* mgr) :
    rubber_band_(nullptr),
    abstract_tool(mgr, "add bone", "add_bone_icon.png", ui::tool_id::add_bone) {
}

void ui::add_bone_tool::mousePressEvent(canvas& c, QGraphicsSceneMouseEvent* event) {
    origin_ = event->scenePos();
    init_rubber_band(c);
    rubber_band_->setLine(QLineF(origin_, origin_));
    rubber_band_->show();
}

void ui::add_bone_tool::mouseMoveEvent(canvas& c, QGraphicsSceneMouseEvent* event) {
    rubber_band_->setLine(QLineF(origin_, event->scenePos()));
}

void ui::add_bone_tool::mouseReleaseEvent(canvas& canv, QGraphicsSceneMouseEvent* event) {
    rubber_band_->hide();
    auto pt = event->scenePos();
    auto parent_node = canv.top_node(origin_);
    auto child_node = canv.top_node(event->scenePos());

	
    if (parent_node && child_node && parent_node != child_node) {
        auto& sandbox = manager()->main_window().sandbox();
		
		auto skel_v = child_node->model().owner();
		if (skel_v.get().get_user_data().has_value()) {
			ui::skeleton_item* skel_itm = &item_from_model<skeleton_item>(skel_v.get());
			canv.delete_item(skel_itm, false);
		}

        auto bone = sandbox.create_bone({}, parent_node->model(), child_node->model());
        if (bone) {
			canv.insert_item(bone->get());
			canv.sync_to_model();
        } else {
            auto error = bone.error();
        }
    }
}

/*------------------------------------------------------------------------------------------------*/

ui::tool_manager::tool_manager(stick_man* sm) :
        main_window_(*sm),
        curr_item_index_(-1){
    tool_registry_.emplace_back(std::make_unique<ui::pan_tool>(this));
    tool_registry_.emplace_back(std::make_unique<ui::zoom_tool>(this));
    tool_registry_.emplace_back(std::make_unique<ui::selection_tool>(this, &main_window_));
    tool_registry_.emplace_back(std::make_unique<ui::move_tool>(this));
    tool_registry_.emplace_back(std::make_unique<ui::add_node_tool>(this));
    tool_registry_.emplace_back(std::make_unique<ui::add_bone_tool>(this));
}

void ui::tool_manager::init() {
	for (auto& tool : tool_registry_) {
		tool->init();
	}
}

void ui::tool_manager::keyPressEvent(ui::canvas& c, QKeyEvent* event) {
    if (has_current_tool()) {
        current_tool().keyPressEvent(c, event);
    }
}

void ui::tool_manager::keyReleaseEvent(ui::canvas& c, QKeyEvent* event) {
    if (has_current_tool()) {
        current_tool().keyReleaseEvent(c, event);
    }
}

void ui::tool_manager::mousePressEvent(ui::canvas& c, QGraphicsSceneMouseEvent* event) {
    if (has_current_tool()) {
        current_tool().mousePressEvent(c, event);
    }
}

void ui::tool_manager::mouseMoveEvent(ui::canvas& c, QGraphicsSceneMouseEvent* event) {
    if (has_current_tool()) {
        current_tool().mouseMoveEvent(c, event);
    }
}

void ui::tool_manager::mouseReleaseEvent(ui::canvas& c, QGraphicsSceneMouseEvent* event) {
    if (has_current_tool()) {
        current_tool().mouseReleaseEvent(c, event);
    }
}

void ui::tool_manager::mouseDoubleClickEvent(ui::canvas& c, QGraphicsSceneMouseEvent* event) {
    if (has_current_tool()) {
        current_tool().mouseDoubleClickEvent(c, event);
    }
}

void ui::tool_manager::wheelEvent(ui::canvas& c, QGraphicsSceneWheelEvent* event) {
    if (has_current_tool()) {
        current_tool().wheelEvent(c, event);
    }
}

std::span<const ui::tool_info> ui::tool_manager::tool_info() const {
    static std::vector<ui::tool_info> tool_records;
    if (tool_records.empty()) {
        tool_records = tool_registry_ |
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
    return *tool_registry_.at(curr_item_index_);
}

void ui::tool_manager::set_current_tool(tool_id id) {
    int new_tool_index = index_from_id(id);
    if (new_tool_index == curr_item_index_) {
        return;
    }
    auto& canvas = main_window().canvases().active_canvas();
    canvas.setFocus();
    if (has_current_tool()) {
        current_tool().deactivate(canvas);
    }
    curr_item_index_ = new_tool_index;
    current_tool().activate(canvas);
    auto& tool_pane = main_window_.tool_pane();
    current_tool().populate_settings(&tool_pane);
}

ui::canvas& ui::tool_manager::canvas() {
    return main_window_.canvases().active_canvas();
}

ui::stick_man& ui::tool_manager::main_window() {
    return main_window_;
}

int ui::tool_manager::index_from_id(tool_id id) const {
    auto iter = r::find_if(tool_registry_, [id](const auto& t) {return id == t->id(); });
    return std::distance(tool_registry_.begin(), iter);
}
