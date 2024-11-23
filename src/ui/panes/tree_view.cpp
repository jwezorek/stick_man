#include "tree_view.h"

ui::pane::tree_view::tree_view(QWidget* parent)
    : QTreeView(parent), model_(new QStandardItemModel(this)) {
    setSelectionMode(QAbstractItemView::ExtendedSelection);
    setModel(model_);
    setHeaderHidden(true);

    setStyleSheet(
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
            QTreeView::branch:open:has-children:has-siblings {
                    border-image: none;
                    image: url(:images/branch-open.png);
            }
        )"
    );
}