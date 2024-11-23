#pragma once
#include <QTreeView>
#include <QStandardItemModel>

namespace ui {
    namespace pane {

        class tree_view : public QTreeView {
            Q_OBJECT

        public:
            explicit tree_view(QWidget* parent = nullptr);
            ~tree_view() override = default;

        private:
            QStandardItemModel* model_;
        };

    } 
} 
