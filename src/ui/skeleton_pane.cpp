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
#include <unordered_set>
#include <functional>
#include <variant>
#include <type_traits>
#include <qDebug>

using namespace std::placeholders;
namespace r = std::ranges;
namespace rv = std::ranges::views;

/*------------------------------------------------------------------------------------------------*/

namespace{ 

	template <typename T>
	struct item_for_model;

	template <>
	struct item_for_model<sm::node> {
		using type = ui::node_item;
	};

	template <>
	struct item_for_model<sm::bone> {
		using type = ui::bone_item;
	};

	template <>
	struct item_for_model<sm::skeleton> {
		using type = ui::skeleton_item;
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

	std::variant<sm::skeleton_ref, sm::bone_ref> get_treeitem_var(QStandardItem* qsi) {
		if (is_bone_treeitem(qsi)) {
			auto* bone = get_treeitem_data<sm::bone>(qsi);
			return { std::ref(*bone) };
		} else {
			auto* skeleton = get_treeitem_data<sm::skeleton>(qsi);
			return { std::ref(*skeleton) };
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
		auto& bi = ui::item_from_model<ui::bone_item>(bone);
		set_treeitem_data(itm, bone);
		bi.set_treeview_item(itm);
		return itm;
	}

	QStandardItem* make_treeitem(ui::canvas& canv, sm::skeleton& skel) {
		auto* itm = new QStandardItem(skel.name().c_str());

		ui::skeleton_item* canv_item = nullptr;
		canv_item = (ui::has_canvas_item(skel)) ?
			 &ui::item_from_model<ui::skeleton_item>(skel):
			 canv.insert_item(skel);
		set_treeitem_data(itm, skel);
		canv_item->set_treeview_item(itm);
		return itm;
	}

	void insert_skeleton(ui::canvas& canv, QStandardItemModel* tree, sm::skeleton_ref skel) {
		
		// traverse the graph and repopulate the treeview during the traversal 
		// by building a hash table mapping bones to their tree items.

		QStandardItem* root = tree->invisibleRootItem();
		QStandardItem* skel_item = make_treeitem(canv, skel.get());
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

		sm::visit_bones(skel.get().root_node().get(), visit);
	}

	bool is_same_bone_selection(const std::vector<ui::bone_item*>& canv_sel,
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

    std::vector<ui::abstract_canvas_item*> normalize_selection_per_active_canvas(
        const std::vector<ui::abstract_canvas_item*>& itms, const ui::canvas& active_canv) {
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
                [&active_canv](ui::abstract_canvas_item* itm) {
                    return  itm->canvas() == &active_canv;
                }
        ) | r::to< std::vector<ui::abstract_canvas_item*>>();
    }
}

void ui::skeleton_pane::expand_selected_items() {

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

void ui::skeleton_pane::sync_with_model(sm::world& model)
{
	disconnect_tree_sel_handler();

	// clear the treeview and repopulate it.
	QStandardItemModel* tree_model = static_cast<QStandardItemModel*>(skeleton_tree_->model());
	tree_model->clear();

	for (const auto& skel : model.skeletons()) {
		insert_skeleton(
            *canvas().manager().canvas_from_skeleton(skel),
            tree_model, 
            skel
        );
	}

	skeleton_tree_->clearSelection();

	//sync the new treeview selection state with the selected bones in the canvas.
	auto selected_tree_items = canvas().selection() | rv::filter(
			[](auto* aci)->bool {
				return aci->is_selected() && dynamic_cast<has_treeview_item*>(aci);
			}
		) | rv::transform(
			[](auto* aci)->QStandardItem* {
				return  dynamic_cast<has_treeview_item*>(aci)->treeview_item();
			}
		) | r::to<std::vector<QStandardItem*>>();
	select_items(selected_tree_items, true);
	expand_selected_items();

	connect_tree_sel_handler();
}

void ui::skeleton_pane::handle_tree_selection_change(
		const QItemSelection&, const QItemSelection&) {

    disconnect_tree_sel_handler();
    disconnect_canv_sel_handler();

    auto& curr_canv = canvas();
	std::vector<abstract_canvas_item*> sel_canv_items;
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
            &item_from_model<skeleton_item>(
                *get_treeitem_data<sm::skeleton>(selected_skel)
            )
        );
	} else {
		for (auto* sel : selection) {
			sm::bone* bone_ptr = get_treeitem_data<sm::bone>(sel);
			sel_canv_items.push_back(&item_from_model<bone_item>(*bone_ptr));
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

bool ui::skeleton_pane::validate_props_name_change(const std::string& new_name) {
	auto model_item = selected_single_model(canvas());
	if (!model_item) {
		return false;
	}
	return std::visit(
		[new_name](auto model_ref)->bool {
			auto& model = model_ref.get();
			using T = std::remove_cvref_t<decltype(model)>;
			auto& owner = model.owner().get();
			if constexpr (std::is_same<T, sm::skeleton>::value) {
				return !owner.contains_skeleton_name(new_name);
			} else {
				return !owner.contains<T>(new_name);
			}
		},
		*model_item
	);
}

void ui::skeleton_pane::handle_props_name_change(const std::string& new_name) {

	// change the name in the model...
	auto model_item = selected_single_model(canvas());
	if (!model_item) {
		return;
	}

	auto result = std::visit(
		[new_name](auto model_ref)->sm::result {
			auto& model = model_ref.get();
			auto& skel = model.owner().get();
			return skel.set_name(model, new_name);
		},
		*model_item
	);

	// then update the skeleton treeview if this is a bone or a skeleton...

	disconnect_tree_change_handler();
	std::visit(
		[new_name](auto model_ref) {
			auto& model = model_ref.get();
			using item_type = 
				typename item_for_model<std::remove_cvref_t<decltype(model)>>::type;
			item_type& item = item_from_model<item_type>(model);
			auto* tv_holder = dynamic_cast<has_treeview_item*>(&item);
			if (tv_holder) {
				QStandardItem* tree_item = tv_holder->treeview_item();
				tree_item->setText(new_name.c_str());
			}
		},
		*model_item
	);
	connect_tree_change_handler();
}

ui::skeleton_pane::skeleton_pane(ui::stick_man* mw) :
        canvases_(nullptr),
		QDockWidget(tr(""), mw) {

    setTitleBarWidget( custom_title_bar("skeleton view") );

	QWidget* contents = new QWidget();
	auto column = new QVBoxLayout(contents);
	column->addWidget(skeleton_tree_ = create_skeleton_tree());
	column->addWidget(sel_properties_ = new selection_properties(
            [mw]()->ui::canvas& {
                return mw->canvases().active_canvas();
            },
            this
        )
    );
    setWidget(contents);

	connect_tree_change_handler();
}

ui::selection_properties& ui::skeleton_pane::sel_properties() {
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

void ui::skeleton_pane::select_item(QStandardItem* item, bool select = true) {

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

void ui::skeleton_pane::connect_canv_cont_handler() {
    canv_content_conn_ = connect(&canvas().manager(), &ui::canvas_manager::contents_changed,
        this, &ui::skeleton_pane::sync_with_model
    );
}

void ui::skeleton_pane::disconnect_canv_cont_handler() {
    disconnect(canv_content_conn_);
}

void ui::skeleton_pane::connect_canv_sel_handler() {
	auto& canv = canvas();
	canv_sel_conn_ = connect(&canv.manager(), &ui::canvas_manager::selection_changed,
		this, &ui::skeleton_pane::handle_canv_sel_change
	);
}

void ui::skeleton_pane::disconnect_canv_sel_handler() {
	disconnect(canv_sel_conn_);
}

void ui::skeleton_pane::init(canvas_manager& canvases) {
    canvases_ = &canvases;
	sel_properties_->init(canvases);

    connect_canv_cont_handler();
	connect_canv_sel_handler();
	connect_tree_sel_handler();
}

void ui::skeleton_pane::select_items(const std::vector<QStandardItem*>& items, bool emit_signal) {
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

void ui::skeleton_pane::handle_canv_sel_change() {

	disconnect_tree_sel_handler();

	skeleton_tree_->clearSelection();
	for (abstract_canvas_item* canv_itm : canvas().selection()) {
		auto* treeitem_holder = dynamic_cast<has_treeview_item*>(canv_itm);
		if (treeitem_holder) {
			select_item(treeitem_holder->treeview_item(), true);
		}
	}

	connect_tree_sel_handler();
}

void ui::skeleton_pane::handle_tree_change(QStandardItem* item) {
	auto success = std::visit(
		[item](auto model_ref)->bool {
			auto& model = model_ref.get();
			auto old_name = model.name();
			auto& owner = model.owner().get();
			auto result = owner.set_name(model, item->text().toStdString());
			if (result != sm::result::success) {
				item->setText(old_name.c_str());
				return false;
			}
			return true;
		},
		get_treeitem_var(item)
		);

	if (success) {
		emit canvas().manager().selection_changed(canvas());
	}
}

std::vector<QStandardItem*> ui::skeleton_pane::selected_items() const {
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

ui::canvas& ui::skeleton_pane::canvas() {
	return canvases_->active_canvas();
}

void ui::skeleton_pane::connect_tree_change_handler() {
	auto* model = static_cast<QStandardItemModel*>(skeleton_tree_->model());
	tree_conn_ = connect(model, &QStandardItemModel::itemChanged, this,
		&skeleton_pane::handle_tree_change
	);
}
void ui::skeleton_pane::disconnect_tree_change_handler() {
	disconnect(tree_conn_);
}

QTreeView* ui::skeleton_pane::create_skeleton_tree() {
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
	treeView->setMaximumHeight( k_treeview_max_hgt );
	return treeView;
}

void ui::skeleton_pane::connect_tree_sel_handler() {
    tree_sel_conn_ = connect(skeleton_tree_->selectionModel(), &QItemSelectionModel::selectionChanged,
		this, &ui::skeleton_pane::handle_tree_selection_change);
}

void ui::skeleton_pane::disconnect_tree_sel_handler() {
	disconnect(tree_sel_conn_);
}