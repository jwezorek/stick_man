#pragma once

#include "abstract_skeleton_pane.h"
#include "tree_view.h"

namespace ui {

    class stick_man;

    namespace pane {

        class skeleton;

        class animation_skeleton_pane : public abstract_skeleton_pane {

            tree_view* skel_tree_;

            const tree_view& skel_tree() const override;
            QWidget* create_content(skeleton* parent) override;
            void handle_canv_sel_change() override;
            void handle_tree_change(QStandardItem* item) override;
            void handle_tree_selection_change(
                const QItemSelection&, const QItemSelection&) override;
            void sync_with_model(sm::world& model) override;
            void init_aux(canvas::manager& canvases, mdl::project& proj) override;

        public:
            animation_skeleton_pane(skeleton* parent, ui::stick_man* mgr);
        };

    }
}