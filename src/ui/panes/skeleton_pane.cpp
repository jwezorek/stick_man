#include "skeleton_pane.h"
#include "../util.h"
#include "../canvas/skel_item.h"
#include "../canvas/bone_item.h"
#include "../canvas/scene.h"
#include "../canvas/canvas_manager.h"
#include "../tools/tool.h"
#include "../stick_man.h"
#include "../../model/project.h"
#include "../../core/sm_bone.h"
#include "../../core/sm_skeleton.h"
#include "../../core/sm_visit.h"
#include <numbers>
#include <ranges>
#include <unordered_map>
#include <unordered_set>
#include <functional>
#include <variant>
#include <type_traits>
#include <qDebug>
#include <stack>

using namespace std::placeholders;
namespace r = std::ranges;
namespace rv = std::ranges::views;

/*------------------------------------------------------------------------------------------------*/

namespace{ 

	template <typename T>
	struct item_for_model;

	template <>
	struct item_for_model<sm::node> {
		using type = ui::canvas::item::node;
	};

	template <>
	struct item_for_model<sm::bone> {
		using type = ui::canvas::item::bone;
	};

	template <>
	struct item_for_model<sm::skeleton> {
		using type = ui::canvas::item::skeleton;
	};

	constexpr int k_treeview_max_hgt = 300;
	const int k_is_bone_role = Qt::UserRole + 1;
	const int k_model_role = Qt::UserRole + 2;

	bool is_bone_treeitem(QStandardItem* qsi) {
		return qsi->data(k_is_bone_role).value<bool>();
	}

	void set_treeitem_data(QStandardItem* qsi, sm::skeleton& skel) {
		qsi->setData(QVariant::fromValue(false), k_is_bone_role);
		qsi->setData(QVariant::fromValue(&skel), k_model_role);
	}

	void set_treeitem_data(QStandardItem* qsi, sm::bone& bone) {
		qsi->setData(QVariant::fromValue(true), k_is_bone_role);
		qsi->setData(QVariant::fromValue(&bone), k_model_role);
	}

	template<typename T>
	T* get_treeitem_data(QStandardItem* qsi) {
		return qsi->data(k_model_role).value<T*>();
	}

	mdl::skel_piece get_treeitem_var(QStandardItem* qsi) {
		if (is_bone_treeitem(qsi)) {
			auto* bone = get_treeitem_data<sm::bone>(qsi);
			return { sm::ref(*bone) };
		} else {
			auto* skeleton = get_treeitem_data<sm::skeleton>(qsi);
			return { sm::ref(*skeleton) };
		}
	}

	struct constraint_dragging_info {
		QPointF axis;
		double anchor_rot;
		double start_angle;
		double span_angle;
		double radius;
	};

	QStandardItem* make_treeitem(sm::bone& bone) {
		auto* itm = new QStandardItem(bone.name().c_str());
		auto& bi = ui::canvas::item_from_model<ui::canvas::item::bone>(bone);
		set_treeitem_data(itm, bone);
		bi.set_treeview_item(itm);
		return itm;
	}

	QStandardItem* make_treeitem(sm::skeleton& skel) {

		auto* itm = new QStandardItem(skel.name().c_str());
		ui::canvas::item::skeleton* canv_item = nullptr;
        canv_item = &ui::canvas::item_from_model<ui::canvas::item::skeleton>(skel);
		set_treeitem_data(itm, skel);
		canv_item->set_treeview_item(itm);

		return itm;
	}

	void insert_skeleton(QStandardItemModel* tree, sm::skel_ref skel) {
		
		// traverse the skeleton graph and repopulate the treeview during the traversal 
		// by building a hash table mapping bones to their tree items.

		QStandardItem* root = tree->invisibleRootItem();
		QStandardItem* skel_item = make_treeitem(skel.get());
		root->appendRow(skel_item);

		std::unordered_map<sm::bone*, QStandardItem*> bone_to_tree_item;
		auto visit = [&](sm::bone& b)->sm::visit_result {
			auto parent = b.parent_bone();
			QStandardItem* bone_row = make_treeitem(b);
			QStandardItem* parent_item = 
				(!parent) ? skel_item : bone_to_tree_item.at(&parent->get());
			parent_item->appendRow(bone_row);
			bone_to_tree_item[&b] = bone_row;
			return sm::visit_result::continue_traversal;
		};

		sm::visit_bones(skel->root_node(), visit);
	}

	bool is_same_bone_selection(const std::vector<ui::canvas::item::bone*>& canv_sel,
			const std::vector<QStandardItem*>& tree_sel) {
		if (canv_sel.size() != tree_sel.size()) {
			return false;
		}
		auto canv_set = canv_sel | rv::transform(
				[](auto* bi)->QStandardItem* {
					return bi->treeview_item();
				}
			) | r::to<std::unordered_set<QStandardItem*>>();

		auto tree_set = tree_sel | r::to<std::unordered_set<QStandardItem*>>();
		for (auto* i : canv_set) {
			if (!tree_set.contains(i)) {
				return false;
			}
		}
		for (auto* i : tree_set) {
			if (!canv_set.contains(i)) {
				return false;
			}
		}

		return true;
	}

    // if the selection is all in one canvas return as is; otherwise, remove all
    // items that are not on the active canvas.

    std::vector<ui::canvas::item::base*> normalize_selection_per_active_canvas(
        const std::vector<ui::canvas::item::base*>& itms, const ui::canvas::scene& active_canv) {
        if (itms.empty()) {
            return {};
        }
        auto* some_canvas = itms.front()->canvas();
        auto iter = r::find_if(itms,
            [some_canvas](auto* itm) { return itm->canvas() != some_canvas; }
        );
        if (iter == itms.end()) {
            return itms;
        }
        return itms |
            rv::filter(
                [&active_canv](ui::canvas::item::base* itm) {
                    return  itm->canvas() == &active_canv;
                }
        ) | r::to< std::vector<ui::canvas::item::base*>>();
    }
}

void ui::pane::skeleton::expand_selected_items() {

	QAbstractItemModel* model = skeleton_tree_->model();
	QItemSelectionModel* selectionModel = skeleton_tree_->selectionModel();

	QModelIndexList selectedIndexes = selectionModel->selectedIndexes();

	for (const QModelIndex& selectedIndex : selectedIndexes) {
		// Expand all parent items of the selected item
		QModelIndex parentIndex = selectedIndex.parent();
		while (parentIndex.isValid()) {
			skeleton_tree_->setExpanded(parentIndex, true);
			parentIndex = parentIndex.parent();
		}
	}
}

void ui::pane::skeleton::sync_with_model(sm::world& model)
{
	disconnect_tree_sel_handler();

	// clear the treeview and repopulate it.
	QStandardItemModel* tree_model = static_cast<QStandardItemModel*>(skeleton_tree_->model());
	tree_model->clear();

	for (const auto& skel : model.skeletons()) {
		insert_skeleton( tree_model, skel );
	}

	skeleton_tree_->clearSelection();

	//sync the new treeview selection state with the selected bones in the canvas.
	auto selected_tree_items = canvas().selection() | rv::filter(
			[](auto* aci)->bool {
				return aci->is_selected() && dynamic_cast<canvas::item::has_treeview_item*>(aci);
			}
		) | rv::transform(
			[](auto* aci)->QStandardItem* {
				return  dynamic_cast<canvas::item::has_treeview_item*>(aci)->treeview_item();
			}
		) | r::to<std::vector<QStandardItem*>>();
	select_items(selected_tree_items, true);
	expand_selected_items();

	connect_tree_sel_handler();
}

void ui::pane::skeleton::handle_tree_selection_change(
		const QItemSelection&, const QItemSelection&) {

    disconnect_tree_sel_handler();
    disconnect_canv_sel_handler();

    auto& curr_canv = canvas();
	std::vector<canvas::item::base*> sel_canv_items;
	auto selection = selected_items();
	QStandardItem* selected_skel = nullptr;

	for (auto* qsi : selection) {
		if (!is_bone_treeitem(qsi)) {
			selected_skel = qsi;
			break;
		}
	}

	if (selected_skel) {
        skeleton_tree_->selectionModel()->clearSelection();
		select_item(selected_skel, true);
		sel_canv_items.push_back(
            &canvas::item_from_model<canvas::item::skeleton>(
                *get_treeitem_data<sm::skeleton>(selected_skel)
            )
        );
	} else {
		for (auto* sel : selection) {
			sm::bone* bone_ptr = get_treeitem_data<sm::bone>(sel);
			sel_canv_items.push_back(&canvas::item_from_model<canvas::item::bone>(*bone_ptr));
		}
        normalize_selection_per_active_canvas(sel_canv_items, curr_canv);
	}

    if (!sel_canv_items.empty()) {
        auto& sel_canv = *sel_canv_items.front()->canvas(); 
        if (&sel_canv != &curr_canv) {
            canvases_->set_active_canvas(sel_canv);
        }
    }

    canvas().set_selection(sel_canv_items, true);
	connect_canv_sel_handler();
    connect_tree_sel_handler();
}

void ui::pane::skeleton::traverse_tree_items(const std::function<void(QStandardItem*)>& callback_visitor) {
    auto& model = *(static_cast<QStandardItemModel*>(skeleton_tree_->model()));

    auto visit_item = [&](this auto&& self, QStandardItem* itm)->void {
        callback_visitor(itm);
        if (itm->hasChildren()) {
            for (int subrow = 0; subrow < itm->rowCount(); ++subrow) {
                QStandardItem* subItem = itm->child(subrow);
                self(subItem);
            }
        }
    };

    for (int row = 0; row < model.rowCount(); ++row) {
        QStandardItem* item = model.item(row);
        visit_item(item);
    }
}

void ui::pane::skeleton::handle_rename(mdl::skel_piece piece, const std::string& new_name) {
    if (std::holds_alternative<sm::node_ref>(piece)) {
        return;
    }
    traverse_tree_items(
        [&](QStandardItem* itm)->void {
            auto itm_piece = get_treeitem_var(itm);
            if (mdl::identical_pieces(piece, itm_piece)) {
                auto curr_name = itm->text().toStdString();
                if (curr_name != new_name) {
                    itm->setText(new_name.c_str());
                }
            }
        }
    );
}

bool ui::pane::skeleton::validate_props_name_change(const std::string& new_name) {
	auto model_item = selected_single_model(canvas());
	if (!model_item) {
		return false;
	}
	return std::visit(
		[new_name](auto model_ref)->bool {
			auto& model = model_ref.get();
			using T = std::remove_cvref_t<decltype(model)>;
			auto& owner = model.owner();
			if constexpr (std::is_same<T, sm::skeleton>::value) {
				return !owner.contains_skeleton(new_name);
			} else {
				return !owner.contains<T>(new_name);
			}
		},
		*model_item
	);
}

ui::pane::skeleton::skeleton(ui::stick_man* mw) :
        canvases_(nullptr),
        project_(nullptr),
		QDockWidget(tr(""), mw) {

	setTitleBarWidget(custom_title_bar("skeleton view"));

	// Create a QTabWidget
	QTabWidget* tab_widget = new QTabWidget(this);

	// Create the skeleton pane (with QSplitter)
	QSplitter* skeleton_splitter = new QSplitter();
	skeleton_splitter->setOrientation(Qt::Vertical);

	skeleton_splitter->addWidget(skeleton_tree_ = create_skeleton_tree());
	skeleton_splitter->addWidget(sel_properties_ = new selection_properties(
		[mw]()->ui::canvas::scene& {
			return mw->canvases().active_canvas();
		},
		this
	)
	);

	QWidget* skeleton_tab = new QWidget();
	QVBoxLayout* skeleton_layout = new QVBoxLayout();
	skeleton_layout->addWidget(skeleton_splitter);
	skeleton_tab->setLayout(skeleton_layout);

	// Add panes to the tab widget
	tab_widget->addTab(skeleton_tab, tr("Skeleton"));

	QWidget* animations_tab = new QWidget();
	animations_tab->setLayout(new QVBoxLayout()); // Placeholder layout
	tab_widget->addTab(animations_tab, tr("Animations"));

	QWidget* skins_tab = new QWidget();
	skins_tab->setLayout(new QVBoxLayout()); // Placeholder layout
	tab_widget->addTab(skins_tab, tr("Skins"));

	// Set the tab widget as the main widget of the dock
	setWidget(tab_widget);

	connect_tree_change_handler();
}

ui::pane::selection_properties& ui::pane::skeleton::sel_properties() {
	return *sel_properties_;
}

void expand_item(QStandardItem* item, QTreeView* treeView) {
    if (!item || !treeView) {
        return;
    }

    QModelIndex index = item->index();
    while (index.isValid()) {
        treeView->setExpanded(index, true);
        index = index.parent();
    }

    treeView->scrollTo(item->index(), QAbstractItemView::PositionAtCenter);
}

void ui::pane::skeleton::select_item(QStandardItem* item, bool select = true) {

    expand_item(item, skeleton_tree_);

	QStandardItemModel* model = qobject_cast<QStandardItemModel*>(skeleton_tree_->model());
	if (!model) {
		return;
	}

	QModelIndex index = model->indexFromItem(item);
	if (!index.isValid()) {
		return;
	}

	QItemSelectionModel* selectionModel = skeleton_tree_->selectionModel();
	if (!selectionModel) {
		return;
	}

	QItemSelection selection(index, index);
	selectionModel->select(
		selection, 
		(select) ? QItemSelectionModel::Select : QItemSelectionModel::Deselect
	);

}

void ui::pane::skeleton::connect_canv_cont_handler() {
    canv_content_conn_ = connect(canvases_, &canvas::manager::canvas_refresh,
        this, &ui::pane::skeleton::sync_with_model
    );
}

void ui::pane::skeleton::disconnect_canv_cont_handler() {
    disconnect(canv_content_conn_);
}

void ui::pane::skeleton::connect_canv_sel_handler() {
	auto& canv = canvas();
	canv_sel_conn_ = connect(&canv.manager(), &ui::canvas::manager::selection_changed,
		this, &ui::pane::skeleton::handle_canv_sel_change
	);
}

void ui::pane::skeleton::disconnect_canv_sel_handler() {
	disconnect(canv_sel_conn_);
}

void ui::pane::skeleton::init(canvas::manager& canvases, mdl::project& proj) {
    project_ = &proj;
    canvases_ = &canvases;
	sel_properties_->init(canvases, proj);

    connect_canv_cont_handler();
	connect_canv_sel_handler();
	connect_tree_sel_handler();

    connect(project_, &mdl::project::name_changed, this, &skeleton::handle_rename);
}

void ui::pane::skeleton::select_items(const std::vector<QStandardItem*>& items, bool emit_signal) {
	if (!emit_signal) {
		disconnect_tree_sel_handler();
	}
	skeleton_tree_->selectionModel()->clearSelection();
	for (auto* itm : items) {
		select_item(itm);
	}
	if (!emit_signal) {
		connect_tree_sel_handler();
	}
}

void ui::pane::skeleton::handle_canv_sel_change() {

	disconnect_tree_sel_handler();

	skeleton_tree_->clearSelection();
	for (canvas::item::base* canv_itm : canvas().selection()) {
		auto* treeitem_holder = dynamic_cast<canvas::item::has_treeview_item*>(canv_itm);
		if (treeitem_holder) {
			select_item(treeitem_holder->treeview_item(), true);
		}
	}

	connect_tree_sel_handler();
}

void ui::pane::skeleton::handle_tree_change(QStandardItem* item) {
    auto piece = get_treeitem_var(item);
    auto old_name = std::visit([](auto p) {return p->name(); }, piece);
    auto result = project_->rename(piece, item->text().toStdString());
    if (!result) {
        item->setText(old_name.c_str());
    }
}

std::vector<QStandardItem*> ui::pane::skeleton::selected_items() const {
	std::vector<QStandardItem*> selection;
	QItemSelectionModel* sel_model = skeleton_tree_->selectionModel();
	QModelIndexList selectedIndexes = sel_model->selectedIndexes();
	auto* model = static_cast<QStandardItemModel*>(skeleton_tree_->model());

	for (const QModelIndex& index : selectedIndexes) {
		QStandardItem* item = model->itemFromIndex(index);
		if (item) {
			selection.push_back(item);
		}
	}

	return selection;
}

ui::canvas::scene& ui::pane::skeleton::canvas() {
	return canvases_->active_canvas();
}

void ui::pane::skeleton::connect_tree_change_handler() {
	auto* model = static_cast<QStandardItemModel*>(skeleton_tree_->model());
	tree_conn_ = connect(model, &QStandardItemModel::itemChanged, this,
		&skeleton::handle_tree_change
	);
}
void ui::pane::skeleton::disconnect_tree_change_handler() {
	disconnect(tree_conn_);
}

QTreeView* ui::pane::skeleton::create_skeleton_tree() {
	QStandardItemModel* model = new QStandardItemModel();
	QTreeView* treeView = new QTreeView();
	treeView->setSelectionMode(QAbstractItemView::ExtendedSelection);
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
	return treeView;
}

void ui::pane::skeleton::connect_tree_sel_handler() {
    tree_sel_conn_ = connect(skeleton_tree_->selectionModel(), &QItemSelectionModel::selectionChanged,
		this, &ui::pane::skeleton::handle_tree_selection_change);
}

void ui::pane::skeleton::disconnect_tree_sel_handler() {
	disconnect(tree_sel_conn_);
}