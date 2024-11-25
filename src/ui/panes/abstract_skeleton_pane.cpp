#include "abstract_skeleton_pane.h"
#include "../canvas/canvas_manager.h"
#include "skeleton_pane.h"

void ui::pane::abstract_skeleton_pane::connect_tree_sel_handler() {
    tree_sel_conn_ = connect(
        skel_tree().selectionModel(), &QItemSelectionModel::selectionChanged,
        this, &abstract_skeleton_pane::handle_tree_selection_change
    );
}

void ui::pane::abstract_skeleton_pane::disconnect_tree_sel_handler() {
    disconnect(tree_sel_conn_);
}

void ui::pane::abstract_skeleton_pane::connect_canv_sel_handler() {
    auto& canv = canvases_->active_canvas();
    canv_sel_conn_ = connect(&canv.manager(), &canvas::manager::selection_changed,
        this, &abstract_skeleton_pane::handle_canv_sel_change
    );
}

void ui::pane::abstract_skeleton_pane::disconnect_canv_sel_handler() {
    disconnect(canv_sel_conn_);
}

void ui::pane::abstract_skeleton_pane::connect_canv_cont_handler() {
    canv_content_conn_ = connect(canvases_, &canvas::manager::canvas_refresh,
        this, &abstract_skeleton_pane::sync_with_model
    );
}

void ui::pane::abstract_skeleton_pane::disconnect_canv_cont_handler() {
    disconnect(canv_content_conn_);
}

void ui::pane::abstract_skeleton_pane::connect_tree_change_handler() {
    auto* model = static_cast<QStandardItemModel*>(skel_tree().model());
    tree_conn_ = connect(model, &QStandardItemModel::itemChanged, this,
        &abstract_skeleton_pane::handle_tree_change
    );
}

void ui::pane::abstract_skeleton_pane::disconnect_tree_change_handler() {
    disconnect(tree_conn_);
}

ui::pane::tree_view& ui::pane::abstract_skeleton_pane::skel_tree() {
    return const_cast<tree_view&>(
        static_cast<const abstract_skeleton_pane&>(*this).skel_tree()
    );
}

void ui::pane::abstract_skeleton_pane::populate() {
    layout_->addWidget(create_content(parent_));
}

ui::pane::abstract_skeleton_pane::abstract_skeleton_pane(skeleton* parent) :
    parent_(parent),
    canvases_(nullptr),
    project_(nullptr),
    layout_(new QStackedLayout(this)) {
}

void ui::pane::abstract_skeleton_pane::init(canvas::manager& canvases, mdl::project& proj) {
    populate();

    project_ = &proj;
    canvases_ = &canvases;

    connect_canv_cont_handler();
    connect_canv_sel_handler();
    connect_tree_sel_handler();
    connect_tree_change_handler();

    init_aux(canvases, proj);
}
