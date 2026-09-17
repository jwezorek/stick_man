#include "ui/stick_man.hpp"
#include "ui/panes/animation_pane.hpp"
#include "ui/panes/animation_timeline.hpp"
#include "ui/canvas/canvas_manager.hpp"
#include "ui/canvas/node_item.hpp"
#include <QtWidgets>
#include <cmath>
#include <iostream>
#include <stdexcept>
void require(bool ok,const char* message) { if (!ok) throw std::runtime_error(message); }
int main(int argc,char** argv) {
    QApplication app(argc,argv);
    auto font=QFontDatabase::addApplicationFont("C:/Windows/Fonts/segoeui.ttf");
    if(font>=0) app.setFont(QFont(QFontDatabase::applicationFontFamilies(font).front(),10));
    try {
        ui::stick_man window; window.resize(1200,800); window.show(); app.processEvents();
        auto& p=window.project();
        p.add_new_skeleton_root({0,0}); auto u=(*p.topology().skeletons().begin())->root_node().id();
        p.add_new_skeleton_root({100,0}); sm::object_id v;
        for(auto s:p.topology().skeletons()) if(s->root_node().id()!=u) v=s->root_node().id();
        require(p.add_bone(u,v)==sm::result::success,"create bone");
        std::vector<sm::const_skel_ref> members{p.topology().get<sm::node>(u)->get().owner()};
        auto cid=p.make_character(members).value();
        sm::animation a; a.name="Rotate"; a.base_pose=p.core().animation_data(cid).default_pose;
        p.edit_animation_data(cid,[&](auto& data){data.animations.push_back(a);});
        auto* browser=window.findChild<ui::pane::animation*>();
        require(browser->open_animation(cid,a.id),"open animation");
        auto* editor=window.findChild<ui::pane::animation_timeline*>(); require(editor,"timeline controller");
        auto& scene=window.canvases().active_canvas();
        auto pointer=[&](QEvent::Type type,QPointF pos){
            QGraphicsSceneMouseEvent event(type); event.setScenePos(pos); event.setButton(Qt::LeftButton); event.setButtons(Qt::LeftButton);
            QApplication::sendEvent(&scene,&event);
        };
        pointer(QEvent::GraphicsSceneMousePress,{100,0}); pointer(QEvent::GraphicsSceneMouseMove,{0,100});
        require(p.core().animation_data(cid).animations[0].layers.empty(),"gesture preview is not committed");
        require(p.topology().get<sm::node>(v)->get().world_x()==100,"gesture leaves persistent rig alone");
        pointer(QEvent::GraphicsSceneMouseRelease,{0,100});
        const auto& authored=p.core().animation_data(cid).animations[0];
        require(authored.layers.size()==1 && authored.layers[0].actions.size()==1,"selection gesture creates one action");
        require(authored.layers[0].actions[0].duration==1000,"default duration in milliseconds");
        editor->seek(500);
        auto working_tip=[&](){ for(auto* node:scene.node_items()) if(node->model().id()==v) return node->model().world_pos(); return sm::point{}; };
        require(std::abs(working_tip().x-std::sqrt(5000))<1e-6,"scrubbing evaluates halfway");
        p.undo(); require(p.core().animation_data(cid).animations[0].layers.empty(),"undo removes action inside mode");
        p.redo(); require(p.core().animation_data(cid).animations[0].layers.size()==1,"redo restores action inside mode");
        auto first_id=p.core().animation_data(cid).animations[0].layers[0].actions[0].id;
        auto* angle=editor->findChild<QDoubleSpinBox*>("rotation_angle");
        auto* apply=editor->findChild<QPushButton*>("apply_rotation");
        auto* duration=editor->findChild<QSpinBox*>("rotation_duration");
        auto* pivot=editor->findChild<QComboBox*>("rotation_pivot");
        auto* timeline=editor->findChild<ui::timeline*>();
        timeline->itemSelected(QString::fromStdString(first_id.to_string()));
        angle->setValue(180); apply->click(); editor->seek(500);
        require(std::abs(working_tip().x)<1e-6 && std::abs(working_tip().y-100)<1e-6,"numeric angle changes preview");
        p.undo();
        duration->setValue(2000);apply->click();
        require(p.core().animation_data(cid).animations[0].duration()==2000,"numeric duration edits action");p.undo();
        pivot->setCurrentIndex(1);apply->click();editor->seek(1000);
        require(std::abs(working_tip().x-100)<1e-6 && std::abs(working_tip().y)<1e-6,"tip pivot remains fixed");p.undo();
        timeline->itemResizeRequested(QString::fromStdString(first_id.to_string()),250,2250);
        require(p.core().animation_data(cid).animations[0].layers[0].actions[0].start==250 && p.core().animation_data(cid).animations[0].duration()==2250,"timeline resize edits milliseconds");p.undo();
        timeline->itemMoveRequested(QString::fromStdString(first_id.to_string()),0,{ui::row_head_position::placement::between_rows,0});
        require(p.core().animation_data(cid).animations[0].layers.size()==2 && p.core().animation_data(cid).animations[0].layers[1].actions.size()==1,"move between rows inserts a layer");p.undo();
        auto* add=editor->findChild<QPushButton*>("add_rotation");
        editor->findChild<QSpinBox*>("rotation_start")->setValue(0);add->click();
        require(p.core().animation_data(cid).animations[0].layers[0].actions.size()==1,"overlap on same layer is rejected");
        auto* layer=editor->findChild<QComboBox*>("rotation_layer");
        layer->setCurrentIndex(0);layer->activated(0);add->click();
        require(p.core().animation_data(cid).animations[0].layers.size()==2,"numeric creation on a new layer");
        editor->seek(1000);require(std::abs(working_tip().x+100)<1e-6,"layers compose rotations");p.undo();
        timeline->itemSelected(QString::fromStdString(first_id.to_string()));
        editor->findChild<QPushButton*>("delete_rotation")->click();
        require(p.core().animation_data(cid).animations[0].layers[0].actions.empty(),"delete action");p.undo();
        require(p.replace_skeletons({members[0]->id()},{})!=sm::result::success,"topology remains locked");
        editor->seek(0); editor->play(); require(editor->playing(),"play starts controller clock");
        QEventLoop playback;QTimer::singleShot(80,&playback,&QEventLoop::quit);playback.exec();
        require(editor->time()>0 && working_tip().y>0,"playback clock advances pose");
        editor->pause(); require(!editor->playing(),"pause stops clock");
        editor->seek(1000);
        pointer(QEvent::GraphicsSceneMousePress,{0,100}); pointer(QEvent::GraphicsSceneMouseMove,{-100,0});
        QKeyEvent escape(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier); window.tool_mgr().keyPressEvent(scene,&escape);
        pointer(QEvent::GraphicsSceneMouseRelease,{-100,0});
        require(p.core().animation_data(cid).animations[0].layers[0].actions.size()==1,"Escape cancels provisional action");
        // A continuous head drag must keep working after the first headMoved notification.
        auto head_mouse=[&](QEvent::Type type,qint64 time,Qt::MouseButtons buttons){
            QPointF point(timeline->x_at(time),10);QMouseEvent event(type,point,point,Qt::LeftButton,buttons,Qt::NoModifier);
            QApplication::sendEvent(timeline->viewport(),&event);
        };
        head_mouse(QEvent::MouseButtonPress,100,Qt::LeftButton);
        head_mouse(QEvent::MouseMove,700,Qt::LeftButton);
        head_mouse(QEvent::MouseButtonRelease,700,Qt::NoButton);
        require(editor->time()==700,"continuous scrub follows head throughout drag");
        app.processEvents();window.grab().save("out/rotation-editor.png");
        editor->seek(1000);
        pointer(QEvent::GraphicsSceneMousePress,{0,100});pointer(QEvent::GraphicsSceneMouseMove,{-100,0});
        browser->leave_animation(); require(!p.animation_mode(),"leave session");
        require(p.core().animation_data(cid).animations[0].layers[0].actions.size()==1,"leaving cancels active gesture");
        require(p.topology().get<sm::node>(v)->get().world_x()==100,"preview never changed persistent pose");
        require(browser->open_animation(cid,a.id),"nonempty animation reopens"); browser->leave_animation();
        std::cout<<"rotation editor passed\n";
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
