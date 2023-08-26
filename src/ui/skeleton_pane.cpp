#include "skeleton_pane.h"
#include "util.h"

/*------------------------------------------------------------------------------------------------*/

namespace{
	QTreeView* create_skeleton_tree() {
		QStandardItemModel* model = new QStandardItemModel();
		QStandardItem* rootItem = model->invisibleRootItem();

		// Populate the model with some items
		QStandardItem* item1 = new QStandardItem("Item 1");
		QStandardItem* item2 = new QStandardItem("Item 2");
		QStandardItem* subItem1 = new QStandardItem("Subitem 1");
		QStandardItem* subItem2 = new QStandardItem("Subitem 2");
		QStandardItem* subItem3 = new QStandardItem("Subitem 3");
		QStandardItem* subItem4 = new QStandardItem("Subitem 4");

		rootItem->appendRow(item1);
		rootItem->appendRow(item2);
		item1->appendRow(subItem1);
		item1->appendRow(subItem2);

		item2->appendRow(subItem3);
		subItem3->appendRow(subItem4);

		// Create the tree view
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

		return treeView;
	 }
}

ui::skeleton_pane::skeleton_pane(QMainWindow* wnd) :
    QDockWidget(tr(""), wnd) {

    setTitleBarWidget( custom_title_bar("skeleton") );

    auto placeholder = create_skeleton_tree();
    placeholder->setMinimumWidth(200);
    setWidget(placeholder);
}