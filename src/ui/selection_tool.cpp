#include "selection_tool.h"
#include "util.h"
#include "../core/ik_sandbox.h"
#include <array>
#include <ranges>
#include <unordered_map>

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

    enum class sel_type {
        none = 0,
        node,
        bone,
        joint,
        mixed
    };

    class abstract_properties_widget : public QWidget {
    protected:
        QLayout* layout_;
        QLabel* title_;
    public:
        abstract_properties_widget(QWidget* parent, QString title) :
                QWidget(parent) {
            layout_ = new ui::FlowLayout(this);
            layout_->addWidget(title_ = new QLabel(title));
            hide();
        }
        virtual void set_selection(const ui::selection_set& sel) {}
    };

    class node_properties : public abstract_properties_widget {
        ui::labeled_numeric_val* x_;
        ui::labeled_numeric_val* y_;
    public:
        node_properties(QWidget* parent) : 
                abstract_properties_widget(parent, "selected node properties"){
            layout_->addWidget(
                x_ = new ui::labeled_numeric_val("x", 0.0, -1500.0, 1500.0)
            );
            layout_->addWidget(
                y_ = new ui::labeled_numeric_val("y", 0.0, -1500.0, 1500.0)
            );
        }
    };

    class bone_properties : public abstract_properties_widget {
        ui::labeled_numeric_val* length_;
        ui::labeled_numeric_val* rotation_;
    public:
        bone_properties(QWidget* parent) : 
                abstract_properties_widget(parent, "selected bone properties") {
            layout_->addWidget(
                length_ = new ui::labeled_numeric_val("length", 0.0, 0.0, 1500.0)
            );
            layout_->addWidget(
                rotation_ = new ui::labeled_numeric_val("rotation", 0.0, -1500.0, 1500.0)
            );
        }
    };

    class joint_properties : public abstract_properties_widget {
    public:
        joint_properties(QWidget* parent) : 
            abstract_properties_widget(parent, "selected joint properties") {
        }
    };

    template<typename T>
    auto to_models_of_item_type(const auto& sel) {
        using out_type = typename T::model_type;
        return sel | rv::transform(
            [](auto ptr)->out_type* {
                auto bi = dynamic_cast<T*>(ptr);
                if (!bi) {
                    return nullptr;
                }
                return &(bi->model());
            }
        ) | rv::filter(
            [](auto ptr) {
                return ptr;
            }
        ) | r::to<std::vector<out_type*>>();
    }

    sm::bone* find_child(sm::bone* b1, sm::bone* b2) {
        auto b1_par = b1->parent_bone().has_value() ? &b1->parent_bone().value().get() : nullptr;
        auto b2_par = b2->parent_bone().has_value() ? &b2->parent_bone().value().get() : nullptr;
        if (!b1_par && !b2_par) {
            return nullptr;
        }
        if (b1_par == b2) {
            return b1;
        }
        if (b2_par == b1) {
            return b2;
        }
        return nullptr;
    }

    sm::bone* find_child_of_joint(const auto& sel) {
        auto bones = to_models_of_item_type<ui::bone_item>(sel);
        if (bones.size() != 2) {
            return nullptr;
        }
        return find_child(bones.front(), bones.back());
    }

    bool is_joint_selection(const auto& sel) {
        if (sel.size() != 2 && sel.size() != 3) {
            return false;
        }
        
        auto child_bone = find_child_of_joint(sel);
        if (!child_bone) {
            return false;
        }

        if (sel.size() == 2) {
            return true;
        }

        auto nodes = to_models_of_item_type<ui::node_item>(sel);
        if (nodes.size() != 1) {
            return false;
        }
        return &child_bone->parent_node() == nodes.front();
    }

    sel_type selection_type(const auto& sel) {
        if (sel.empty()) {
            return sel_type::none;
        }
        if (is_joint_selection(sel)) {
            return sel_type::joint;
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
        return has_node ? sel_type::node : sel_type::bone;
    }

    class selection_properties : public QStackedWidget {
    public:
        selection_properties() { 
            addWidget(new abstract_properties_widget(this, "no selection"));
            addWidget(new node_properties(this));
            addWidget(new bone_properties(this));
            addWidget(new joint_properties(this));
            addWidget(new abstract_properties_widget(this, "mixed selection"));

            set(sel_type::none, {});
        }

        abstract_properties_widget* current_props() const {
            return static_cast<abstract_properties_widget*>(currentWidget());
        }

        void set(sel_type typ, const std::unordered_set<ui::abstract_stick_man_item*>& sel) {
            setCurrentIndex(static_cast<int>(typ));
            current_props()->set_selection(sel);
        }
    };
}

ui::selection_tool::selection_tool(tool_manager* mgr) :
    intitialized_(false),
    properties_(nullptr),
    abstract_tool(mgr, "selection", "arrow_icon.png", ui::tool_id::selection) {

}

void ui::selection_tool::activate(canvas& canv) {
    if (!intitialized_) {
        canv.connect(&canv, &canvas::selection_changed, 
            [this](const std::unordered_set<ui::abstract_stick_man_item*>& sel) {
                this->handle_sel_changed(sel);
            }
        );
        intitialized_ = true;
    }
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
}

void ui::selection_tool::mousePressEvent(canvas& canv, QGraphicsSceneMouseEvent* event) {
    rubber_band_ = {};
}

void ui::selection_tool::mouseReleaseEvent(canvas& canv, QGraphicsSceneMouseEvent* event) {
    bool shift_down = event->modifiers().testFlag(Qt::ShiftModifier);
    bool alt_down = event->modifiers().testFlag(Qt::AltModifier);
    if (rubber_band_) {
        handle_drag(canv, *rubber_band_, shift_down, alt_down);
    } else {
        handle_click(canv, event->scenePos(), shift_down, alt_down);
    }
}

void ui::selection_tool::handle_click(canvas& canv, QPointF pt, bool shift_down, bool alt_down) {
    auto clicked_item = canv.top_item(pt);
    if (!clicked_item) {
        canv.clear_selection();
        return;
    }

    if (shift_down && !alt_down) {
        canv.add_to_selection(clicked_item);
        return;
    }

    if (alt_down && !shift_down) {
        canv.subtract_from_selection(clicked_item);
        return;
    }

    canv.set_selection(clicked_item);
}

void ui::selection_tool::handle_drag(canvas& canv, QRectF rect, bool shift_down, bool alt_down) {
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

    canv.set_selection( clicked_items);
}

void ui::selection_tool::handle_sel_changed(
        const std::unordered_set<ui::abstract_stick_man_item*>& sel) {
    auto type_of_sel = selection_type(sel);
    auto props = static_cast<selection_properties*>(properties_);
    props->set(type_of_sel, sel);
}


void ui::selection_tool::deactivate(canvas& canv) {
    auto& view = canv.view();
    view.setDragMode(QGraphicsView::NoDrag);
    view.disconnect(conn_);
}

QWidget* ui::selection_tool::settings_widget() {
    if (!properties_) {
        properties_ = new selection_properties();
    }
    return properties_;
}