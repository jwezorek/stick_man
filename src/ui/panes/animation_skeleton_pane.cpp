#include "animation_skeleton_pane.h"
#include "tree_view.h"

namespace {

	void insert_skeleton(QStandardItemModel* tree, sm::skel_ref skel) {

		QStandardItem* root = tree->invisibleRootItem();
		QStandardItem* skel_item = new QStandardItem(skel->name().c_str());
		root->appendRow(skel_item);

	}

}

const ui::pane::tree_view& ui::pane::animation_skeleton_pane::skel_tree() const
{
    return *skel_tree_;
}

QWidget* ui::pane::animation_skeleton_pane::create_content(skeleton* parent)
{
    return skel_tree_ = new tree_view();
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
