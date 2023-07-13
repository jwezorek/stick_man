#include "selection_tool.h"
#include "util.h"
#include "canvas.h"
#include "canvas_item.h"
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

	QPointF joint_axis(const ui::joint_info& ji) {
		return ui::to_qt_pt(ji.shared_node->world_pos());
	}

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

    class abstract_properties_widget : public QScrollArea {
    private:
        ui::canvas* canv_;
    protected:
        QVBoxLayout* layout_;
        QLabel* title_;
        ui::tool_manager* mgr_;
    public:
        abstract_properties_widget(ui::tool_manager* mgr, QWidget* parent, QString title) :
                QScrollArea(parent),
                mgr_(mgr),
                canv_(nullptr),
                title_(nullptr),
                layout_(new QVBoxLayout(this)) {
            layout_->addWidget(title_ = new QLabel(title));
            hide();
        }

        void init() {
            populate();
            layout_->addStretch();
        }

        virtual void populate() {

        }

        ui::canvas& canvas() {
            if (!canv_) {
                canv_ = &(mgr_->canvas());
            }
            return *canv_;
        }

        virtual void set_selection(const ui::canvas& canv) = 0;
		virtual void lose_selection() {}
    };

    class no_properties : public abstract_properties_widget {
    public:
        no_properties(ui::tool_manager* mgr, QWidget* parent) : abstract_properties_widget(mgr, parent, "no selection") {}
        void set_selection(const ui::canvas& canv) override {}
    };

    class mixed_properties : public abstract_properties_widget {
    public:
        mixed_properties(ui::tool_manager* mgr, QWidget* parent) :
            abstract_properties_widget(mgr, parent, "mixed selection") {}
        void set_selection(const ui::canvas& canv) override {}
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
        }

        void populate() override {

            layout_->addWidget(tab_ = new ui::TabWidget(this));
            tab_->setTabPosition(QTabWidget::South);

            QWidget* world_tab = new QWidget();
            QVBoxLayout* wtl;
            world_tab->setLayout(wtl = new QVBoxLayout());
            wtl->addWidget(world_x_ = new ui::labeled_numeric_val("x", 0.0, -1500.0, 1500.0));
            wtl->addWidget(world_y_ = new ui::labeled_numeric_val("y", 0.0, -1500.0, 1500.0));
            wtl->addStretch();
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
            tab_->setFixedHeight(135); //TODO: figure out how to size a tab to its contents
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

        void set_selection(const ui::canvas& canv) override {
			const auto& sel = canv.selection();
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
        }

        void populate() override {
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
            rotation_tab_ctrl_->setFixedHeight(70);
            //TODO: figure out how to size a tab to its contents

            auto& canv = this->canvas();
            connect(length_->num_edit(), &ui::number_edit::value_changed,
                [&canv](double v) {
                    set_bone_length(canv, v);
                }
            );
        }

        void set_selection(const ui::canvas& canv) override {
			const auto& sel = canv.selection();
            auto bones = to_model_objects(ui::as_range_view_of_type<ui::bone_item>(sel));
            auto length = get_unique_val(bones |
                rv::transform([](sm::bone& b) {return b.scaled_length(); }));
            length_->num_edit()->set_value(length);
            //auto rotation = get_unique_val(bones |
            //    rv::transform([](sm::bone&& b) {return b.r }));
        }
    };

	struct constraint_dragging_info {
		QPointF axis;
		double anchor_rot;
		double start_angle;
		double span_angle;
		double radius;
	};

    class joint_properties : public abstract_properties_widget {
        QLabel* bones_lbl_;
        QPushButton* constraint_btn_;
        QGroupBox* constraint_box_;
        ui::joint_info joint_info_;
		QGraphicsEllipseItem* arc_rubber_band_;
		ui::labeled_numeric_val* min_angle_;
		ui::labeled_numeric_val* max_angle_;
		std::optional<constraint_dragging_info> dragging_;

        ui::selection_tool* sel_tool() const {
            return static_cast<ui::selection_tool*>(&mgr_->current_tool());
        }

        QGroupBox* create_constraint_box() {
            auto box = new QGroupBox();

            auto* layout = new QVBoxLayout();
            box->setLayout(layout);
            layout->addWidget(min_angle_ = new ui::labeled_numeric_val("minimum angle", 0, -180, 180));
            layout->addWidget(max_angle_ = new ui::labeled_numeric_val("maximum angle", 0, -180, 180));
            layout_->addWidget(box);
            box->hide();

            return box;
        }
        
        QString name_of_joint() {
            const auto& ji = joint_info_;
            std::string name = ji.anchor_bone->name() + "/" + ji.dependent_bone->name();
            return name.c_str();
        }

        void add_or_delete_constraint() {
            if (constraint_box_->isVisible()) {
				//TODO: delete constraint
            } else {
                sel_tool()->set_modal(
                    ui::selection_tool::modal_state::defining_joint_constraint,
                    canvas()
                );
            }
        }

		void show_constraint_box(bool should_show) {
			if (should_show) {
				constraint_box_->show();
				constraint_btn_->setText("delete constraint");
			} else {
				constraint_box_->hide();
				constraint_btn_->setText("add constraint");
			}
		}

    public:
        joint_properties(ui::tool_manager* mgr, QWidget* parent, bool parent_child) :
			arc_rubber_band_(nullptr),
            abstract_properties_widget(
                mgr, 
                parent, 
                (parent_child) ? "selected parent-child joint" : "selected sibling joint"
            ) {
        }

        void populate() override {
            layout_->addWidget(bones_lbl_ = new QLabel());
            layout_->addWidget(constraint_box_ = create_constraint_box());
            layout_->addWidget(constraint_btn_ = new QPushButton("add constraint"));

            connect(constraint_btn_, &QPushButton::clicked,
                this, &joint_properties::add_or_delete_constraint);
        }

        void set_selection(const ui::canvas& canv) override {
            joint_info_ = canv.selected_joint().value();
            bones_lbl_->setText(name_of_joint());
        }

		void lose_selection() override {
			show_constraint_box(false);
		}

        ui::joint_info joint_info() const {
            return joint_info_;
        }

        void start_dragging_joint_constraint(ui::canvas& canv, QGraphicsSceneMouseEvent* event) {
			if (is_dragging_joint_constraint()) {
				return;
			}
			if (!arc_rubber_band_) {
				arc_rubber_band_ = new QGraphicsEllipseItem();
				arc_rubber_band_->setPen(QPen(Qt::black, 2.0, Qt::DotLine));
				canv.addItem(arc_rubber_band_);
			}
			arc_rubber_band_->show(); 

			auto ji = joint_info();
			auto center = joint_axis(ji);
			dragging_ = {
				.axis = center,
				.anchor_rot = ji.anchor_bone->world_rotation(),
				.start_angle = ui::angle_through_points(center, event->scenePos()),
				.span_angle = 0.0,
				.radius = ui::distance(center, event->scenePos())
			};
			ui::set_arc(
				arc_rubber_band_,
				dragging_->axis,
				dragging_->radius,
				dragging_->start_angle,
				dragging_->span_angle
			);
        }

        void drag_joint_constraint(ui::canvas& canv, QGraphicsSceneMouseEvent* event) {
			auto end_theta = ui::angle_through_points(dragging_->axis, event->scenePos());
			auto start_relative = ui::normalize_angle(
				dragging_->start_angle - dragging_->anchor_rot
			);
			auto end_relative = ui::normalize_angle(
				end_theta - dragging_->anchor_rot
			);
			dragging_->span_angle = ui::clamp_above(end_relative - start_relative, 0);

			ui::set_arc(
				arc_rubber_band_,
				dragging_->axis,
				dragging_->radius,
				dragging_->start_angle,
				dragging_->span_angle
			);
        }

        bool is_dragging_joint_constraint() const {
			return dragging_.has_value();
        }

        void stop_dragging_joint_constraint(ui::canvas& canv, QGraphicsSceneMouseEvent* event) {
			arc_rubber_band_->hide();

			auto min_angle_radians = ui::normalize_angle(
				dragging_->start_angle - dragging_->anchor_rot
			);
			auto max_angle_radians = min_angle_radians + dragging_->span_angle;

			min_angle_->num_edit()->set_value(ui::radians_to_degrees(min_angle_radians));
			max_angle_->num_edit()->set_value(ui::radians_to_degrees(max_angle_radians));
			show_constraint_box(true);

			sel_tool()->set_modal(
				ui::selection_tool::modal_state::none,
				canvas()
			);
        }
    };

    class selection_properties : public QStackedWidget {
    public:
        selection_properties(ui::tool_manager* mgr) { 
            addWidget(new no_properties(mgr, this));
            addWidget(new node_properties(mgr, this, false));
            addWidget(new node_properties(mgr, this, true));
            addWidget(new bone_properties(mgr, this, false));
            addWidget(new bone_properties(mgr, this, true));
            addWidget(new joint_properties(mgr, this, true));
            addWidget(new joint_properties(mgr, this, false));
            addWidget(new mixed_properties(mgr, this));
           
            for (auto* child : findChildren<abstract_properties_widget*>()) {
                child->init();
            }
            set(ui::sel_type::none, {});
        }

        abstract_properties_widget* current_props() const {
            return static_cast<abstract_properties_widget*>(currentWidget());
        }

        void set(ui::sel_type typ, const ui::canvas& canv) {
            setCurrentIndex(static_cast<int>(typ));
			auto* old_props = current_props();
            current_props()->set_selection(canv);
			old_props->lose_selection();
        }
    };
}

ui::selection_tool::selection_tool(tool_manager* mgr) :
    intitialized_(false),
    properties_(nullptr),
    state_(modal_state::none),
    abstract_tool(mgr, "selection", "arrow_icon.png", ui::tool_id::selection) {

}

void ui::selection_tool::activate(canvas& canv) {
    if (!intitialized_) {
        canv.connect(&canv, &canvas::selection_changed, 
            [this, &canv]() {
				const auto& sel = canv.selection();
                this->handle_sel_changed(canv);
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
    state_ = modal_state::none;
}

void ui::selection_tool::keyReleaseEvent(canvas & c, QKeyEvent * event) {
    if (is_in_modal_state() && event->key() == Qt::Key_Escape) {
        set_modal(modal_state::none, c);
    }
}

void ui::selection_tool::mousePressEvent(canvas& canv, QGraphicsSceneMouseEvent* event) {
    rubber_band_ = {};

    if (is_in_modal_state()) {
        auto props = static_cast<selection_properties*>(settings_widget());
        auto joint_props = static_cast<joint_properties*>(props->current_props());
        joint_props->start_dragging_joint_constraint(canv, event);
    }
}

void ui::selection_tool::mouseMoveEvent(canvas& canv, QGraphicsSceneMouseEvent* event) {
    if (is_in_modal_state()) {
        auto props = static_cast<selection_properties*>(settings_widget());
        auto joint_props = static_cast<joint_properties*>(props->current_props());
        if (joint_props->is_dragging_joint_constraint()) {
            joint_props->drag_joint_constraint(canv, event);
        }
    }
}

void ui::selection_tool::mouseReleaseEvent(canvas& canv, QGraphicsSceneMouseEvent* event) {
    if (is_in_modal_state()) {
        auto props = static_cast<selection_properties*>(settings_widget());
        auto joint_props = static_cast<joint_properties*>(props->current_props());
        if (joint_props->is_dragging_joint_constraint()) {
            joint_props->stop_dragging_joint_constraint(canv, event);
        }
        return;
    }
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

void ui::selection_tool::handle_sel_changed( const ui::canvas& canv) {
    auto type_of_sel = canv.selection_type();
    auto props = static_cast<selection_properties*>(properties_);
    props->set(type_of_sel, canv);
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
    if (!properties_) {
        properties_ = new selection_properties(manager());
    }
    return properties_;
}