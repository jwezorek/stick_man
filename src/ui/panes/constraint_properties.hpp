#pragma once

#include "props_box.hpp"

namespace ui::pane::props {

class constraint_properties : public props_box {
    ui::labeled_field* name_ = nullptr;
    QGroupBox* rotation_group_ = nullptr;
    QGroupBox* triangle_group_ = nullptr;

    QLabel* target_bone_ = nullptr;
    QComboBox* reference_ = nullptr;
    QLabel* reference_bone_label_ = nullptr;
    QComboBox* reference_bone_ = nullptr;
    ui::labeled_numeric_val* range_start_ = nullptr;
    ui::labeled_numeric_val* range_span_ = nullptr;

    QLabel* first_bone_ = nullptr;
    QLabel* second_bone_ = nullptr;
    ui::labeled_numeric_val* relative_angle_ = nullptr;

    bool setting_ = false;

    void populate_reference_bones(const sm::rotation_constraint& rotation);
    void update_rotation(std::function<void(sm::rotation_constraint&)> edit);
    void update_triangle(std::function<void(sm::rigid_triangle_constraint&)> edit);

public:
    constraint_properties(const current_canvas_fn& fn, selection_properties* parent);
    void populate(mdl::project& proj) override;
    void set_selection(const ui::canvas::scene& canv) override;
    void lose_selection() override;
};

} // namespace ui::pane::props
