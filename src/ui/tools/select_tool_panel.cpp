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
    if (include_master) ctrls.push_back(rotate_);
    return ctrls;
}

std::vector<QWidget*> ui::tool::select_tool_panel::trans_ctrls(bool include_master) {
    std::vector<QWidget*> ctrls = { 
        trans_rag_doll_mode_, trans_rubber_band_mode_, trans_rigid_mode_
    };
    if (include_master) ctrls.push_back(translate_);
    return ctrls;
}

ui::tool::sel_drag_mode ui::tool::select_tool_panel::rot_mode() const {
    if (rot_rag_doll_mode_->isChecked()) return sel_drag_mode::rag_doll;
    if (rot_unique_mode_->isChecked()) return sel_drag_mode::unique;
    return sel_drag_mode::rigid;
}

ui::tool::sel_drag_mode ui::tool::select_tool_panel::trans_mode() const {
    if (trans_rag_doll_mode_->isChecked()) return sel_drag_mode::rag_doll;
    if (trans_rubber_band_mode_->isChecked()) return sel_drag_mode::rubber_band;
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

    animation_translation_group_=new QGroupBox("Animation Translation");
    auto* anim_grid=new QGridLayout(animation_translation_group_);
    path_=new QComboBox; path_->addItems({"Straight","Curve","Spline"});
    reference_=new QComboBox; reference_->addItems({"Animation Root","Character Root","Bone"});
    reference_bone_=new QComboBox; reference_bone_->setMinimumContentsLength(12);
    reference_bone_label_=new QLabel("Bone");
    capture_pins_=new QPushButton("Set action pins from current pins");
    anim_grid->addWidget(new QLabel("Path"),0,0);anim_grid->addWidget(path_,0,1);
    anim_grid->addWidget(new QLabel("Relative To"),1,0);anim_grid->addWidget(reference_,1,1);
    anim_grid->addWidget(reference_bone_label_,2,0);anim_grid->addWidget(reference_bone_,2,1);
    anim_grid->addWidget(capture_pins_,3,0,1,2);
    column->addWidget(animation_translation_group_);
    column->addStretch();

    toplevel_group_->addButton(rotate_);toplevel_group_->addButton(translate_);
    translate_group_->addButton(trans_rag_doll_mode_);translate_group_->addButton(trans_rubber_band_mode_);translate_group_->addButton(trans_rigid_mode_);
    rotate_group_->addButton(rot_rag_doll_mode_);rotate_group_->addButton(rot_unique_mode_);rotate_group_->addButton(rot_rigid_mode_);

    connect(rotate_, &QRadioButton::toggled,[this](bool checked) {
        for(auto* w:rot_ctrls(false))w->setEnabled(checked);
        for(auto* w:trans_ctrls(false))w->setEnabled(!checked);
    });
    connect(drag_behaviors_, &QCheckBox::toggled,[this](bool checked) {
        rotate_->setEnabled(checked);translate_->setEnabled(checked);
        for(auto* w:rot_ctrls(false))w->setEnabled(checked&&rotate_->isChecked());
        for(auto* w:trans_ctrls(false))w->setEnabled(checked&&translate_->isChecked());
    });
    auto changed=[this]{update_reference_bone_enabled();if(animation_property_changed_)animation_property_changed_();};
    connect(path_,qOverload<int>(&QComboBox::currentIndexChanged),this,[changed](int){changed();});
    connect(reference_,qOverload<int>(&QComboBox::currentIndexChanged),this,[changed](int){changed();});
    connect(reference_bone_,qOverload<int>(&QComboBox::currentIndexChanged),this,[changed](int){changed();});
    connect(capture_pins_,&QPushButton::clicked,this,[this]{if(capture_pins_requested_)capture_pins_requested_();});
    init();
}

void ui::tool::select_tool_panel::init() {
    drag_behaviors_->setChecked(true);translate_->setChecked(true);trans_rigid_mode_->setChecked(true);rot_rigid_mode_->setChecked(true);
    for(auto* rot:rot_ctrls(false))rot->setEnabled(false);
    for(auto* trans:trans_ctrls(false))trans->setEnabled(true);
    path_->setCurrentIndex(int(sm::motion_path_kind::straight));
    reference_->setCurrentIndex(int(sm::translation_reference::animation_root));
    set_animation_mode(false);
}

ui::tool::sel_drag_settings ui::tool::select_tool_panel::settings() const {
    return {.is_in_rotate_mode_=rotate_->isChecked(),.rotate_on_pinned_=rotate_on_pin_->isChecked(),.rotate_mode_=rot_mode(),.trans_mode_=trans_mode()};
}
bool ui::tool::select_tool_panel::has_drag_behavior() const { return drag_behaviors_->isChecked(); }
QPushButton& ui::tool::select_tool_panel::pin_button() const { return *pin_button_; }

ui::tool::animation_translation_settings ui::tool::select_tool_panel::animation_translation() const {
    animation_translation_settings result;
    result.path=sm::motion_path_kind(std::max(0,path_->currentIndex()));
    result.reference=sm::translation_reference(std::max(0,reference_->currentIndex()));
    if(reference_bone_->currentIndex()>=0) result.reference_bone=sm::object_id::from_string(reference_bone_->currentData().toString().toStdString()).value_or(sm::object_id{});
    return result;
}
void ui::tool::select_tool_panel::update_reference_bone_enabled() {
    const bool enabled=animation_translation_group_->isEnabled() && reference_->currentIndex()==int(sm::translation_reference::bone);
    reference_bone_->setEnabled(enabled);reference_bone_label_->setEnabled(enabled);
}
void ui::tool::select_tool_panel::set_animation_mode(bool enabled) {
    animation_translation_group_->setEnabled(enabled);capture_pins_->setEnabled(false);update_reference_bone_enabled();
}
void ui::tool::select_tool_panel::set_reference_bones(const std::vector<std::pair<sm::object_id,std::string>>& bones) {
    auto selected=animation_translation().reference_bone;QSignalBlocker block(reference_bone_);reference_bone_->clear();
    for(const auto& [id,name]:bones)reference_bone_->addItem(QString::fromStdString(name),QString::fromStdString(id.to_string()));
    int index=reference_bone_->findData(QString::fromStdString(selected.to_string()));if(index>=0)reference_bone_->setCurrentIndex(index);
    update_reference_bone_enabled();
}
void ui::tool::select_tool_panel::set_animation_translation(animation_translation_settings settings,bool ik_selected) {
    QSignalBlocker p(path_),r(reference_),b(reference_bone_);
    path_->setCurrentIndex(int(settings.path));reference_->setCurrentIndex(int(settings.reference));
    if(!settings.reference_bone.is_nil()) {
        const int i=reference_bone_->findData(QString::fromStdString(settings.reference_bone.to_string()));
        reference_bone_->setCurrentIndex(i); // -1 deliberately displays a missing reference instead of substituting another bone.
    }
    capture_pins_->setEnabled(animation_translation_group_->isEnabled()&&ik_selected);update_reference_bone_enabled();
}
void ui::tool::select_tool_panel::set_animation_property_changed(std::function<void()> callback){animation_property_changed_=std::move(callback);}
void ui::tool::select_tool_panel::set_capture_pins_requested(std::function<void()> callback){capture_pins_requested_=std::move(callback);}
