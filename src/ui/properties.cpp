#include "properties.h"
#include "canvas.h"
#include "canvas_item.h"
#include "util.h"
#include "stick_man.h"
#include "skeleton_pane.h"
#include "project.h"
#include <unordered_map>
#include <unordered_set>
#include <ranges>
#include <functional>
#include <numbers>
#include <qdebug.h>

/*------------------------------------------------------------------------------------------------*/

using namespace std::placeholders;
namespace r = std::ranges;
namespace rv = std::ranges::views;

namespace {

	constexpr double k_tolerance = 0.00005;
	constexpr double k_default_rot_constraint_min = -std::numbers::pi / 2.0;
	constexpr double k_default_rot_constraint_span = std::numbers::pi;

	ui::selection_type type_of_selection(const ui::selection_set& sel) {

		if (sel.empty()) {
			return ui::selection_type::none;
		}

		if (sel.size() == 1 && dynamic_cast<ui::skeleton_item*>(*sel.begin())) {
			return ui::selection_type::skeleton;
		}

		bool has_node = false;
		bool has_bone = false;
		for (auto itm_ptr : sel) {
			has_node = dynamic_cast<ui::node_item*>(itm_ptr) || has_node;
			has_bone = dynamic_cast<ui::bone_item*>(itm_ptr) || has_bone;
			if (has_node && has_bone) {
				return ui::selection_type::mixed;
			}
		}
		return has_node ? ui::selection_type::node : ui::selection_type::bone;
	}

	auto to_model_objects(r::input_range auto&& itms) {
		return itms |
			rv::transform(
				[](auto itm)->auto& {
					return itm->model();
				}
		);
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
			std::not_fn(std::bind(is_approximately_equal, _1, first_val, tolerance))
		);
		return  (first_not_equal == vals.end()) ?
			std::optional<double>{first_val} : std::optional<double>{};
	}

	void set_rot_constraints(ui::project& proj, ui::canvas& canv, bool is_parent_relative, double start, double span) {
        proj.transform(
            to_model_refs(canv.selected_bones()) | r::to<std::vector<sm::bone_ref>>(),
            [&](sm::bone& bone) {
                bone.set_rotation_constraint(start, span, is_parent_relative);
            }
        );
	}

	void remove_rot_constraints(ui::project& proj, ui::canvas& canv) {
        proj.transform(
            to_model_refs(canv.selected_bones()) | r::to<std::vector<sm::bone_ref>>(),
            [](sm::bone& bone) {
                bone.remove_rotation_constraint();
            }
        );
	}

    std::vector<sm::bone_ref> topological_sort_selected_bones(ui::canvas& canv) {
        auto bone_items = canv.selected_bones();
        std::unordered_set<sm::bone*> selected = ui::to_model_ptrs(bone_items) |
            r::to< std::unordered_set<sm::bone*>>();
        std::vector<sm::bone_ref> ordered_bones;
        auto skeletons = canv.skeleton_items();

        for (auto skel_item : skeletons) {
            auto& skel = skel_item->model();
            sm::visit_bones(skel.root_node().get(),
                [&](auto& bone)->sm::visit_result {
                    if (selected.contains(&bone)) {
                        ordered_bones.push_back(bone);
                    }
                    return sm::visit_result::continue_traversal;
                }
            );
        }

        return ordered_bones;
    }

    void set_selected_bone_length(ui::project& proj, ui::canvas& canv, double new_length) {
        proj.transform(
            topological_sort_selected_bones(canv),
            [new_length](sm::bone_ref bone) {
                bone.get().set_length(new_length);
            }
        );
    }

	void set_selected_bone_rotation(ui::project& proj, ui::canvas& canv, double theta) {

        // sort bones into topological order and then set world rotation...
        proj.transform(
            topological_sort_selected_bones(canv),
            [theta](sm::bone_ref bone) {
                bone.get().set_world_rotation(theta);
            }
        );
	}

	class no_properties : public ui::abstract_properties_widget {
	public:
		no_properties(const ui::current_canvas_fn& fn,
            ui::selection_properties* parent) : 
                abstract_properties_widget(fn, parent, "no selection") {}
		void set_selection(const ui::canvas& canv) override {}
	};

	class skeleton_properties : public ui::abstract_properties_widget {
		ui::labeled_field* name_;
	public:
		skeleton_properties(const ui::current_canvas_fn& fn, ui::selection_properties* parent) :
			abstract_properties_widget(fn, parent, "skeleton selection") {}

		void populate(ui::project& proj) override {
			layout_->addWidget(
				name_ = new ui::labeled_field("   name", "")
			);
			name_->set_color(QColor("yellow"));
			name_->value()->set_validator(
				[this](const std::string& new_name)->bool {
					return parent_->skel_pane().validate_props_name_change(new_name);
				}
			);

            connect(&proj, &ui::project::name_changed,
                [this](ui::skel_piece piece, const std::string& new_name) {
                    handle_rename(piece, name_->value(), new_name);
                }
            );

			connect(
                name_->value(), &ui::string_edit::value_changed, 
                this, &skeleton_properties::do_property_name_change
			);
		}

		void set_selection(const ui::canvas& canv) override {
			auto* skel_item = canv.selected_skeleton();
			name_->set_value(skel_item->model().name().c_str());
		}
	};

	class node_position_tab : public ui::tabbed_values {
	private:
        ui::current_canvas_fn get_curr_canv_;

		static sm::point origin_by_tab(int tab_index, const sm::node& selected_node) {
			if (tab_index == 1) {
				// skeleton relative...
				const auto& owner = selected_node.owner().get();
				return owner.root_node().get().world_pos();
			}
			else {
				// parent relative...
				auto parent_bone = selected_node.parent_bone();
				return (parent_bone) ?
					parent_bone->get().parent_node().world_pos() :
					sm::point(0, 0);
			}
		}

		double to_nth_tab(int n, int field, double val) override {
			auto selected_nodes = get_curr_canv_().selected_nodes();
			if (selected_nodes.size() != 1 || n == 0) {
				return val;
			}

			auto origin = origin_by_tab(n, selected_nodes.front()->model());
			if (field == 0) {
				return val - origin.x;
			} else {
				return val - origin.y;
			}
		}

		double from_nth_tab(int n, int field, double val) override {
			auto selected_nodes = get_curr_canv_().selected_nodes();
			if (selected_nodes.size() != 1 || n == 0) {
				return val;
			}

			auto origin = origin_by_tab(n, selected_nodes.front()->model());
			if (field == 0) {
				return origin.x + val;
			} else {
				return origin.y + val;
			}
		}

	public:
		node_position_tab(const ui::current_canvas_fn& fn) :
            get_curr_canv_(fn),
			ui::tabbed_values(nullptr,
					{ "world", "skeleton", "parent" }, {
						{"x", 0.0, -1500.0, 1500.0},
						{"y", 0.0, -1500.0, 1500.0}
					}, 135
			)
		{}
	};

	class node_properties : public ui::single_or_multi_props_widget {
		ui::labeled_field* name_;
		node_position_tab* positions_;

		static double world_coordinate_to_rel(int index, double val) {
			return val;
		}

	public:
		node_properties(const ui::current_canvas_fn& fn, ui::selection_properties* parent) :
			single_or_multi_props_widget(
				fn,
				parent,
				"selected nodes"
			),
			positions_{} {
		}

		void populate(ui::project& proj) override {
			layout_->addWidget(
				name_ = new ui::labeled_field("   name", "")
			);
			name_->set_color(QColor("yellow"));
			name_->value()->set_validator(
				[this](const std::string& new_name)->bool {
					return parent_->skel_pane().validate_props_name_change(new_name);
				}
			);

			connect(name_->value(), &ui::string_edit::value_changed,
                this, &node_properties::do_property_name_change
			);

			layout_->addWidget(positions_ = new node_position_tab(get_current_canv_));

			connect(positions_, &ui::tabbed_values::value_changed,
				[this](int field) {
					auto& canv = get_current_canv_();
					auto v = positions_->value(field);
                    auto sel = ui::to_model_ptrs(canv.selected_nodes()) |
                        rv::transform([](auto* ptr)->sm::node_ref {return std::ref(*ptr); }) |
                        r::to<std::vector<sm::node_ref>>();
					if (field == 0) {
                        proj_->transform(sel,
                            [&](sm::node& node) {
                                auto y = node.world_y();
                                node.set_world_pos(sm::point(*v, y));
                            }
                        );
					} else {
                        proj_->transform(sel,
                            [&](sm::node& node) {
                                auto x = node.world_x();
                                node.set_world_pos(sm::point(x, *v));
                            }
                        );
					} 
					canv.sync_to_model();
				}
			);

            connect(&proj, &ui::project::name_changed,
                [this](ui::skel_piece piece, const std::string& new_name) {
                    handle_rename(piece, name_->value(), new_name);
                }
            );
		}

		void set_selection_common(const ui::canvas& canv) {
			const auto& sel = canv.selection();
			auto nodes = to_model_objects(ui::as_range_view_of_type<ui::node_item>(sel));
			auto x_pos = get_unique_val(nodes |
				rv::transform([](sm::node& n) {return n.world_x(); }));
			auto y_pos = get_unique_val(nodes |
				rv::transform([](sm::node& n) {return n.world_y(); }));

			positions_->set_value(0, x_pos);
			positions_->set_value(1, y_pos);
		}

		void set_selection_single(const ui::canvas& canv) {
			name_->show();
			auto& node = canv.selected_nodes().front()->model();
			name_->set_value(node.name().c_str());
			positions_->unlock();
		}

		void set_selection_multi(const ui::canvas& canv) {
			name_->hide();
			positions_->lock_to_primary_tab();
		}

		bool is_multi(const ui::canvas& canv) {
			return canv.selected_nodes().size() > 1;
		}

		void lose_selection() override {
		}
	};

	class rot_constraint_box : public QGroupBox {
		QPushButton* btn_;
		ui::labeled_numeric_val* start_angle_;
		ui::labeled_numeric_val* span_angle_;
		QComboBox* mode_;
		ui::canvas& canv_;
        ui::project& proj_;

		bool is_constraint_relative_to_parent() const {
			return mode_->currentIndex() == 0;
		}

		double constraint_start_angle() const {
			if (!start_angle_->num_edit()->value()) {
				throw std::runtime_error("attempted to get value from indeterminate value box");
			}
			return ui::degrees_to_radians(*start_angle_->num_edit()->value());
		}

		double constraint_span_angle() const {
			if (!span_angle_->num_edit()->value()) {
				throw std::runtime_error("attempted to get value from indeterminate value box");
			}
			return ui::degrees_to_radians(*span_angle_->num_edit()->value());
		}

		void set_constraint_angle(ui::project& proj, ui::canvas& canv, double val, bool is_start_angle) {
			auto theta = ui::degrees_to_radians(val);
			auto start_angle = is_start_angle ? theta : constraint_start_angle();
			auto span_angle = is_start_angle ? constraint_span_angle() : theta;
			set_rot_constraints(proj, canv,
				is_constraint_relative_to_parent(), start_angle, span_angle);
			canv.sync_to_model();
		}

		void set_constraint_mode(ui::project& proj, ui::canvas& canv, bool relative_to_parent) {
			set_rot_constraints(proj, canv,
				relative_to_parent, constraint_start_angle(), constraint_span_angle()
			);
			canv.sync_to_model();
		}

		void showEvent(QShowEvent* event) override
		{
			QWidget::showEvent(event);
			btn_->setText("remove rotation constraint");
		}

		void hideEvent(QHideEvent* event) override
		{
			QWidget::hideEvent(event);
			btn_->setText("add rotation constraint");
		}

	public:
		rot_constraint_box(QPushButton* btn, ui::canvas& canv, ui::project& proj) :
			    btn_(btn),
			    start_angle_(nullptr),
			    span_angle_(nullptr),
			    mode_(nullptr),
			    canv_(canv),
                proj_(proj) {
			this->setTitle("rotation constraint");
			btn_->setText("add rotation constraint");
			auto* layout = new QVBoxLayout();
			this->setLayout(layout);
			layout->addWidget(start_angle_ = new ui::labeled_numeric_val("start angle", 0, -180, 180));
			layout->addWidget(span_angle_ = new ui::labeled_numeric_val("span angle", 0, 0, 360));
			layout->addWidget(new QLabel("mode"));
			layout->addWidget(mode_ = new QComboBox());
			mode_->addItem("relative to parent");
			mode_->addItem("relative to world");
			this->hide();

			connect(start_angle_->num_edit(), &ui::number_edit::value_changed,
				[this](double v) {
					set_constraint_angle(proj_, canv_, v, true);
				}
			);

			connect(span_angle_->num_edit(), &ui::number_edit::value_changed,
				[this](double v) {
					set_constraint_angle(proj_, canv_, v, false);
				}
			);

			connect(mode_, &QComboBox::currentIndexChanged,
				[this](int new_index) {
					set_constraint_mode(proj_, canv_, new_index == 0);
				}
			);
		}

		void set(bool parent_relative, double start, double span) {
			start_angle_->num_edit()->set_value(ui::radians_to_degrees(start));
			span_angle_->num_edit()->set_value(ui::radians_to_degrees(span));
			mode_->setCurrentIndex(parent_relative ? 0 : 1);
			show();
		}
	};

	std::function<void()> make_select_node_fn(const ui::current_canvas_fn& get_current_canv, 
            bool parent_node) {
		return [get_current_canv, parent_node]() {
			auto& canv = get_current_canv();
			auto bones = canv.selected_bones();
			if (bones.size() != 1) {
				return;
			}
			sm::bone& bone = bones.front()->model();
			auto& node_itm = ui::item_from_model<ui::node_item>(
				(parent_node) ? bone.parent_node() : bone.child_node()
			);
			canv.set_selection(&node_itm, true);
			};
	}

	class rotation_tab : public ui::tabbed_values {
	private:
		ui::current_canvas_fn get_current_canv_;

		double convert_to_or_from_parent_coords(double val, bool to_parent) {
			auto bone_selection = get_current_canv_().selected_bones();
			if (bone_selection.size() != 1) {
				return val;
			}
			auto& bone = bone_selection.front()->model();
			auto parent = bone.parent_bone();
			if (!parent) {
				return val;
			}
			auto parent_rot = ui::radians_to_degrees(parent->get().world_rotation());
			return (to_parent) ?
				val - parent_rot :
				val + parent_rot;
		}

	public:
		rotation_tab(const ui::current_canvas_fn& get_current_canv) :
            get_current_canv_(get_current_canv),
			ui::tabbed_values(nullptr,
				{"world", "parent"}, {
					{"rotation", 0.0, -180.0, 180.0}
				}, 100
			)
		{}

		double to_nth_tab(int tab, int, double val) override {
			if (tab == 0) {
				return val;
			}
			return convert_to_or_from_parent_coords(val, true);
		}

		double from_nth_tab(int tab, int, double val) override {
			if (tab == 0) {
				return val;
			}
			return convert_to_or_from_parent_coords(val, false);
		}
	};

	class bone_properties : public ui::single_or_multi_props_widget {
		ui::labeled_numeric_val* length_;
		ui::labeled_field* name_;
		ui::labeled_hyperlink* u_;
		ui::labeled_hyperlink* v_;
		QWidget* nodes_;
		rotation_tab* rotation_;
		rot_constraint_box* constraint_box_;
		QPushButton* constraint_btn_;

		void add_or_delete_constraint(ui::project& proj, ui::canvas& canv) {
			bool is_adding = !constraint_box_->isVisible();
			if (is_adding) {
                set_rot_constraints(proj, canv, true,
                    k_default_rot_constraint_min, k_default_rot_constraint_span);
				constraint_box_->set(
					true, k_default_rot_constraint_min, k_default_rot_constraint_span
				);
			} else {
                remove_rot_constraints(proj, canv);
                constraint_box_->hide();
			}
			get_current_canv_().sync_to_model();
		}

	public:
		bone_properties(const ui::current_canvas_fn& fn, ui::selection_properties* parent) :
			single_or_multi_props_widget(
				fn,
				parent,
				"selected bones"
			),
			name_(nullptr),
			length_(nullptr),
			rotation_(nullptr),
			constraint_box_(nullptr),
			constraint_btn_(nullptr),
			u_(nullptr),
			v_(nullptr),
			nodes_(nullptr) {
		}

		void populate(ui::project& proj) override {
			layout_->addWidget(
				name_ = new ui::labeled_field("   name", "")
			);
			name_->set_color(QColor("yellow"));
			name_->value()->set_validator(
				[this](const std::string& new_name)->bool {
					return parent_->skel_pane().validate_props_name_change(new_name);
				}
			);

			nodes_ = new QWidget();
			auto vert_pair = new QVBoxLayout(nodes_);
			vert_pair->addWidget(new QLabel("nodes"));
			vert_pair->addWidget(u_ = new ui::labeled_hyperlink("   u", ""));
			vert_pair->addWidget(v_ = new ui::labeled_hyperlink("   v", ""));
			vert_pair->setAlignment(Qt::AlignTop);
			vert_pair->setSpacing(0);

			layout_->addWidget(nodes_);

			layout_->addWidget(
				length_ = new ui::labeled_numeric_val("length", 0.0, 0.0, 1500.0)
			);

			layout_->addWidget(rotation_ = new rotation_tab(get_current_canv_));


			constraint_btn_ = new QPushButton();
			layout_->addWidget(constraint_box_ = new rot_constraint_box(
                constraint_btn_, get_current_canv_(), proj)
            );
			layout_->addWidget(constraint_btn_);

            connect(constraint_btn_, &QPushButton::clicked,
                [&]() {
                    add_or_delete_constraint(proj, get_current_canv_());
                }
            );

            connect(&proj, &ui::project::name_changed,
                [this](ui::skel_piece piece, const std::string& new_name) {
                    handle_rename(piece, name_->value(), new_name);
                }
            );

			connect(name_->value(), &ui::string_edit::value_changed,
                this, &bone_properties::do_property_name_change
			);

			connect(u_->hyperlink(), &QPushButton::clicked,
				make_select_node_fn(get_current_canv_, true)
			);
			connect(v_->hyperlink(), &QPushButton::clicked,
				make_select_node_fn(get_current_canv_, false)
			);

			connect(length_->num_edit(), &ui::number_edit::value_changed,
                [this, &proj](double val) {
                    auto& canv = get_current_canv_();
                    set_selected_bone_length(proj, canv, val);
                }
			);
			connect(rotation_, &ui::tabbed_values::value_changed,
				[this, &proj](int) {
					auto rot = rotation_->value(0);
					set_selected_bone_rotation( 
                        proj, 
                        get_current_canv_(), 
                        ui::degrees_to_radians(*rot) 
                    );
				}
			);
		}

		bool is_multi(const ui::canvas& canv) override {
			return canv.selected_bones().size() > 1;
		}

		void set_selection_common(const ui::canvas& canv) override {
			const auto& sel = canv.selection();
			auto bones = to_model_objects(ui::as_range_view_of_type<ui::bone_item>(sel));
			auto length = get_unique_val(bones |
				rv::transform([](sm::bone& b) {return b.scaled_length(); }));
			length_->num_edit()->set_value(length);

			auto world_rot = get_unique_val(bones |
				rv::transform([](sm::bone& b) {return b.world_rotation(); }));
			rotation_->set_value(0, world_rot.transform(ui::radians_to_degrees));
		}

		void set_selection_multi(const ui::canvas& canv) override {

			name_->hide();
			nodes_->hide();
			constraint_box_->hide();
			constraint_btn_->hide();
			rotation_->lock_to_primary_tab();

		}

		void set_selection_single(const ui::canvas& canv) override {
			name_->show();
			nodes_->show();
			constraint_btn_->show();
			rotation_->unlock();
			auto& bone = canv.selected_bones().front()->model();
			name_->set_value(bone.name().c_str());
			auto rot_constraint = bone.rotation_constraint();
			if (rot_constraint) {
				constraint_box_->show();
				constraint_box_->set(
					rot_constraint->relative_to_parent,
					rot_constraint->start_angle,
					rot_constraint->span_angle
				);
			} else {
				constraint_box_->hide();
			}
			u_->hyperlink()->setText(bone.parent_node().name().c_str());
			v_->hyperlink()->setText(bone.child_node().name().c_str());
		}

		void lose_selection() override {
			if (constraint_box_) {
				constraint_box_->hide();
			}
		}
	};

	class mixed_properties : public ui::abstract_properties_widget {
	private:
		node_properties* nodes_;
		bone_properties* bones_;
	public:
		mixed_properties(const ui::current_canvas_fn& fn, ui::selection_properties* parent) :
			abstract_properties_widget(fn, parent, "") {
		}

		void populate(ui::project& proj) override {
			layout_->addWidget(
				nodes_ = new node_properties(get_current_canv_, parent_)
			);
			layout_->addWidget( ui::horz_separator() );
			layout_->addWidget(
				bones_ = new bone_properties(get_current_canv_, parent_)
			);
			nodes_->populate(proj);
			bones_->populate(proj);
			nodes_->show();
			bones_->show();
		}

		void set_selection(const ui::canvas& canv) override {
			nodes_->set_selection(canv);
			bones_->set_selection(canv);
		}

		void lose_selection() override {
			nodes_->lose_selection();
			bones_->lose_selection();
		}
	};
}

/*------------------------------------------------------------------------------------------------*/

ui::abstract_properties_widget::abstract_properties_widget(
            const current_canvas_fn& fn,
            selection_properties* parent, QString title) :
		QWidget(parent),
        get_current_canv_(fn),
        parent_(parent),
		title_(nullptr),
		layout_(new QVBoxLayout(this)) {
	layout_->addWidget(title_ = new QLabel(title));
	hide();
}

void ui::abstract_properties_widget::do_property_name_change(const std::string& new_name) {
    auto maybe_piece = selected_single_model(get_current_canv_());
    if (!maybe_piece) {
        return;
    }
    proj_->rename(*maybe_piece, new_name);
}

void ui::abstract_properties_widget::handle_rename(ui::skel_piece p, ui::string_edit* name_edit, const std::string& new_name)
{
    auto maybe_piece = selected_single_model(get_current_canv_());
    if (!maybe_piece) {
        return;
    }
    if (!identical_pieces(*maybe_piece, p)) {
        return;
    }
    if (name_edit->text().toStdString() != new_name) {
        name_edit->setText(new_name.c_str());
    }
}

void ui::abstract_properties_widget::set_title(QString title) {
	title_->setText(title);
}

void ui::abstract_properties_widget::init(project& proj) {
	populate(proj);
	layout_->addStretch();
    proj_ = &proj;
}

void ui::abstract_properties_widget::populate(project& proj) {

}

void ui::abstract_properties_widget::lose_selection() {}

/*------------------------------------------------------------------------------------------------*/

ui::single_or_multi_props_widget::single_or_multi_props_widget(
        const current_canvas_fn& fn, selection_properties* parent, QString title) :
	abstract_properties_widget(fn, parent, title)
{}

void ui::single_or_multi_props_widget::set_selection(const ui::canvas& canv) {
	set_selection_common(canv);
	if (is_multi(canv)) {
		set_selection_multi(canv);
	} else {
		set_selection_single(canv);
	}
}

/*------------------------------------------------------------------------------------------------*/

ui::selection_properties::selection_properties(const current_canvas_fn& fn, skeleton_pane* sp) :
        skel_pane_(sp),
		props_{
			{selection_type::none, new no_properties(fn, this)},
			{selection_type::node, new node_properties(fn, this)},
			{selection_type::bone, new bone_properties(fn, this)},
			{selection_type::skeleton, new skeleton_properties(fn, this)},
			{selection_type::mixed, new mixed_properties(fn, this)}
		} {
	for (const auto& [key, prop_box] : props_) {
        QScrollArea* scroller = new QScrollArea();
        scroller->setWidget(prop_box);
        scroller->setWidgetResizable(true);
		addWidget(scroller);
	}
}

ui::abstract_properties_widget* ui::selection_properties::current_props() const {
	return static_cast<abstract_properties_widget*>(
        static_cast<QScrollArea*>(currentWidget())->widget()
    );
}

void ui::selection_properties::set(const ui::canvas& canv) {
	auto* old_props = current_props();

    QScrollArea* scroller = nullptr;
    QWidget* widg = props_.at(type_of_selection(canv.selection()));
    while (scroller == nullptr) {
        scroller = dynamic_cast<QScrollArea*>(widg);
        widg = widg->parentWidget();
    }

	setCurrentWidget(
        scroller
    );

	old_props->lose_selection();
	current_props()->set_selection(canv);

	auto bone_items = ui::as_range_view_of_type<ui::bone_item>(canv.selection());
	for (ui::bone_item* bi : bone_items) {
		auto* tvi = bi->treeview_item();
	}
}

void ui::selection_properties::handle_selection_changed(canvas& canv) {
    set(canv);
}

void ui::selection_properties::init(canvas_manager& canvases, project& proj)
{
    for (const auto& [key, prop_box] : props_) {
        prop_box->init(proj);
    }
    set(canvases.active_canvas());
    connect(&canvases, &canvas_manager::selection_changed,
        this,
        &selection_properties::handle_selection_changed
    );
}

ui::skeleton_pane& ui::selection_properties::skel_pane() {
    return *skel_pane_;
}