#include "select_tool_panel.hpp"
#include <algorithm>

namespace {
    QWidget* indent_widget(int indent_level = 1) {
        auto* iw = new QWidget();
        iw->setFixedWidth(indent_level * 40);
        return iw;
    }

    QLayout* indented_widget(int indent_level, QWidget* widg) {
        auto* row = new QHBoxLayout();
        row->addWidget(indent_widget(indent_level));
        row->addWidget(widg);
        row->addStretch();
        return row;
    }
}

std::vector<QWidget*> ui::tool::select_tool_panel::rot_ctrls(bool include_master) {
    std::vector<QWidget*> ctrls = {
        rotate_on_pin_, rot_rag_doll_mode_,
        rot_unique_mode_, rot_rigid_mode_
    };
    if (include_master)
        ctrls.push_back(rotate_);
    return ctrls;
}

std::vector<QWidget*> ui::tool::select_tool_panel::trans_ctrls(bool include_master) {
    std::vector<QWidget*> ctrls = { 
        trans_rag_doll_mode_, trans_rubber_band_mode_, trans_rigid_mode_
    };
    if (include_master)
        ctrls.push_back(translate_);
    return ctrls;
}

ui::tool::sel_drag_mode ui::tool::select_tool_panel::rot_mode() const {
    if (rot_rag_doll_mode_->isChecked())
        return sel_drag_mode::rag_doll;
    if (rot_unique_mode_->isChecked())
        return sel_drag_mode::unique;
    return sel_drag_mode::rigid;
}

ui::tool::sel_drag_mode ui::tool::select_tool_panel::trans_mode() const {
    if (trans_rag_doll_mode_->isChecked())
        return sel_drag_mode::rag_doll;
    if (trans_rubber_band_mode_->isChecked())
        return sel_drag_mode::rubber_band;
    return sel_drag_mode::rigid;
}

ui::tool::select_tool_panel::select_tool_panel() : QWidget() {
    QVBoxLayout* column = new QVBoxLayout(this);
    toplevel_group_ = new QButtonGroup(this);
    translate_group_ = new QButtonGroup(this);
    rotate_group_ = new QButtonGroup(this);
    column->addWidget(drag_behaviors_ = new QCheckBox("drag behaviors on"));
    column->addLayout(indented_widget(1, rotate_ = new QRadioButton("rotate")));
    column->addLayout(indented_widget(2, rotate_on_pin_ = new QCheckBox("rotate on nearest pin")));
    column->addLayout(indented_widget(2, rot_rag_doll_mode_ = new QRadioButton("rag doll mode")));
    column->addLayout(indented_widget(2, rot_unique_mode_ = new QRadioButton("unique bone mode")));
    column->addLayout(indented_widget(2, rot_rigid_mode_ = new QRadioButton("rigid mode")));
    column->addLayout(indented_widget(1, translate_ = new QRadioButton("translate")));
    column->addLayout(indented_widget(2, trans_rag_doll_mode_ = new QRadioButton("rag doll mode")));
    column->addLayout(indented_widget(2, trans_rubber_band_mode_ = new QRadioButton("rubber band mode")));
    column->addLayout(indented_widget(2, trans_rigid_mode_ = new QRadioButton("rigid mode")));
    column->addSpacerItem(new QSpacerItem(15, 15));
    column->addWidget(pin_button_ = new QPushButton("pin selected nodes"));

    column->addStretch();

    toplevel_group_->addButton(rotate_);
    toplevel_group_->addButton(translate_);
    translate_group_->addButton(trans_rag_doll_mode_);
    translate_group_->addButton(trans_rubber_band_mode_);
    translate_group_->addButton(trans_rigid_mode_);
    rotate_group_->addButton(rot_rag_doll_mode_);
    rotate_group_->addButton(rot_unique_mode_);
    rotate_group_->addButton(rot_rigid_mode_);

    connect(rotate_, &QRadioButton::toggled, [this](bool checked) {
        for (auto* w : rot_ctrls(false)) {
            w->setEnabled(checked);
        }
        for (auto* w : trans_ctrls(false)) {
            w->setEnabled(!checked);
        }
    });
    connect(drag_behaviors_, &QCheckBox::toggled, [this](bool checked) {
        rotate_->setEnabled(checked);
        translate_->setEnabled(checked);
        for (auto* w : rot_ctrls(false)) {
            w->setEnabled(checked && rotate_->isChecked());
        }
        for (auto* w : trans_ctrls(false)) {
            w->setEnabled(checked && translate_->isChecked());
        }
    });
    init();
}

void ui::tool::select_tool_panel::init() {
    drag_behaviors_->setChecked(true);
    translate_->setChecked(true);
    trans_rigid_mode_->setChecked(true);
    rot_rigid_mode_->setChecked(true);
    for (auto* rot : rot_ctrls(false)) {
        rot->setEnabled(false);
    }
    for (auto* trans : trans_ctrls(false)) {
        trans->setEnabled(true);
    }
}

ui::tool::sel_drag_settings ui::tool::select_tool_panel::settings() const {
    return { .is_in_rotate_mode_ = rotate_->isChecked(),
        .rotate_on_pinned_ = rotate_on_pin_->isChecked(),
        .rotate_mode_ = rot_mode(),
        .trans_mode_ = trans_mode() };
}
bool ui::tool::select_tool_panel::has_drag_behavior() const { return drag_behaviors_->isChecked(); }
QPushButton& ui::tool::select_tool_panel::pin_button() const { return *pin_button_; }

