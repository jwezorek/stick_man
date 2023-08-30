#include "skeleton_pane.h"
#include "util.h"
#include "canvas_item.h"
#include "canvas.h"
#include "tools.h"
#include "tools.h"
#include "stick_man.h"
#include "../core/sm_bone.h"
#include "../core/sm_skeleton.h"
#include "../core/sm_skeleton.h"
#include <numbers>
#include <ranges>
#include <unordered_map>
#include <qDebug>

using namespace std::placeholders;
namespace r = std::ranges;
namespace rv = std::ranges::views;

/*------------------------------------------------------------------------------------------------*/

namespace{ 

	constexpr double k_tolerance = 0.00005;
	constexpr double k_default_rot_constraint_min = -std::numbers::pi / 2.0;
	constexpr double k_default_rot_constraint_span = std::numbers::pi;

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

	class no_properties : public ui::abstract_properties_widget{
	public:
		no_properties(ui::tool_manager* mgr, QWidget* parent) : abstract_properties_widget(mgr, parent, "no selection") {}
		void set_selection(const ui::canvas& canv) override {}
	};

	class mixed_properties : public ui::abstract_properties_widget {
	public:
		mixed_properties(ui::tool_manager* mgr, QWidget* parent) :
			abstract_properties_widget(mgr, parent, "mixed selection") {}
		void set_selection(const ui::canvas& canv) override {}
	};

	class node_properties : public ui::abstract_properties_widget {
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
				"skeleton"
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

	class bone_properties : public ui::abstract_properties_widget {
		ui::labeled_numeric_val* length_;
		ui::labeled_field* name_;
		QTabWidget* rotation_tab_ctrl_;
		ui::labeled_numeric_val* world_rotation_;
		ui::labeled_numeric_val* parent_rotation_;
		rot_constraint_box* constraint_box_;
		QPushButton* constraint_btn_;
		bool multi_;

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
		bone_properties(ui::tool_manager* mgr, QWidget* parent, bool multi) :
			abstract_properties_widget(
				mgr,
				parent,
				(multi) ? "selected bones" : "selected bone"
			),
			multi_(multi),
			name_(nullptr),
			length_(nullptr),
			rotation_tab_ctrl_(nullptr),
			world_rotation_(nullptr),
			parent_rotation_(nullptr),
			constraint_box_(nullptr),
			constraint_btn_(nullptr) {
		}

		void populate() override {
			if (!multi_) {
				layout_->addWidget(
					name_ = new ui::labeled_field("name", "")
				);
				name_->set_color(QColor("yellow"));
			}
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
				constraint_btn_ = new QPushButton();
				layout_->addWidget(constraint_box_ = new rot_constraint_box(constraint_btn_, canvas()));
				layout_->addWidget(constraint_btn_);

				connect(constraint_btn_, &QPushButton::clicked,
					this, &bone_properties::add_or_delete_constraint);
			}

			auto& canv = this->canvas();
			connect(length_->num_edit(), &ui::number_edit::value_changed,
				std::bind(set_bone_length, std::ref(canv), _1)
			);
		}

		void set_selection(const ui::canvas& canv) override {
			const auto& sel = canv.selection();
			auto bones = to_model_objects(ui::as_range_view_of_type<ui::bone_item>(sel));
			auto length = get_unique_val(bones |
				rv::transform([](sm::bone& b) {return b.scaled_length(); }));
			length_->num_edit()->set_value(length);
			if (!multi_) {
				auto& bone = bones.front();
				name_->set_value(bone.name().c_str());
				auto rot_constraint = bone.rotation_constraint();
				if (rot_constraint) {
					constraint_box_->show();
					constraint_box_->set(
						rot_constraint->relative_to_parent,
						rot_constraint->start_angle,
						rot_constraint->span_angle
					);
				}
				else {
					constraint_box_->hide();
				}
			}
		}

		void lose_selection() override {
			if (constraint_box_) {
				constraint_box_->hide();
			}
		}
	};

	struct constraint_dragging_info {
		QPointF axis;
		double anchor_rot;
		double start_angle;
		double span_angle;
		double radius;
	};

	/*
	class bone_tree_item : public QStandardItem {
	private:
		sm::bone* bone_;
	public:
		bone_tree_item(sm::bone& b) : bone_(&b), QStandardItem(b.name().c_str())
		{}

		sm::bone& get() const {
			return *bone_;
		}
	};
	*/

	QStandardItem* make_bone_tree_item(sm::bone& b) {
		auto* itm = new QStandardItem(b.name().c_str());
		auto& bi = ui::item_from_model<ui::bone_item>(b);
		itm->setData(QVariant::fromValue(&bi));
		return itm;
	}

	void insert_skeleton(QStandardItemModel* tree, sm::skeleton_ref skel) {
		std::unordered_map<sm::bone*,QStandardItem*> bone_to_tree_item;
		QStandardItem* root = tree->invisibleRootItem();
		QStandardItem* skel_item = new QStandardItem(skel.get().name().c_str()); 
		skel_item->setSelectable(false);
		root->appendRow(skel_item);

		auto visit = [&](sm::bone& b)->bool {
			auto parent = b.parent_bone();
			QStandardItem* bone_row = make_bone_tree_item(b);
			QStandardItem* parent_item = 
				(!parent) ? skel_item : bone_to_tree_item.at(&parent->get());
			parent_item->appendRow(bone_row);
			bone_to_tree_item[&b] = bone_row;
			return true;
		};

		sm::visit_bones(skel.get().root_node().get(), visit);
	}
}

ui::selection_properties::selection_properties(ui::tool_manager* mgr) {
	addWidget(new no_properties(mgr, this));
	addWidget(new node_properties(mgr, this, false));
	addWidget(new node_properties(mgr, this, true));
	addWidget(new bone_properties(mgr, this, false));
	addWidget(new bone_properties(mgr, this, true));
	addWidget(new mixed_properties(mgr, this));
}

ui::abstract_properties_widget* ui::selection_properties::current_props() const {
	return static_cast<abstract_properties_widget*>(currentWidget());
}

void ui::selection_properties::set(ui::sel_type typ, const ui::canvas& canv) {
	auto* old_props = current_props();
	setCurrentIndex(static_cast<int>(typ));

	old_props->lose_selection();
	current_props()->set_selection(canv);
}

void ui::selection_properties::init()
{
	for (auto* child : findChildren<abstract_properties_widget*>()) {
		child->init();
	}
	set(ui::sel_type::none, {});
}

ui::abstract_properties_widget::abstract_properties_widget(ui::tool_manager* mgr, QWidget* parent, QString title) :
		QScrollArea(parent),
		mgr_(mgr),
		canv_(nullptr),
		title_(nullptr),
		layout_(new QVBoxLayout(this)) {
		layout_->addWidget(title_ = new QLabel(title));
		hide();
	}

void ui::abstract_properties_widget::init() {
	populate();
	layout_->addStretch();
}

void ui::abstract_properties_widget::populate() {

}

ui::canvas& ui::abstract_properties_widget::canvas() {
	if (!canv_) {
		canv_ = &(mgr_->canvas());
	}
	return *canv_;
}

void ui::abstract_properties_widget::lose_selection() {}


void ui::skeleton_pane::sync_with_model()
{
	QStandardItemModel* tree_model = static_cast<QStandardItemModel*>(skeleton_tree_->model());
	tree_model->clear();

	auto& sandbox = main_wnd_->sandbox();
	for (const auto& skel : sandbox.skeletons()) {
		insert_skeleton(tree_model, skel);
	}
}

void ui::skeleton_pane::skel_tree_selection_change(const QItemSelection& selected, const QItemSelection& deselected)
{
	auto model = static_cast<QStandardItemModel*>(skeleton_tree_->model());
	auto& canv = main_wnd_->view().canvas();
	QModelIndexList selectedIdxs = selected.indexes();
	std::vector<abstract_canvas_item*> sel_canv_items;
	for (QModelIndex& idx : selectedIdxs) {
		bone_item* bi = idx.data(Qt::UserRole + 1).value<bone_item*>();
		sel_canv_items.push_back(bi);
	}
	canv.set_selection(sel_canv_items, true);
}

ui::skeleton_pane::skeleton_pane(QMainWindow* wnd) :
		main_wnd_(static_cast<stick_man*>(wnd)),
		QDockWidget(tr(""), wnd) {

    setTitleBarWidget( custom_title_bar("skeleton") );

	auto& tool_mgr = main_wnd_->tool_mgr();

	QWidget* contents = new QWidget();
	auto column = new QVBoxLayout(contents);
	column->addWidget(skeleton_tree_ = create_skeleton_tree());
	column->addWidget(sel_properties_ = new selection_properties(&tool_mgr));


    setWidget(contents);
}

ui::selection_properties& ui::skeleton_pane::sel_properties() {
	return *sel_properties_;
}

void ui::skeleton_pane::init()
{
	//TODO: refactor application startup such that this
	// can be removed (it is an artifact of when properties
	// were original a pane on the selection tool's per
	// tool properties...

	sel_properties_->init();
	auto& canv = main_wnd_->view().canvas();
	connect( &canv, &ui::canvas::contents_changed, this, &ui::skeleton_pane::sync_with_model );
}

QTreeView* ui::skeleton_pane::create_skeleton_tree() {
	QStandardItemModel* model = new QStandardItemModel();
	QTreeView* treeView = new QTreeView();

	treeView->setModel(model);
	treeView->setHeaderHidden(true);

	treeView->setStyleSheet(
		R"(
			QTreeView::branch:has-siblings:!adjoins-item {
				border-image: url(:images/vline.png) 0;
			}

			QTreeView::branch:has-siblings:adjoins-item {
				border-image: url(:images/branch-more.png) 0;
			}

			QTreeView::branch:!has-children:!has-siblings:adjoins-item {
				border-image: url(:images/branch-end.png) 0;
			}

			QTreeView::branch:has-children:!has-siblings:closed,
			QTreeView::branch:closed:has-children:has-siblings {
					border-image: none;
					image: url(:images/branch-closed.png);
			}

			QTreeView::branch:open:has-children:!has-siblings,
			QTreeView::branch:open:has-children:has-siblings  {
					border-image: none;
					image: url(:images/branch-open.png);
			}
		 )"
	);

	connect(treeView->selectionModel(), &QItemSelectionModel::selectionChanged,
		this, &ui::skeleton_pane::skel_tree_selection_change);

	return treeView;
}