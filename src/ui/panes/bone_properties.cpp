#include "bone_properties.hpp"
#include "../canvas/scene.hpp"
#include "../canvas/canvas_item.hpp"
#include "../canvas/bone_item.hpp"
#include "../canvas/node_item.hpp"
#include "../canvas/skel_item.hpp"
#include "../panes/skeleton_pane.hpp"
#include "../panes/main_skeleton_pane.hpp"
#include "../../core/sm_visit.hpp"
#include <ranges>
#include <functional>

using namespace std::placeholders;
namespace r = std::ranges;
namespace rv = std::ranges::views;

namespace {

    std::vector<sm::bone*> topological_sort_selected_bones(ui::canvas::scene& canv) {
        auto bone_items = canv.selected_bones();
        std::unordered_set<sm::bone*> selected = ui::canvas::to_model_ptrs(bone_items) |
            r::to<std::unordered_set<sm::bone*>>();
        std::vector<sm::bone*> ordered_bones;
        for (auto skel_item : canv.skeleton_items()) {
            auto& skel = skel_item->model();
            sm::visit_bones(skel.root_node(), [&](auto& bone)->sm::visit_result {
                if (selected.contains(&bone)) ordered_bones.push_back(&bone);
                return sm::visit_result::continue_traversal;
            });
        }
        return ordered_bones;
    }

    void set_selected_bone_length(mdl::project& proj, ui::canvas::scene& canv, double new_length) {
        auto ordered = topological_sort_selected_bones(canv);
        proj.transform(
            mdl::to_handles(rv::all(ordered)) | r::to<std::vector<mdl::handle>>(),
            [new_length](sm::bone_ref bone) { bone->set_length(new_length); });
    }

    void set_selected_bone_rotation(mdl::project& proj, ui::canvas::scene& canv, double theta) {
        auto ordered = topological_sort_selected_bones(canv);
        proj.transform(
            mdl::to_handles(rv::all(ordered)) | r::to<std::vector<mdl::handle>>(),
            [theta](sm::bone_ref bone) { bone->set_world_rotation(theta); });
    }
}

namespace ui::pane::props {

std::function<void()> make_select_node_fn(const current_canvas_fn& get_current_canv, bool parent_node) {
    return [get_current_canv, parent_node]() {
        auto& canv = get_current_canv();
        auto bones = canv.selected_bones();
        if (bones.size() != 1) return;
        sm::bone& bone = bones.front()->model();
        auto& node_itm = ui::canvas::item_from_model<ui::canvas::item::node>(
            parent_node ? bone.parent_node() : bone.child_node());
        canv.set_selection(&node_itm, true);
    };
}

class rotation_tab : public ui::tabbed_values {
    current_canvas_fn get_current_canv_;

    double convert_to_or_from_parent_coords(double val, bool to_parent) {
        auto bone_selection = get_current_canv_().selected_bones();
        if (bone_selection.size() != 1) return val;
        auto& bone = bone_selection.front()->model();
        auto parent = bone.parent_bone();
        if (!parent) return val;
        auto parent_rot = ui::radians_to_degrees(parent->get().world_rotation());
        return to_parent ? val - parent_rot : val + parent_rot;
    }

public:
    explicit rotation_tab(const current_canvas_fn& get_current_canv) :
        get_current_canv_(get_current_canv),
        ui::tabbed_values(nullptr, {"world", "parent"}, {{"rotation", 0.0, -180.0, 180.0}}, 100) {}

    double to_nth_tab(int tab, int, double val) override {
        return tab == 0 ? val : convert_to_or_from_parent_coords(val, true);
    }
    double from_nth_tab(int tab, int, double val) override {
        return tab == 0 ? val : convert_to_or_from_parent_coords(val, false);
    }
};

} // namespace ui::pane::props

ui::pane::props::bones::bones(const current_canvas_fn& fn, selection_properties* parent) :
    single_or_multi_props_widget(fn, parent, "selected bones"),
    length_(nullptr), name_(nullptr), u_(nullptr), v_(nullptr), nodes_(nullptr),
    rotation_(nullptr), character_root_btn_(nullptr) {}

void ui::pane::props::bones::populate(mdl::project& proj) {
    layout_->addWidget(name_ = new ui::labeled_field("   name", ""));
    name_->set_color(QColor("yellow"));
    name_->value()->set_validator([this](const std::string& new_name) {
        return parent_->skel_pane().validate_props_name_change(new_name);
    });

    nodes_ = new QWidget();
    auto* vert_pair = new QVBoxLayout(nodes_);
    vert_pair->addWidget(new QLabel("nodes"));
    vert_pair->addWidget(u_ = new ui::labeled_hyperlink("   u", ""));
    vert_pair->addWidget(v_ = new ui::labeled_hyperlink("   v", ""));
    vert_pair->setAlignment(Qt::AlignTop);
    vert_pair->setSpacing(0);
    layout_->addWidget(nodes_);

    layout_->addWidget(length_ = new ui::labeled_numeric_val("length", 0.0, 0.0, 1500.0));
    layout_->addWidget(rotation_ = new rotation_tab(get_current_canv_));

    character_root_btn_ = new QPushButton("Set as Character Root Bone");
    layout_->addWidget(character_root_btn_);

    connect(character_root_btn_, &QPushButton::clicked, this, [this, &proj] {
        auto bones = get_current_canv_().selected_bones();
        if (bones.size() != 1) return;
        auto& bone = bones.front()->model();
        auto parent = bone.owner().parent_character();
        if (!parent) return;
        if (proj.set_character_root_bone(parent->get().id(), bone.id()) == sm::result::success)
            set_selection_single(get_current_canv_());
    });

    connect(&proj, &mdl::project::name_changed,
        [this](mdl::const_skel_piece piece, const std::string& new_name) {
            handle_rename(piece, name_->value(), new_name);
        });
    connect(name_->value(), &ui::string_edit::value_changed, this, &bones::do_property_name_change);
    connect(u_->hyperlink(), &QPushButton::clicked, make_select_node_fn(get_current_canv_, true));
    connect(v_->hyperlink(), &QPushButton::clicked, make_select_node_fn(get_current_canv_, false));
    connect(length_->num_edit(), &ui::number_edit::value_changed,
        [this, &proj](double val) { set_selected_bone_length(proj, get_current_canv_(), val); });
    connect(rotation_, &ui::tabbed_values::value_changed, [this, &proj](int) {
        if (auto rot = rotation_->value(0))
            set_selected_bone_rotation(proj, get_current_canv_(), ui::degrees_to_radians(*rot));
    });
}

bool ui::pane::props::bones::is_multi(const ui::canvas::scene& canv) {
    return canv.selected_bones().size() > 1;
}

void ui::pane::props::bones::set_selection_common(const ui::canvas::scene& canv) {
    const auto& sel = canv.selection();
    auto bones = ui::canvas::to_model_ptrs(ui::as_range_view_of_type<ui::canvas::item::bone>(sel));
    auto length = get_unique_val(bones | rv::transform([](sm::bone* b) { return b->scaled_length(); }));
    length_->num_edit()->set_value(length);
    auto world_rot = get_unique_val(bones | rv::transform([](sm::bone* b) { return b->world_rotation(); }));
    rotation_->set_value(0, world_rot.transform(ui::radians_to_degrees));
}

void ui::pane::props::bones::set_selection_multi(const ui::canvas::scene&) {
    name_->hide(); nodes_->hide(); character_root_btn_->hide(); rotation_->lock_to_primary_tab();
}

void ui::pane::props::bones::set_selection_single(const ui::canvas::scene& canv) {
    name_->show(); nodes_->show(); character_root_btn_->show(); rotation_->unlock();
    auto& bone = canv.selected_bones().front()->model();
    auto parent = bone.owner().parent_character();
    character_root_btn_->setEnabled(parent.has_value());
    character_root_btn_->setText(parent && parent->get().character_root_bone() == bone.id()
        ? "Character Root Bone (current)" : "Set as Character Root Bone");
    name_->set_value(bone.name().c_str());
    u_->hyperlink()->setText(bone.parent_node().name().c_str());
    v_->hyperlink()->setText(bone.child_node().name().c_str());
}

void ui::pane::props::bones::lose_selection() {}
