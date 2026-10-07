#pragma once

#include "abstract_skeleton_pane.hpp"
#include "../canvas/scene.hpp"
#include <QWidget>
#include <QtWidgets>
#include "../../core/sm_types.hpp"
#include "tree_view.hpp"
#include <functional>

/*------------------------------------------------------------------------------------------------*/

namespace ui {

    namespace canvas {
        class manager;
    }

    namespace pane {

        class skeleton;

        class main_skeleton_pane : public abstract_skeleton_pane {
            tree_view* skeleton_tree_;
            bool animation_mode_ = false;
            bool tree_enabled_before_ = true;

            void expand_selected_items();
            void handle_rename(mdl::const_skel_piece piece, const std::string& new_name);
            void traverse_tree_items(const std::function<void(QStandardItem*)>& visitor);
            std::vector<QStandardItem*> selected_items() const;
            canvas::scene& canvas();
            void select_item(QStandardItem* item, bool select);
            void select_items(const std::vector<QStandardItem*>& items, bool emit_signal = true);

            const tree_view& skel_tree() const override;
            QWidget* create_content() override;
            void handle_canv_sel_change() override;
            void handle_tree_change(QStandardItem* item) override;
            void handle_tree_selection_change( 
                const QItemSelection&, const QItemSelection&) override;
            void sync_with_model(const sm::project& model) override;
            void init_aux(canvas::manager& canvases, mdl::project& proj) override;

        public:

            explicit main_skeleton_pane(skeleton* parent);
            void set_animation_mode(bool active);
        };

    }

}