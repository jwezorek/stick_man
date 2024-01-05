#include "select_settings.h"

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

ui::tool::select_settings::select_settings() : QWidget() {

    QVBoxLayout* column = new QVBoxLayout(this);
    
    QButtonGroup* toplevel_behavior = new QButtonGroup(this);
    QButtonGroup* translate_mode = new QButtonGroup(this);
    QButtonGroup* rotate_mode = new QButtonGroup(this);
    
    column->addWidget(new QCheckBox("drag behaviors on"));
    
    column->addLayout(indented_widget(1, rotate_ = new QRadioButton("rotate")));
    column->addLayout(indented_widget(2, rotate_on_pin_ = new QCheckBox("rotate on pin")));
    column->addLayout(indented_widget(2, rot_rag_doll_mode_ = new QRadioButton("rag doll mode")));
    column->addLayout(indented_widget(2, rot_rubber_band_mode_ = new QRadioButton("rubber band mode")));
    column->addLayout(indented_widget(2, rot_rigid_mode_ = new QRadioButton("rigid mode")));

    column->addLayout(indented_widget(1, translate_ = new QRadioButton("translate")));
    column->addLayout(indented_widget(2, trans_selection_ = new QCheckBox("entire selection")));
    column->addLayout(indented_widget(2, trans_rag_doll_mode_ = new QRadioButton("rag doll mode")));
    column->addLayout(indented_widget(2, trans_rubber_band_mode_ = new QRadioButton("rubber band mode")));
    column->addLayout(indented_widget(2, trans_rigid_mode_ = new QRadioButton("rigid mode")));
    column->addStretch();

    toplevel_behavior->addButton( rotate_ );
    toplevel_behavior->addButton( translate_ );

    translate_mode->addButton( trans_rag_doll_mode_ );
    translate_mode->addButton( trans_rubber_band_mode_ );
    translate_mode->addButton( trans_rigid_mode_ );

    rotate_mode->addButton( rot_rag_doll_mode_ );
    rotate_mode->addButton( rot_rubber_band_mode_ );
    rotate_mode->addButton( rot_rigid_mode_ );
}









