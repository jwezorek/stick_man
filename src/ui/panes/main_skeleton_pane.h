#pragma once

#include "abstract_skeleton_pane.h"
#include "../canvas/scene.h"
#include <QWidget>
#include <QtWidgets>
#include "../../core/sm_types.h"
#include "properties.h"
#include "tree_view.h"
#include <functional>

/*------------------------------------------------------------------------------------------------*/

namespace ui {

    class stick_man;

    namespace canvas {
        class manager;
    }

    namespace pane {

        class skeleton;

        class main_skeleton_pane : public abstract_skeleton_pane {
            friend class skeleton;

            tree_view* skeleton_tree_;
            selection_properties* sel_properties_;
            ui::stick_man* main_wnd_;

            void expand_selected_items();
            void handle_rename(mdl::skel_piece piece, const std::string& new_name);
            void traverse_tree_items(const std::function<void(QStandardItem*)>& visitor);
            std::vector<QStandardItem*> selected_items() const;
            canvas::scene& canvas();
            void select_item(QStandardItem* item, bool select);
            void select_items(const std::vector<QStandardItem*>& items, bool emit_signal = true);

            const tree_view& skel_tree() const override;
            QWidget* create_content(skeleton* parent) override;
            void handle_canv_sel_change() override;
            void handle_tree_change(QStandardItem* item) override;
            void handle_tree_selection_change( 
                const QItemSelection&, const QItemSelection&) override;
            void sync_with_model(sm::world& model) override;
            void init_aux(canvas::manager& canvases, mdl::project& proj) override;

        public:

            main_skeleton_pane(skeleton* parent, ui::stick_man* mgr);
            selection_properties& sel_properties();
            bool validate_props_name_change(const std::string& new_name);
        };

    }

}