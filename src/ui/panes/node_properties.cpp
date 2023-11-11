#include "node_properties.h"
#include "../util.h"
#include "../canvas/canvas.h"
#include "../canvas/node_item.h"
#include "skeleton_pane.h"
#include <ranges>

namespace r = std::ranges;
namespace rv = std::ranges::views;

namespace {

}

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

double props::node_properties::world_coordinate_to_rel(int index, double val) {
    return val;
}

props::node_properties::node_properties(const current_canvas_fn& fn, ui::selection_properties* parent) :
    single_or_multi_props_widget(
        fn,
        parent,
        "selected nodes"
    ),
    positions_{} {
}

void props::node_properties::populate(mdl::project& proj) {
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
        this, &node_properties::do_property_name_change
    );

    layout_->addWidget(positions_ = new node_position_tab(get_current_canv_));

    connect(positions_, &ui::tabbed_values::value_changed,
        [this](int field) {
            auto& canv = get_current_canv_();
            auto v = positions_->value(field);
            auto sel = mdl::to_handles(ui::to_model_ptrs(canv.selected_nodes())) |
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

void props::node_properties::set_selection_common(const ui::canvas& canv) {
    const auto& sel = canv.selection();
    auto nodes = ui::to_model_ptrs(ui::as_range_view_of_type<ui::node_item>(sel));
    auto x_pos = ui::get_unique_val(nodes |
        rv::transform([](sm::node* n) {return n->world_x(); }));
    auto y_pos = ui::get_unique_val(nodes |
        rv::transform([](sm::node* n) {return n->world_y(); }));

    positions_->set_value(0, x_pos);
    positions_->set_value(1, y_pos);
}

void props::node_properties::set_selection_single(const ui::canvas& canv) {
    name_->show();
    auto& node = canv.selected_nodes().front()->model();
    name_->set_value(node.name().c_str());
    positions_->unlock();
}

void props::node_properties::set_selection_multi(const ui::canvas& canv) {
    name_->hide();
    positions_->lock_to_primary_tab();
}

bool props::node_properties::is_multi(const ui::canvas& canv) {
    return canv.selected_nodes().size() > 1;
}

void props::node_properties::lose_selection() {
}