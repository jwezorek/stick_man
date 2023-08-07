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
#include <numbers>

//#include <fstream> // TODO: remove
//#include <sstream> // TODO: remove
//#include <format> // TODO: remove

namespace r = std::ranges;
namespace rv = std::ranges::views;

/*------------------------------------------------------------------------------------------------*/

namespace {

    constexpr double k_tolerance = 0.00005;
	constexpr double k_default_rot_constraint_min = -std::numbers::pi / 2.0;
	constexpr double k_default_rot_constraint_max = std::numbers::pi / 2.0; 

/*
	// TODO: remove debug code
	class svg_debug {
		std::stringstream ss;
	public:
		svg_debug() {
			ss << "<svg height='500' width='500' xmlns='http://www.w3.org/2000/svg' version='1.1'>>\n";
			ss << "<g transform = 'matrix(1 0 0 -1 0 500)'>\n";
		}
		void add_line(sm::point u, sm::point v, std::string color) {
			ss << std::format(
				"<line x1 ='{0}' y1 ='{1}' x2='{2}' y2='{3}' style='stroke:{4};stroke-width:2' />\n",
				u.x, u.y, v.x, v.y, color);
		}

		std::string to_string() {
			ss << "</g>\n</svg>";
			return ss.str();
		}
	};

	// TODO: remove debug code
	sm::point pt_rel_to_line(sm::point u, sm::point v, double angle) {
		auto diff = v-u;
		auto rot = std::atan2(diff.y, diff.x);
		//auto canonicalize = sm::rotation_matrix(-rot) * sm::translation_matrix(-v);
		auto decanonicalize = sm::translation_matrix(v) * sm::rotation_matrix(rot);
		sm::point pt = { 30.0 * std::cos(angle), 30.0 * std::sin(angle) };
		return sm::transform(pt, decanonicalize);
	}

	// TODO: remove debug code
	void debug() {
		std::ofstream svg_file;
		svg_debug svg;

		sm::point u = { 20, 20 };
		sm::point v = { 60, 50 };
		sm::point target = { 60, 110 };
		auto max_angle = ui::degrees_to_radians(10.0);
		auto min_angle = ui::degrees_to_radians(-20.0);

		target = sm::apply_angle_constraint(u, v, target, min_angle, max_angle, true);

		svg.add_line(u, v, "green");
		svg.add_line(v, target, "red");
		svg.add_line(v, pt_rel_to_line(u, v, max_angle), "black");
		svg.add_line(v, pt_rel_to_line(u, v, min_angle), "black");

		svg_file.open("C:\\test\\debug.svg");
		svg_file << svg.to_string();
		svg_file.close();
	}

	// TODO: remove debug code
	void debug2(std::vector<ui::node_item*> v) {
		if (v.empty()) {
			return;
		}
		auto& node = v.front()->model();
		auto dbg_str = sm::debug(node);
		qDebug() << dbg_str.c_str();
	}
*/

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

	void set_rot_constraints(ui::canvas& canv, bool is_parent_relative, double min_angle, double max_angle) {
		auto bone_items = canv.selected_bones();
		for (auto& bone : to_model_objects(bone_items)) {
			bone.set_rotation_constraint(min_angle, max_angle, is_parent_relative);
		}
		canv.sync_to_model();
	}

	void remove_rot_constraints(ui::canvas& canv) {
		auto bone_items = canv.selected_bones();
		for (auto& bone : to_model_objects(bone_items)) {
			bone.remove_rotation_constraint();
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
                multi_(multi),
				tab_{},
				world_x_{},
				world_y_{} {
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

	class rot_constraint_box : public QGroupBox {
		ui::labeled_numeric_val* min_angle_;
		ui::labeled_numeric_val* max_angle_;
		QComboBox* mode_;
		ui::canvas& canv_;
	public:
		rot_constraint_box(ui::canvas& canv) :
				min_angle_(nullptr),
				max_angle_(nullptr),
				mode_(nullptr),
				canv_(canv) {
			this->setTitle("rotation constraint");
			auto* layout = new QVBoxLayout();
			this->setLayout(layout);
			layout->addWidget(min_angle_ = new ui::labeled_numeric_val("minimum angle", 0, -180, 180));
			layout->addWidget(max_angle_ = new ui::labeled_numeric_val("maximum angle", 0, -180, 180));
			layout->addWidget(new QLabel("mode"));
			layout->addWidget(mode_ = new QComboBox());
			mode_->addItem("relative to parent");
			mode_->addItem("relative to world");
			this->hide();

			//TODO: hook up controls
		}

		void set(bool parent_relative, double min, double max) {
			min_angle_->num_edit()->set_value(ui::radians_to_degrees(min));
			max_angle_->num_edit()->set_value(ui::radians_to_degrees(max));
			mode_->setCurrentIndex(parent_relative ? 0 : 1);

			set_rot_constraints(canv_, parent_relative, min, max);

			show();
		}

		void clear() {
			remove_rot_constraints(canv_);
			hide();
		}
	};

    class bone_properties : public abstract_properties_widget {
        ui::labeled_numeric_val* length_;
        QTabWidget* rotation_tab_ctrl_;
        ui::labeled_numeric_val* world_rotation_;
        ui::labeled_numeric_val* parent_rotation_;
		rot_constraint_box* constraint_box_;
		QPushButton* constraint_btn_;
        bool multi_;

		void add_or_delete_constraint() {
			bool is_adding = !constraint_box_->isVisible();
			constraint_btn_->setText(
				is_adding ? "remove rotation constraint" : "add rotation constraint"
			);
			if (is_adding) {
				constraint_box_->set(
					true, k_default_rot_constraint_min, k_default_rot_constraint_max
				);
			} else {
				constraint_box_->clear();
			}
		}

    public:
        bone_properties(ui::tool_manager* mgr, QWidget* parent, bool multi) : 
                abstract_properties_widget(
                    mgr,
                    parent, 
                    (multi) ? "selected bones" : "selected bone"
                ),
                multi_(multi),
				length_(nullptr),
				rotation_tab_ctrl_(nullptr),
				world_rotation_(nullptr),
				parent_rotation_(nullptr),
				constraint_box_(nullptr),
				constraint_btn_(nullptr) {
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

			if (!multi_) {
				layout_->addWidget(constraint_box_ = new rot_constraint_box(canvas()));
				layout_->addWidget(constraint_btn_ = new QPushButton("add rotation constraint"));

				connect(constraint_btn_, &QPushButton::clicked,
					this, &bone_properties::add_or_delete_constraint);
			}

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


    class selection_properties : public QStackedWidget {
    public:
        selection_properties(ui::tool_manager* mgr) { 
            addWidget(new no_properties(mgr, this));
            addWidget(new node_properties(mgr, this, false));
            addWidget(new node_properties(mgr, this, true));
            addWidget(new bone_properties(mgr, this, false));
            addWidget(new bone_properties(mgr, this, true));
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
			auto* old_props = current_props();
            setCurrentIndex(static_cast<int>(typ));

			old_props->lose_selection();
            current_props()->set_selection(canv);
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
		//TODO
		/*
        auto props = static_cast<selection_properties*>(settings_widget());
        auto joint_props = static_cast<joint_properties*>(props->current_props());
        joint_props->start_dragging_joint_constraint(canv, event);
		*/
    }
}

void ui::selection_tool::mouseMoveEvent(canvas& canv, QGraphicsSceneMouseEvent* event) {
	//TODO
	/*
    if (is_in_modal_state()) {
        auto props = static_cast<selection_properties*>(settings_widget());
        auto joint_props = static_cast<joint_properties*>(props->current_props());
        if (joint_props->is_dragging_joint_constraint()) {
            joint_props->drag_joint_constraint(canv, event);
        }
    }
	*/
}

void ui::selection_tool::mouseReleaseEvent(canvas& canv, QGraphicsSceneMouseEvent* event) {
    if (is_in_modal_state()) {
		//TODO
		/*
        auto props = static_cast<selection_properties*>(settings_widget());
        auto joint_props = static_cast<joint_properties*>(props->current_props());
        if (joint_props->is_dragging_joint_constraint()) {
            joint_props->stop_dragging_joint_constraint(canv, event);
        }
        return;
		*/
    }
    bool shift_down = event->modifiers().testFlag(Qt::ShiftModifier);
    bool alt_down = event->modifiers().testFlag(Qt::AltModifier);
    if (rubber_band_) {
        handle_drag(canv, *rubber_band_, shift_down, alt_down);
    } else {
        handle_click(canv, event->scenePos(), shift_down, alt_down);
    }
	canv.sync_to_model();
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