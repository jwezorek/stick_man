#include "node_properties.h"
#include "../util.h"
#include "../canvas/scene.h"
#include "../canvas/node_item.h"
#include "skeleton_pane.h"
#include <ranges>

namespace r = std::ranges;
namespace rv = std::ranges::views;

namespace ui {
    namespace pane {
        namespace props {
            class node_position_tab : public ui::tabbed_values {
            private:
                current_canvas_fn get_curr_canv_;

                static sm::point origin_by_tab(int tab_index, const sm::node& selected_node) {
                    if (tab_index == 1) {
                        // skeleton relative...
                        const auto& owner = selected_node.owner();
                        return owner.root_node().world_pos();
                    }
                    else {
                        // parent relative...
                        auto parent_bone = selected_node.parent_bone();
                        return (parent_bone) ?
                            parent_bone->get().parent_node().world_pos() :
                            sm::point(0, 0);
                    }
                }

                double to_nth_tab(int n, int field, double val) override {
                    auto selected_nodes = get_curr_canv_().selected_nodes();
                    if (selected_nodes.size() != 1 || n == 0) {
                        return val;
                    }

                    auto origin = origin_by_tab(n, selected_nodes.front()->model());
                    if (field == 0) {
                        return val - origin.x;
                    }
                    else {
                        return val - origin.y;
                    }
                }

                double from_nth_tab(int n, int field, double val) override {
                    auto selected_nodes = get_curr_canv_().selected_nodes();
                    if (selected_nodes.size() != 1 || n == 0) {
                        return val;
                    }

                    auto origin = origin_by_tab(n, selected_nodes.front()->model());
                    if (field == 0) {
                        return origin.x + val;
                    }
                    else {
                        return origin.y + val;
                    }
                }

            public:
                node_position_tab(const current_canvas_fn& fn) :
                    get_curr_canv_(fn),
                    ui::tabbed_values(nullptr,
                    { "world", "skeleton", "parent" }, {
                        {"x", 0.0, -1500.0, 1500.0},
                        {"y", 0.0, -1500.0, 1500.0}
                    }, 135
                    )
                {}
            };
        }
    }
}

double ui::pane::props::nodes::world_coordinate_to_rel(int index, double val) {
    return val;
}

ui::pane::props::nodes::nodes(const current_canvas_fn& fn, selection_properties* parent) :
    single_or_multi_props_widget(
        fn,
        parent,
        "selected nodes"
    ),
    positions_{} {
}

void ui::pane::props::nodes::populate(mdl::project& proj) {
    layout_->addWidget(
        name_ = new ui::labeled_field("   name", "")
    );
    name_->set_color(QColor("yellow"));
    name_->value()->set_validator(
        [this](const std::string& new_name)->bool {
            return parent_->skel_pane().validate_props_name_change(new_name);
        }
    );

    connect(name_->value(), &ui::string_edit::value_changed,
        this, &nodes::do_property_name_change
    );

    layout_->addWidget(positions_ = new node_position_tab(get_current_canv_));

    connect(positions_, &ui::tabbed_values::value_changed,
        [this](int field) {
            auto& canv = get_current_canv_();
            auto v = positions_->value(field);
            auto sel = mdl::to_handles(ui::canvas::to_model_ptrs(canv.selected_nodes())) |
                r::to<std::vector<mdl::handle>>();
            if (field == 0) {
                proj_->transform(sel,
                    [&](sm::node& node) {
                        auto y = node.world_y();
                        node.set_world_pos(sm::point(*v, y));
                    }
                );
            }
            else {
                proj_->transform(sel,
                    [&](sm::node& node) {
                        auto x = node.world_x();
                        node.set_world_pos(sm::point(x, *v));
                    }
                );
            }
            canv.sync_to_model();
        }
    );

    connect(&proj, &mdl::project::name_changed,
        [this](mdl::skel_piece piece, const std::string& new_name) {
            handle_rename(piece, name_->value(), new_name);
        }
    );
}

void ui::pane::props::nodes::set_selection_common(const ui::canvas::scene& canv) {
    const auto& sel = canv.selection();
    auto nodes = ui::canvas::to_model_ptrs(ui::as_range_view_of_type<ui::canvas::item::node>(sel));
    auto x_pos = ui::get_unique_val(nodes |
        rv::transform([](sm::node* n) {return n->world_x(); }));
    auto y_pos = ui::get_unique_val(nodes |
        rv::transform([](sm::node* n) {return n->world_y(); }));

    positions_->set_value(0, x_pos);
    positions_->set_value(1, y_pos);
}

void ui::pane::props::nodes::set_selection_single(const ui::canvas::scene& canv) {
    name_->show();
    auto& node = canv.selected_nodes().front()->model();
    name_->set_value(node.name().c_str());
    positions_->unlock();
}

void ui::pane::props::nodes::set_selection_multi(const ui::canvas::scene& canv) {
    name_->hide();
    positions_->lock_to_primary_tab();
}

bool ui::pane::props::nodes::is_multi(const ui::canvas::scene& canv) {
    return canv.selected_nodes().size() > 1;
}

void ui::pane::props::nodes::lose_selection() {
}