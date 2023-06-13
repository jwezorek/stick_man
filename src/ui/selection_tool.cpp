#include "selection_tool.h"
#include "util.h"
#include "canvas.h"
#include "stick_man.h"
#include "../core/ik_sandbox.h"
#include "../core/ik_types.h"
#include <array>
#include <ranges>
#include <unordered_map>
#include <unordered_set>

namespace r = std::ranges;
namespace rv = std::ranges::views;

/*------------------------------------------------------------------------------------------------*/

namespace {

    double k_tolerance = 0.00005;

    auto to_model_objects(r::input_range auto&& itms) {
        return itms |
            rv::transform(
                [](auto itm)->auto& {
                    return itm->model();
                }
            );
    }

    struct bone_info {
        double rotation;
        double length;
    };

    void set_bone_length(ui::canvas& canv, double new_length) {
        using bone_table_t = std::unordered_map<sm::bone*, bone_info>;
        auto bone_items = canv.bone_items();
        auto bone_tbl = to_model_objects(bone_items) |
            rv::transform(
                [new_length](sm::bone& bone)->bone_table_t::value_type {
                    auto& itm = ui::item_from_model<ui::bone_item>(bone);
                    auto length = itm.is_selected() ? new_length : bone.scaled_length();
                    return { &bone,bone_info{bone.world_rotation(), length} };
                }
            ) | r::to<bone_table_t>();

        auto root_node_items = canv.root_node_items();
        for (auto& root : to_model_objects(root_node_items)) {
            sm::dfs(
                root, 
                {}, 
                [&](sm::bone& bone)->bool {
                    auto [rot, len] = bone_tbl.at(&bone);
                    sm::point offset = {
                        len * std::cos(rot),
                        len * std::sin(rot)
                    };
                    auto new_child_node_pos = bone.parent_node().world_pos() + offset;
                    bone.child_node().set_world_pos(new_child_node_pos);
                    return true;
                },
                true
            );
        }
        canv.sync_to_model();
    }

    bool is_approximately_equal(double v1, double v2, double tolerance) {
        return std::abs(v1 - v2) < tolerance;
    }

    std::optional<double> get_unique_val(auto vals, double tolerance = k_tolerance) {
        if (vals.empty()) {
            return {};
        }
        auto first_val = *vals.begin();
        auto first_not_equal = r::find_if(
            vals,
            [=](auto val) {
                return !is_approximately_equal(val, first_val, tolerance);
            }
        );
        return  (first_not_equal == vals.end()) ? 
            std::optional<double>{first_val} : std::optional<double>{};
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

    enum class sel_type {
        none = 0,
        node,
        nodes,
        bone,
        bones,
        joint,
        mixed
    };

    class abstract_properties_widget : public QWidget {
    protected:
        QLayout* layout_;
        QLabel* title_;
        ui::tool_manager* mgr_;
        ui::canvas* canv_;
    public:
        abstract_properties_widget(ui::tool_manager* mgr, QWidget* parent, QString title) :
                QWidget(parent),
                mgr_(mgr),
                canv_(nullptr){
            layout_ = new ui::FlowLayout(this);
            layout_->addWidget(title_ = new QLabel(title));
            hide();
        }

        ui::canvas& canvas() {
            if (!canv_) {
                canv_ = &(mgr_->canvas());
            }
            return *canv_;
        }

        virtual void set_selection(const ui::selection_set& sel) = 0;
    };

    class no_properties : public abstract_properties_widget {
    public:
        no_properties(ui::tool_manager* mgr, QWidget* parent) : abstract_properties_widget(mgr, parent, "no selection") {}
        void set_selection(const ui::selection_set& sel) override {}
    };

    class mixed_properties : public abstract_properties_widget {
    public:
        mixed_properties(ui::tool_manager* mgr, QWidget* parent) :
            abstract_properties_widget(mgr, parent, "mixed selection") {}
        void set_selection(const ui::selection_set& sel) override {}
    };

    class node_properties : public abstract_properties_widget {
        ui::TabWidget* tab_;
        ui::labeled_numeric_val* world_x_;
        ui::labeled_numeric_val* world_y_;
        bool multi_;
    public:
        node_properties(ui::tool_manager* mgr, QWidget* parent, bool multi) :
                abstract_properties_widget(
                    mgr,
                    parent, 
                    (multi) ? "selected nodes" : "selected node"
                ),
                multi_(multi) {
            layout_->addWidget(tab_ = new ui::TabWidget(this));
            tab_->setTabPosition(QTabWidget::South);

            QWidget* world_tab = new QWidget();
            QLayout* wtl;
            world_tab->setLayout(wtl = new QVBoxLayout());
            wtl->addWidget(world_x_ = new ui::labeled_numeric_val("x", 0.0, -1500.0, 1500.0));
            wtl->addWidget(world_y_ = new ui::labeled_numeric_val("y", 0.0, -1500.0, 1500.0));
            tab_->addTab(
                world_tab,
                "world"
            );
            tab_->addTab(
                new QWidget(),
                "model"
            );

            tab_->addTab(
                new QWidget(),
                "parent"
            );

            auto& canv = this->canvas();
            connect(world_x_->num_edit(), &ui::number_edit::value_changed,
                [&canv](double v) {
                    canv.transform_selection(
                        [v](ui::node_item* ni) {
                            auto y = ni->model().world_y();
                            ni->model().set_world_pos(sm::point(v, y));
                        }
                    );
                    canv.sync_to_model();
                }
            );

            connect(world_y_->num_edit(), &ui::number_edit::value_changed,
                [&canv](double v) {
                    canv.transform_selection(
                        [v](ui::node_item* ni) {
                            auto x = ni->model().world_x();
                            ni->model().set_world_pos(sm::point(x, v));
                        }
                    );
                    canv.sync_to_model();
                }
            );
        }

        void set_selection(const ui::selection_set& sel) override {
            auto nodes = to_model_objects(ui::as_range_view_of_type<ui::node_item>(sel));
            auto x_pos = get_unique_val(nodes |
                rv::transform([](sm::node& n) {return n.world_x(); }));
            auto y_pos = get_unique_val(nodes |
                rv::transform([](sm::node& n) {return n.world_y(); }));
            world_x_->num_edit()->set_value(x_pos);
            world_y_->num_edit()->set_value(y_pos);
        }
    };

    class bone_properties : public abstract_properties_widget {
        ui::labeled_numeric_val* length_;
        QTabWidget* rotation_tab_ctrl_;
        ui::labeled_numeric_val* world_rotation_;
        ui::labeled_numeric_val* parent_rotation_;
        bool multi_;
    public:
        bone_properties(ui::tool_manager* mgr, QWidget* parent, bool multi) : 
                abstract_properties_widget(
                    mgr,
                    parent, 
                    (multi) ? "selected bones" : "selected bone"
                ),
                multi_(multi) {
            layout_->addWidget(
                length_ = new ui::labeled_numeric_val("length", 0.0, 0.0, 1500.0)
            );
            
            layout_->addWidget(rotation_tab_ctrl_ = new ui::TabWidget(this));
            rotation_tab_ctrl_->setTabPosition(QTabWidget::South);

            rotation_tab_ctrl_->addTab(
                world_rotation_ = new ui::labeled_numeric_val("rotation", 0.0, -180.0, 180.0),
                "world"
            );
            rotation_tab_ctrl_->addTab(
                world_rotation_ = new ui::labeled_numeric_val("rotation", 0.0, -180.0, 180.0),
                "parent"
            );

            auto& canv = this->canvas();
            connect(length_->num_edit(), &ui::number_edit::value_changed,
                [&canv](double v) {
                    set_bone_length(canv, v);
                }
            );
        }

        void set_selection(const ui::selection_set& sel) override {
            auto bones = to_model_objects(ui::as_range_view_of_type<ui::bone_item>(sel));
            auto length = get_unique_val(bones |
                rv::transform([](sm::bone& b) {return b.scaled_length(); }));
            length_->num_edit()->set_value(length);
            //auto rotation = get_unique_val(bones |
            //    rv::transform([](sm::bone&& b) {return b.r }));
        }
    };

    class joint_properties : public abstract_properties_widget {
    public:
        joint_properties(ui::tool_manager* mgr, QWidget* parent) :
            abstract_properties_widget(mgr, parent, "selected joint") {
        }

        void set_selection(const ui::selection_set& sel) override {

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

    sm::node* find_shared_node(sm::bone* b1, sm::bone* b2) {
        std::array< sm::node*, 4> nodes = { {
            &(b1->parent_node()),
            &(b1->child_node()),
            &(b2->parent_node()),
            &(b2->child_node()),
        } };
        r::sort(nodes);
        auto adj_dup = r::adjacent_find(nodes, [](auto n1, auto n2) {return n1 == n2; });
        return (adj_dup != nodes.end()) ? *adj_dup: nullptr;
    }

    bool is_joint_selection(const auto& sel) {
        if (sel.size() != 3) {
            return false;
        }
        auto bones = to_models_of_item_type<ui::bone_item>(sel);
        if (bones.size() != 2) {
            return false;
        }
        auto shared_node = find_shared_node(bones[0],bones[1]);
        if (!shared_node) {
            return false;
        }
        auto nodes = to_models_of_item_type<ui::node_item>(sel);
        if (nodes.size() != 1) {
            return false;
        }
        return shared_node == nodes.front();
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
        bool multi = sel.size() > 1;
        if (has_node) {
            return multi ? sel_type::nodes : sel_type::node;
        } else {
            return multi ? sel_type::bones : sel_type::bone;
        }
    }

    class selection_properties : public QStackedWidget {
    public:
        selection_properties(ui::tool_manager* mgr) { 
            addWidget(new no_properties(mgr, this));
            addWidget(new node_properties(mgr, this, false));
            addWidget(new node_properties(mgr, this, true));
            addWidget(new bone_properties(mgr, this, false));
            addWidget(new bone_properties(mgr, this, true));
            addWidget(new joint_properties(mgr, this));
            addWidget(new mixed_properties(mgr, this));
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

void ui::selection_tool::keyReleaseEvent(canvas & c, QKeyEvent * event) {
    auto props = static_cast<selection_properties*>(properties_);
    auto node_props = static_cast<node_properties*>(props->current_props());
    for (auto* child : node_props->children()) {

    }
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
        properties_ = new selection_properties(manager());
    }
    return properties_;
}