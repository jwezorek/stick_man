#include "edit_tool_panel.hpp"

namespace {
    constexpr int drag_select_only = 0;
    constexpr int drag_translate = 1;
    constexpr int drag_rotate = 2;

    constexpr int mode_rigid = 0;
    constexpr int mode_rag_doll = 1;
    constexpr int mode_rubber_band_or_unique = 2;
}

ui::tool::edit_tool_panel::drag_behavior ui::tool::edit_tool_panel::current_drag_behavior() const {
    switch (drag_behavior_->currentData().toInt()) {
    case drag_rotate:
        return drag_behavior::rotate;
    case drag_select_only:
        return drag_behavior::select_only;
    default:
        return drag_behavior::translate;
    }
}

ui::tool::edit_drag_mode ui::tool::edit_tool_panel::selected_mode() const {
    switch (mode_->currentData().toInt()) {
    case mode_rag_doll:
        return edit_drag_mode::rag_doll;
    case mode_rubber_band_or_unique:
        return current_drag_behavior() == drag_behavior::rotate ?
            edit_drag_mode::unique : edit_drag_mode::rubber_band;
    default:
        return edit_drag_mode::rigid;
    }
}

void ui::tool::edit_tool_panel::populate_mode_combo() {
    QSignalBlocker blocker(mode_);
    mode_->clear();
    mode_->addItem("Rigid", mode_rigid);
    mode_->addItem("Rag doll", mode_rag_doll);

    const auto behavior = current_drag_behavior();
    if (behavior == drag_behavior::rotate)
        mode_->addItem("Unique bone", mode_rubber_band_or_unique);
    else if (behavior == drag_behavior::translate)
        mode_->addItem("Rubber band", mode_rubber_band_or_unique);

    const auto desired = behavior == drag_behavior::rotate ? rotate_mode_ : trans_mode_;
    int desired_data = mode_rigid;
    if (desired == edit_drag_mode::rag_doll)
        desired_data = mode_rag_doll;
    else if (desired == edit_drag_mode::rubber_band || desired == edit_drag_mode::unique)
        desired_data = mode_rubber_band_or_unique;

    const auto index = mode_->findData(desired_data);
    mode_->setCurrentIndex(index >= 0 ? index : 0);
}

void ui::tool::edit_tool_panel::update_controls() {
    const auto behavior = current_drag_behavior();
    const bool has_mode = behavior != drag_behavior::select_only;

    mode_label_->setVisible(has_mode);
    mode_->setVisible(has_mode);
    rotate_on_pin_->setVisible(behavior == drag_behavior::rotate);

    if (has_mode)
        populate_mode_combo();
}

ui::tool::edit_tool_panel::edit_tool_panel() : QWidget() {
    auto* column = new QVBoxLayout(this);

    auto* drag_row = new QHBoxLayout;
    drag_row->addWidget(new QLabel("Drag:"));
    drag_behavior_ = new QComboBox;
    drag_behavior_->addItem("Selection only", drag_select_only);
    drag_behavior_->addItem("Translate", drag_translate);
    drag_behavior_->addItem("Rotate", drag_rotate);
    drag_row->addWidget(drag_behavior_, 1);
    column->addLayout(drag_row);

    auto* mode_row = new QHBoxLayout;
    mode_label_ = new QLabel("Mode:");
    mode_row->addWidget(mode_label_);
    mode_ = new QComboBox;
    mode_row->addWidget(mode_, 1);
    column->addLayout(mode_row);

    rotate_on_pin_ = new QCheckBox("Rotate on nearest pin");
    column->addWidget(rotate_on_pin_);

    column->addSpacerItem(new QSpacerItem(15, 15));
    pin_button_ = new QPushButton("Pin selected nodes");
    column->addWidget(pin_button_);
    column->addStretch();

    connect(drag_behavior_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) {
        update_controls();
    });
    connect(mode_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) {
        const auto behavior = current_drag_behavior();
        if (behavior == drag_behavior::rotate)
            rotate_mode_ = selected_mode();
        else if (behavior == drag_behavior::translate)
            trans_mode_ = selected_mode();
    });

    init();
}

void ui::tool::edit_tool_panel::init() {
    rotate_mode_ = edit_drag_mode::rigid;
    trans_mode_ = edit_drag_mode::rag_doll;
    rotate_on_pin_->setChecked(false);
    drag_behavior_->setCurrentIndex(drag_behavior_->findData(drag_translate));
    update_controls();
}

ui::tool::edit_drag_settings ui::tool::edit_tool_panel::settings() const {
    return {
        .is_in_rotate_mode_ = current_drag_behavior() == drag_behavior::rotate,
        .rotate_on_pinned_ = rotate_on_pin_->isChecked(),
        .rotate_mode_ = rotate_mode_,
        .trans_mode_ = trans_mode_
    };
}

bool ui::tool::edit_tool_panel::has_drag_behavior() const {
    return current_drag_behavior() != drag_behavior::select_only;
}

QPushButton& ui::tool::edit_tool_panel::pin_button() const {
    return *pin_button_;
}
