#include "animation_skeleton_pane.h"
#include "tree_view.h"

namespace {

	void insert_skeleton(QStandardItemModel* tree, sm::skel_ref skel) {

		QStandardItem* root = tree->invisibleRootItem();
		QStandardItem* skel_item = new QStandardItem(skel->name().c_str());
		root->appendRow(skel_item);

	}

}

const ui::pane::tree_view& ui::pane::animation_skeleton_pane::skel_tree() const {
    return *skel_tree_;
}

void ui::pane::animation_skeleton_pane::on_tree_context_menu(const QPoint& point)
{
    QModelIndex index = skel_tree_->indexAt(point);
    if (!index.isValid()) {
        return;  // no item at the click location
    }

    // Get the corresponding QStandardItem from the index
    QStandardItemModel* model = static_cast<QStandardItemModel*>(skel_tree_->model());
    QStandardItem* item = model->itemFromIndex(index);

    // Create the context menu
    QMenu contextMenu(skel_tree_);
    QAction* addPoseAction = contextMenu.addAction("Add new pose");
    QAction* addAnimationAction = contextMenu.addAction("Add new animation");

    // Show the context menu at the cursor position
    QAction* selectedAction = contextMenu.exec(skel_tree_->viewport()->mapToGlobal(point));

    if (selectedAction == addPoseAction) {
        handle_add_new_pose(item);
    }
    else if (selectedAction == addAnimationAction) {
        handle_add_new_animation(item);
    }
}

QWidget* ui::pane::animation_skeleton_pane::create_content(skeleton* parent)
{
    skel_tree_ = new tree_view();
	skel_tree_->setContextMenuPolicy(Qt::CustomContextMenu);
	connect(skel_tree_, &QTreeView::customContextMenuRequested, this,
		&animation_skeleton_pane::on_tree_context_menu);
	return skel_tree_;
}

void ui::pane::animation_skeleton_pane::handle_canv_sel_change()
{
}

void ui::pane::animation_skeleton_pane::handle_tree_change(QStandardItem* item)
{
}

void ui::pane::animation_skeleton_pane::handle_tree_selection_change(const QItemSelection&, const QItemSelection&)
{
}

void ui::pane::animation_skeleton_pane::sync_with_model(sm::world& model) {
	disconnect_tree_sel_handler();

	// clear the treeview and repopulate it.
	QStandardItemModel* tree_model = static_cast<QStandardItemModel*>(skel_tree_->model());
	tree_model->clear();

	for (const auto& skel : model.skeletons()) {
		insert_skeleton(tree_model, skel);
	}

	skel_tree_->clearSelection();

	// sync the new treeview selection state with the selected skeleton in the canvas.
	// TODO

	connect_tree_sel_handler();
}

void ui::pane::animation_skeleton_pane::init_aux(canvas::manager& canvases, mdl::project& proj)
{
}

ui::pane::animation_skeleton_pane::animation_skeleton_pane(skeleton* parent, ui::stick_man* mgr) :
    abstract_skeleton_pane(parent),
    skel_tree_(nullptr)
{
}

void ui::pane::animation_skeleton_pane::handle_add_new_pose(QStandardItem* skeleton_item)
{
    QString skeletonName = skeleton_item->text();
    // TODO: Implement logic for adding a new pose to the skeleton
    qDebug() << "Adding new pose to skeleton:" << skeletonName;
}

void ui::pane::animation_skeleton_pane::handle_add_new_animation(QStandardItem* skeleton_item)
{
    QString skeletonName = skeleton_item->text();
    // TODO: Implement logic for adding a new animation to the skeleton
    qDebug() << "Adding new animation to skeleton:" << skeletonName;
}
