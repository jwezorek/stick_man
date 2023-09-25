#include "properties.h"
#include "canvas.h"
#include "canvas_item.h"
#include "util.h"
#include "stick_man.h"
#include "skeleton_pane.h"
#include <unordered_map>
#include <unordered_set>
#include <ranges>
#include <functional>
#include <numbers>

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

	void set_rot_constraints(ui::canvas& canv, bool is_parent_relative, double start, double span) {
		auto bone_items = canv.selected_bones();
		for (auto& bone : to_model_objects(bone_items)) {
			bone.set_rotation_constraint(start, span, is_parent_relative);
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

	// TODO: the following has bad computational complexity
	// maybe do this in one pass if it is ever an issue.
	// (the way it is now it is like O(n * (V + E)) where n is the number
	// of selected bones and V and E are the number of vertices 
	// and edges in skeletons because each call to set_world_rotation
	// is doing a traversal of the skeleton dowwnstream of the bone)

	void set_selected_bone_rotation(ui::stick_man& main_window, double theta) {
        auto& canv = main_window.canvases().active_canvas();
		auto bone_items = canv.selected_bones();
		std::unordered_set<sm::bone*> selected = ui::to_model_ptrs( rv::all(bone_items) ) |
			r::to< std::unordered_set<sm::bone*>>();
		std::vector<sm::bone*> ordered_bones;
		auto& world = main_window.sandbox();
		
		for (auto skel : world.skeletons()) {
			sm::visit_bones(skel.get().root_node().get(),
				[&](auto& bone)->bool {
					if (selected.contains(&bone)) {
						ordered_bones.push_back(&bone);
					}
					return true;
				}
			);
		}

		r::reverse(ordered_bones);
		for (auto* bone : ordered_bones) {
			bone->set_world_rotation(theta);
		}

		canv.sync_to_model();
	}

	class no_properties : public ui::abstract_properties_widget {
	public:
		no_properties(ui::stick_man* mw, QWidget* parent) : abstract_properties_widget(mw, parent, "no selection") {}
		void set_selection(const ui::canvas& canv) override {}
	};

	class skeleton_properties : public ui::abstract_properties_widget {
		ui::labeled_field* name_;
	public:
		skeleton_properties(ui::stick_man* mw, QWidget* parent) :
			abstract_properties_widget(mw, parent, "skeleton selection") {}

		void populate() override {
			layout_->addWidget(
				name_ = new ui::labeled_field("   name", "")
			);
			name_->set_color(QColor("yellow"));
			name_->value()->set_validator(
				[this](const std::string& new_name)->bool {
					return main_wnd_->skel_pane().validate_props_name_change(new_name);
				}
			);

			connect(name_->value(), &ui::string_edit::value_changed,
				[this]() {
					main_wnd_->skel_pane().handle_props_name_change(
						name_->value()->text().toStdString()
					);
				}
			);
		}

		void set_selection(const ui::canvas& canv) override {
			auto* skel_item = canv.selected_skeleton();
			name_->set_value(skel_item->model().name().c_str());
		}
	};

	class node_position_tab : public ui::tabbed_values {
	private:
		ui::stick_man* main_wnd_;

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
			auto selected_nodes = main_wnd_->canvases().active_canvas().selected_nodes();
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
			auto selected_nodes = main_wnd_->canvases().active_canvas().selected_nodes();
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
		node_position_tab(ui::stick_man* main_wnd) :
			ui::tabbed_values(nullptr,
					{ "world", "skeleton", "parent" }, {
						{"x", 0.0, -1500.0, 1500.0},
						{"y", 0.0, -1500.0, 1500.0}
					}, 135
			),
			main_wnd_(main_wnd)
		{}
	};

	class node_properties : public ui::single_or_multi_props_widget {
		ui::labeled_field* name_;
		node_position_tab* positions_;

		static double world_coordinate_to_rel(int index, double val) {
			return val;
		}

	public:
		node_properties(ui::stick_man* mw, QWidget* parent) :
			single_or_multi_props_widget(
				mw,
				parent,
				"selected nodes"
			),
			positions_{} {
		}

		void populate() override {
			layout_->addWidget(
				name_ = new ui::labeled_field("   name", "")
			);
			name_->set_color(QColor("yellow"));
			name_->value()->set_validator(
				[this](const std::string& new_name)->bool {
					return main_wnd_->skel_pane().validate_props_name_change(new_name);
				}
			);

			connect(name_->value(), &ui::string_edit::value_changed,
				[this]() {
					main_wnd_->skel_pane().handle_props_name_change(
						name_->value()->text().toStdString()
					);
				}
			);

			layout_->addWidget(positions_ = new node_position_tab( main_wnd_ ));

			connect(positions_, &ui::tabbed_values::value_changed,
				[this](int field) {
					auto& canv = main_wnd_->canvases().active_canvas();
					auto v = positions_->value(field);
					if (field == 0) {
						canv.transform_selection(
							[v](ui::node_item* ni) {
								auto y = ni->model().world_y();
								ni->model().set_world_pos(sm::point(*v, y));
							}
						);
					} else {
						canv.transform_selection(
							[v](ui::node_item* ni) {
								auto x = ni->model().world_x();
								ni->model().set_world_pos(sm::point(x, *v));
							}
						);
					}
					canv.sync_to_model();
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

		void set_constraint_angle(ui::canvas& canv, double val, bool is_start_angle) {
			auto theta = ui::degrees_to_radians(val);
			auto start_angle = is_start_angle ? theta : constraint_start_angle();
			auto span_angle = is_start_angle ? constraint_span_angle() : theta;
			set_rot_constraints(canv,
				is_constraint_relative_to_parent(), start_angle, span_angle);
			canv.sync_to_model();
		}

		void set_constraint_mode(ui::canvas& canv, bool relative_to_parent) {
			set_rot_constraints(canv,
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
		rot_constraint_box(QPushButton* btn, ui::canvas& canv) :
			btn_(btn),
			start_angle_(nullptr),
			span_angle_(nullptr),
			mode_(nullptr),
			canv_(canv) {
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
				[this, &canv](double v) {
					set_constraint_angle(canv, v, true);
				}
			);

			connect(span_angle_->num_edit(), &ui::number_edit::value_changed,
				[this, &canv](double v) {
					set_constraint_angle(canv, v, false);
				}
			);

			connect(mode_, &QComboBox::currentIndexChanged,
				[this, &canv](int new_index) {
					set_constraint_mode(canv, new_index == 0);
				}
			);
		}

		void set(bool parent_relative, double start, double span) {
			start_angle_->num_edit()->set_value(ui::radians_to_degrees(start));
			span_angle_->num_edit()->set_value(ui::radians_to_degrees(span));
			mode_->setCurrentIndex(parent_relative ? 0 : 1);

			set_rot_constraints(canv_, parent_relative, start, span);

			show();
		}

		void clear() {
			remove_rot_constraints(canv_);
			hide();
		}
	};

	std::function<void()> make_select_node_fn(ui::stick_man& mw, bool parent_node) {
		return [&mw, parent_node]() {
			auto& canv = mw.canvases().active_canvas();
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
		ui::stick_man* main_wnd_;

		double convert_to_or_from_parent_coords(double val, bool to_parent) {
			auto bone_selection = main_wnd_->canvases().active_canvas().selected_bones();
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
		rotation_tab(ui::stick_man* mw) :
			ui::tabbed_values(nullptr,
				{"world", "parent"}, {
					{"rotation", 0.0, -180.0, 180.0}
				}, 100
			),
			main_wnd_(mw) 
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

		void add_or_delete_constraint() {
			bool is_adding = !constraint_box_->isVisible();
			if (is_adding) {
				constraint_box_->set(
					true, k_default_rot_constraint_min, k_default_rot_constraint_span
				);
			}
			else {
				constraint_box_->clear();
			}
			canvas().sync_to_model();
		}

	public:
		bone_properties(ui::stick_man* mw, QWidget* parent) :
			single_or_multi_props_widget(
				mw,
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

		void populate() override {
			layout_->addWidget(
				name_ = new ui::labeled_field("   name", "")
			);
			name_->set_color(QColor("yellow"));
			name_->value()->set_validator(
				[this](const std::string& new_name)->bool {
					return main_wnd_->skel_pane().validate_props_name_change(new_name);
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

			layout_->addWidget(rotation_ = new rotation_tab(main_wnd_));


			constraint_btn_ = new QPushButton();
			layout_->addWidget(constraint_box_ = new rot_constraint_box(constraint_btn_, canvas()));
			layout_->addWidget(constraint_btn_);

			connect(constraint_btn_, &QPushButton::clicked,
				this, &bone_properties::add_or_delete_constraint);

			connect(name_->value(), &ui::string_edit::value_changed,
				[this]() {
					main_wnd_->skel_pane().handle_props_name_change(
						name_->value()->text().toStdString()
					);
				}
			);

			connect(u_->hyperlink(), &QPushButton::clicked,
				make_select_node_fn(*main_wnd_, true)
			);
			connect(v_->hyperlink(), &QPushButton::clicked,
				make_select_node_fn(*main_wnd_, false)
			);

			connect(length_->num_edit(), &ui::number_edit::value_changed,
                [this](double val) {
                    auto& canv = main_wnd_->canvases().active_canvas();
                    set_bone_length(canv, val);
                }
			);
			connect(rotation_, &ui::tabbed_values::value_changed,
				[this](int) {
					auto rot = rotation_->value(0);
					set_selected_bone_rotation( *main_wnd_, ui::degrees_to_radians(*rot) );
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
		mixed_properties(ui::stick_man* mw, QWidget* parent) :
			abstract_properties_widget(mw, parent, "") {
		}

		void populate() override {
			layout_->addWidget(
				nodes_ = new node_properties(main_wnd_, this)
			);
			layout_->addWidget( ui::horz_separator() );
			layout_->addWidget(
				bones_ = new bone_properties(main_wnd_, this)
			);
			nodes_->populate();
			bones_->populate();
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

ui::abstract_properties_widget::abstract_properties_widget(ui::stick_man* mw, 
			QWidget* parent, QString title) :
		QWidget(parent),
		main_wnd_(mw),
		title_(nullptr),
		layout_(new QVBoxLayout(this)) {
	layout_->addWidget(title_ = new QLabel(title));
	hide();
}

void ui::abstract_properties_widget::set_title(QString title) {
	title_->setText(title);
}

void ui::abstract_properties_widget::init() {
	populate();
	layout_->addStretch();
}

void ui::abstract_properties_widget::populate() {

}

ui::canvas& ui::abstract_properties_widget::canvas() {
    return main_wnd_->canvases().active_canvas();
}

void ui::abstract_properties_widget::lose_selection() {}

/*------------------------------------------------------------------------------------------------*/

ui::single_or_multi_props_widget::single_or_multi_props_widget(
		ui::stick_man* mw, QWidget* parent, QString title) :
	abstract_properties_widget(mw, parent, title)
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

ui::selection_properties::selection_properties(ui::stick_man* mw) :
		props_{
			{selection_type::none, new no_properties(mw, this)},
			{selection_type::node, new node_properties(mw, this)},
			{selection_type::bone, new bone_properties(mw, this)},
			{selection_type::skeleton, new skeleton_properties(mw, this)},
			{selection_type::mixed, new mixed_properties(mw, this)}
		} {
	for (const auto& [key, prop_box] : props_) {
		addWidget(prop_box);
	}
}

ui::abstract_properties_widget* ui::selection_properties::current_props() const {
	return static_cast<abstract_properties_widget*>(currentWidget());
}

void ui::selection_properties::set(const ui::canvas& canv) {
	auto* old_props = current_props();

	setCurrentWidget(props_.at(type_of_selection(canv.selection())));

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

void ui::selection_properties::init(canvas_manager& canvases)
{
    for (const auto& [key, prop_box] : props_) {
        prop_box->init();
    }
    set(canvases.active_canvas());
    connect(&canvases, &canvas_manager::selection_changed,
        this,
        &selection_properties::handle_selection_changed
    );
}