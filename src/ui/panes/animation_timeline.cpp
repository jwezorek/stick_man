#include "animation_timeline.hpp"
#include "../canvas/canvas_manager.hpp"
#include "../canvas/bone_item.hpp"
#include "../canvas/node_item.hpp"
#include "../tools/selection_tool.hpp"
#include "../tools/tool_manager.hpp"
#include <numbers>
#include <cmath>
#include <algorithm>

namespace {
QString text(sm::object_id id) { return QString::fromStdString(id.to_string()); }
sm::object_id id(const QString& text) { return sm::object_id::from_string(text.toStdString()).value_or(sm::object_id{}); }
constexpr double degrees = 180.0/std::numbers::pi;
QSpinBox* milliseconds(const QString& name, int minimum, QWidget* parent) {
    auto* box = new QSpinBox(parent); box->setObjectName(name); box->setRange(minimum, INT_MAX);
    box->setSuffix(" ms"); box->setKeyboardTracking(false); return box;
}
}
ui::pane::animation_timeline::animation_timeline(mdl::project& project, canvas::manager& canvases,
        tool::manager& tools, QWidget* parent) : QDockWidget("Animation Timeline",parent),
        project_(project),canvases_(canvases),tools_(tools) {
    setObjectName("animation_timeline_pane");
    setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
    auto* content = new QWidget; auto* layout = new QVBoxLayout(content); setWidget(content);
    auto* transport = new QHBoxLayout; layout->addLayout(transport);
    auto button = [&](const QString& label, auto callback) {
        auto* b = new QPushButton(label); transport->addWidget(b); connect(b,&QPushButton::clicked,this,callback); return b;
    };
    button("Start",[this]{seek(0);});
    play_ = button("Play",[this]{ if(playing()) pause(); else play(); });
    button("Stop",[this]{seek(0);});
    button("End",[this]{if(auto* a=current()) seek(a->duration());});
    undo_ = button("Undo",[this]{cancel_gesture();pause();project_.undo();});
    redo_ = button("Redo",[this]{cancel_gesture();pause();project_.redo();});
    transport->addSpacing(12);
    time_label_ = new QLabel; transport->addWidget(time_label_); transport->addStretch();
    timeline_ = new timeline; timeline_->setObjectName("animation_timeline");
    timeline_->set_snap_interval(10); timeline_->set_snap_enabled(true); layout->addWidget(timeline_);
    parameters_ = new QWidget; auto* parameters_layout = new QVBoxLayout(parameters_); parameters_layout->setContentsMargins(0,0,0,0); layout->addWidget(parameters_);
    auto* common = new QGridLayout; parameters_layout->addLayout(common);
    start_ = milliseconds("action_start",0,parameters_);
    duration_ = milliseconds("action_duration",1,parameters_); duration_->setValue(1000);
    easing_ = new QComboBox; easing_->setObjectName("action_easing"); easing_->addItems({"Linear","Ease in","Ease out","Ease in/out","Smoothstep"});
    layer_ = new QComboBox; layer_->setObjectName("action_layer");
    const QStringList common_labels{"Start","Duration","Easing","Layer"};
    QList<QWidget*> common_fields{start_,duration_,easing_,layer_};
    for(int col=0;col<common_fields.size();++col) {common->addWidget(new QLabel(common_labels[col]),0,col);common->addWidget(common_fields[col],1,col);}

    auto* specific = new QGridLayout; parameters_layout->addLayout(specific);
    bone_ = new QComboBox; bone_->setObjectName("rotation_bone"); bone_->setMinimumContentsLength(12);
    pivot_ = new QComboBox; pivot_->setObjectName("rotation_pivot"); pivot_->addItems({"Root","Tip"});
    propagation_ = new QComboBox; propagation_->setObjectName("rotation_propagation"); propagation_->addItems({"Hierarchy","Bone only"});
    effector_ = new QComboBox; effector_->setObjectName("ik_rotation_effector"); effector_->setMinimumContentsLength(12);
    pivot_node_ = new QComboBox; pivot_node_->setObjectName("ik_rotation_pivot_node"); pivot_node_->setMinimumContentsLength(12);
    angle_ = new QDoubleSpinBox; angle_->setObjectName("rotation_angle"); angle_->setRange(-360000,360000);
    angle_->setDecimals(3); angle_->setSuffix("°"); angle_->setValue(90); angle_->setKeyboardTracking(false);
    bone_label_ = new QLabel("Bone"); pivot_label_ = new QLabel("Pivot"); propagation_label_ = new QLabel("Propagation");
    effector_label_ = new QLabel("Effector"); pivot_node_label_ = new QLabel("Pivot node"); angle_label_ = new QLabel("Angle");
    QList<QLabel*> specific_labels{bone_label_,pivot_label_,propagation_label_,effector_label_,pivot_node_label_,angle_label_};
    QList<QWidget*> specific_fields{bone_,pivot_,propagation_,effector_,pivot_node_,angle_};
    for(int col=0;col<specific_fields.size();++col) {specific->addWidget(specific_labels[col],0,col);specific->addWidget(specific_fields[col],1,col);}
    auto* edits = new QHBoxLayout; layout->addLayout(edits);
    apply_ = new QPushButton("Apply changes"); apply_->setObjectName("apply_rotation"); edits->addWidget(apply_);
    connect(apply_,&QPushButton::clicked,this,&animation_timeline::apply_changes);
    remove_ = new QPushButton("Delete action"); remove_->setObjectName("delete_rotation"); edits->addWidget(remove_);
    connect(remove_,&QPushButton::clicked,this,&animation_timeline::delete_action);
    edits->addStretch();
    status_ = new QLabel("Use the Selection/Animate tool and its existing Rigid, Unique Bone, or Ragdoll rotation settings to create actions.");
    status_->setWordWrap(true); layout->addWidget(status_);
    connect(timeline_,&timeline::headMoved,this,&animation_timeline::seek);
    connect(timeline_,&timeline::rowHeadMoved,this,[this](row_head_position row){insertion_=row;refresh_parameters();});
    connect(timeline_,&timeline::itemSelected,this,&animation_timeline::select_action);
    connect(timeline_,&timeline::itemDoubleClicked,this,&animation_timeline::focus_action_editor);
    connect(timeline_,&timeline::itemMoveRequested,this,&animation_timeline::move_action);
    connect(timeline_,&timeline::itemResizeRequested,this,&animation_timeline::resize_action);
    connect(timeline_,&timeline::itemContextMenuRequested,this,[this](QString item,QPoint point){
        select_action(item); QMenu menu; menu.addAction("Delete action",this,&animation_timeline::delete_action); menu.exec(point);
    });
    timeline_->set_drop_validator([this](const QString& item,qint64 start,qint64 end,row_head_position row){
        if(!current()) return false;
        for(const auto& l:current()->layers) for(auto a:l.actions) if(text(a.id)==item) {
            a.start=start;a.duration=end-start;return bool(place(a,row,true));
        }
        return false;
    });
    connect(layer_,qOverload<int>(&QComboBox::activated),this,[this](int index){
        if(updating_) return;
        insertion_ = {index%2==0 ? row_head_position::placement::between_rows : row_head_position::placement::on_row,index/2};
        timeline_->set_row_head(insertion_);
    });
    timer_.setInterval(16); connect(&timer_,&QTimer::timeout,this,&animation_timeline::tick);
    connect(&project_,&mdl::project::project_changed,this,[this]{ if(working_) {pause();cancel_gesture();refresh();} });
    connect(&project_,&mdl::project::refresh_undo_redo_state,this,[this](bool redo,bool undo){undo_->setEnabled(undo);redo_->setEnabled(redo);});
    auto shortcut = [&](QKeySequence key, auto callback) {
        auto* s = new QShortcut(key,this); s->setContext(Qt::WidgetWithChildrenShortcut); connect(s,&QShortcut::activated,this,callback);
    };
    shortcut(QKeySequence::Undo,[this]{cancel_gesture();pause();project_.undo();});
    shortcut(QKeySequence::Redo,[this]{cancel_gesture();pause();project_.redo();});
    shortcut(QKeySequence(Qt::Key_Delete),[this]{delete_action();});
    shortcut(QKeySequence(Qt::Key_Space),[this]{if(playing())pause();else play();});
    shortcut(QKeySequence(Qt::Key_Escape),[this]{cancel_gesture();});
    hide();
}
const sm::animation* ui::pane::animation_timeline::current() const {
    if(!working_) return nullptr;
    auto c=project_.core().character(character_); return c ? c->get().animation_data().find_animation(animation_) : nullptr;
}
const sm::animation_action* ui::pane::animation_timeline::selected_action() const {
    if(auto* a=current()) for(const auto& l:a->layers) for(const auto& action:l.actions) if(action.id==selected_) return &action;
    return nullptr;
}
void ui::pane::animation_timeline::begin(sm::object_id character,sm::object_id animation,sm::topology& working) {
    character_=character;animation_=animation;working_=&working;selected_={};time_=0;insertion_={};
    bone_->clear(); effector_->clear(); pivot_node_->clear();
    for(auto s:working.skeletons()) {
        for(auto b:s->bones()) bone_->addItem(QString::fromStdString(b->name()),text(b->id()));
        for(auto n:s->nodes()) {
            const auto name=QString::fromStdString(n->name());
            effector_->addItem(name,text(n->id())); pivot_node_->addItem(name,text(n->id()));
        }
    }
    auto& selection=static_cast<tool::select&>(tools_.tool_from_id(tool::id::selection));
    selection.set_animation_authoring(tool::select::animation_authoring{
        [this](const auto& r){rotation_begin(r);},
        [this](const auto& r){rotation_update(r);},
        [this](const auto& r){rotation_complete(r);},
        [this]{cancel_gesture();},
        [this](QString why){cancel_gesture();message(std::move(why));}});
    tools_.set_current_tool(canvases_,tool::id::selection);
    show(); refresh(); timeline_->set_visible_range(0,std::max<qint64>(5000,current()->duration()));
}
void ui::pane::animation_timeline::end() {
    pause(); cancel_gesture();
    static_cast<tool::select&>(tools_.tool_from_id(tool::id::selection)).set_animation_authoring({});
    working_=nullptr; selected_={}; hide();
}
void ui::pane::animation_timeline::message(QString text) {status_->setText(std::move(text));}
void ui::pane::animation_timeline::evaluate(const sm::animation& a,sm::animation_time time) {
    const auto& data=project_.core().animation_data(character_);
    const auto* base=data.find_pose(a.base_pose); if(!base) return;
    auto report=sm::evaluate_animation(a,*base,*working_,time);
    if(!report.invalid_actions.empty()) message("Some actions have missing or invalid targets and are skipped.");
    else if(!report.unsupported_actions.empty()) message("Some action types are not previewed in this phase.");
    canvases_.active_canvas().sync_to_model();
    canvases_.active_canvas().update();
}
void ui::pane::animation_timeline::present(const sm::animation& a,sm::animation_time time,std::optional<sm::object_id> provisional) {
    const int rows=int(a.layers.size()); timeline_->set_rows(rows);
    std::vector<timeline_item> items;
    for(int layer=0;layer<rows;++layer) for(const auto& action:a.layers[layer].actions) {
        QString label="Unsupported action"; timeline_color color=timeline_color::blue; bool supported=false, invalid=false;
        if(const auto* rotation=std::get_if<sm::rigid_rotation>(&action.data)) {
            supported=true; auto b=working_->get<sm::bone>(rotation->bone); invalid=!b;
            const auto propagation=rotation->propagation==sm::rotation_propagation::bone_only ? "bone only" : "hierarchy";
            label=QString("Rotate %1 (%2°, %3)").arg(b?QString::fromStdString(b->get().name()):"missing bone")
                .arg(rotation->angle*degrees,0,'f',1).arg(propagation);
        } else if(const auto* rotation=std::get_if<sm::ik_rotation>(&action.data)) {
            supported=true;color=timeline_color::purple;
            auto effector=working_->get<sm::node>(rotation->effector);auto pivot=working_->get<sm::node>(rotation->pivot_node);
            invalid=!effector || !pivot;
            label=QString("IK rotate %1 (%2°)").arg(effector?QString::fromStdString(effector->get().name()):"missing effector")
                .arg(rotation->angle*degrees,0,'f',1);
        }
        items.push_back({text(action.id),action.start,action.duration,rows-1-layer,label,color,
            provisional==action.id,!supported,invalid});
    }
    timeline_->set_items(std::move(items));timeline_->set_selected_item(text(selected_));timeline_->set_head_time(time);timeline_->set_row_head(insertion_);
    time_label_->setText(QString("%1 / %2 ms").arg(time).arg(a.duration()));
}
void ui::pane::animation_timeline::refresh() {
    auto* a=current(); if(!a) return;
    if(!selected_action()) selected_={};
    if(selected_action()) for(int i=0;i<int(a->layers.size());++i) for(const auto& action:a->layers[i].actions)
        if(action.id==selected_) insertion_={row_head_position::placement::on_row,int(a->layers.size())-1-i};
    time_=std::max<sm::animation_time>(0,time_);
    evaluate(*a,time_);present(*a,time_);refresh_parameters();
    undo_->setEnabled(project_.can_undo());redo_->setEnabled(project_.can_redo());
}
void ui::pane::animation_timeline::update_action_field_visibility() {
    const auto* action = selected_action();
    const bool bone_rotation = action && std::holds_alternative<sm::rigid_rotation>(action->data);
    const bool ik_rotation = action && std::holds_alternative<sm::ik_rotation>(action->data);
    bone_label_->setVisible(bone_rotation); bone_->setVisible(bone_rotation);
    pivot_label_->setVisible(bone_rotation); pivot_->setVisible(bone_rotation);
    propagation_label_->setVisible(bone_rotation); propagation_->setVisible(bone_rotation);
    effector_label_->setVisible(ik_rotation); effector_->setVisible(ik_rotation);
    pivot_node_label_->setVisible(ik_rotation); pivot_node_->setVisible(ik_rotation);
    angle_label_->setVisible(bone_rotation || ik_rotation); angle_->setVisible(bone_rotation || ik_rotation);
}
void ui::pane::animation_timeline::refresh_parameters() {
    auto* a=current(); if(!a) return;
    updating_=true;
    layer_->clear();
    for(int row=0;row<=int(a->layers.size());++row) {
        layer_->addItem(row==0?"New top layer":row==int(a->layers.size())?"New bottom layer":"New layer between");
        if(row<int(a->layers.size())) layer_->addItem(QString("Layer %1").arg(a->layers.size()-row));
    }
    auto* action=selected_action();
    const auto* rotation=action ? std::get_if<sm::rigid_rotation>(&action->data):nullptr;
    const auto* ik=action ? std::get_if<sm::ik_rotation>(&action->data):nullptr;
    apply_->setEnabled(rotation || ik);remove_->setEnabled(action);start_->setEnabled(action);
    if(action) {
        start_->setValue(int(std::min<qint64>(INT_MAX,action->start)));
        duration_->setValue(int(std::min<qint64>(INT_MAX,action->duration)));
        easing_->setCurrentIndex(int(action->easing));
    } else {
        start_->setValue(int(std::min<qint64>(INT_MAX,time_)));
        easing_->setCurrentIndex(int(sm::easing::linear));
    }
    if(rotation) {
        bone_->setCurrentIndex(bone_->findData(text(rotation->bone)));
        pivot_->setCurrentIndex(int(rotation->pivot));
        propagation_->setCurrentIndex(int(rotation->propagation));
        angle_->setValue(rotation->angle*degrees);
    } else if(ik) {
        effector_->setCurrentIndex(effector_->findData(text(ik->effector)));
        pivot_node_->setCurrentIndex(pivot_node_->findData(text(ik->pivot_node)));
        angle_->setValue(ik->angle*degrees);
    }
    insertion_.index=std::clamp(insertion_.index,0,insertion_.kind==row_head_position::placement::between_rows?int(a->layers.size()):std::max(0,int(a->layers.size())-1));
    if(a->layers.empty()) insertion_={};
    layer_->setCurrentIndex(2*insertion_.index+(insertion_.kind==row_head_position::placement::on_row?1:0));
    timeline_->set_row_head(insertion_); update_action_field_visibility(); updating_=false;
}
std::optional<sm::animation> ui::pane::animation_timeline::place(sm::animation_action action,row_head_position row,bool replace,bool explain) {
    auto* a=current();if(!a) return {};
    auto fail=[&](QString text)->std::optional<sm::animation>{if(explain)message(text);return {};};
    if(action.start<0 || action.duration<=0 || action.start>INT64_MAX-action.duration) return fail("Invalid action time.");
    sm::animation copy=*a;
    if(replace) for(auto& l:copy.layers) std::erase_if(l.actions,[&](const auto& v){return v.id==action.id;});
    int index;
    if(row.kind==row_head_position::placement::between_rows) {
        if(row.index<0 || row.index>int(copy.layers.size())) return fail("Choose a layer insertion position.");
        index=int(copy.layers.size())-row.index;copy.layers.insert(copy.layers.begin()+index,sm::animation_layer{});
    } else {
        if(row.index<0 || row.index>=int(copy.layers.size())) return fail("Choose a layer.");
        index=int(copy.layers.size())-1-row.index;
    }
    for(const auto& other:copy.layers[index].actions)
        if(action.start<other.start+other.duration && other.start<action.start+action.duration)
            return fail("Actions on the same layer cannot overlap. Choose a new layer or another time.");
    copy.layers[index].actions.push_back(std::move(action));return copy;
}
bool ui::pane::animation_timeline::commit(const sm::animation& animation) {
    try {
        project_.edit_animation_data(character_,[&](auto& data){for(auto& a:data.animations) if(a.id==animation_) {a=animation;return;}});
        return true;
    } catch(const std::exception& error) {message(error.what());return false;}
}
void ui::pane::animation_timeline::select_action(QString item) {
    cancel_gesture();pause();selected_=id(item);
    if(auto* a=current()) for(int i=0;i<int(a->layers.size());++i) for(const auto& action:a->layers[i].actions)
        if(action.id==selected_) insertion_={row_head_position::placement::on_row,int(a->layers.size())-1-i};
    refresh_parameters();
}
void ui::pane::animation_timeline::apply_changes() {
    auto* action=selected_action();if(!action) return;
    auto edited=*action;edited.start=start_->value();edited.duration=duration_->value();edited.easing=sm::easing(easing_->currentIndex());
    if(std::holds_alternative<sm::rigid_rotation>(action->data)) {
        if(bone_->currentIndex()<0) {message("Select a bone first.");return;}
        edited.data=sm::rigid_rotation{id(bone_->currentData().toString()),sm::rotation_pivot(pivot_->currentIndex()),
            angle_->value()/degrees,sm::rotation_propagation(propagation_->currentIndex())};
    } else if(std::holds_alternative<sm::ik_rotation>(action->data)) {
        if(effector_->currentIndex()<0 || pivot_node_->currentIndex()<0) {message("Select an effector and pivot node.");return;}
        edited.data=sm::ik_rotation{id(effector_->currentData().toString()),id(pivot_node_->currentData().toString()),angle_->value()/degrees};
    } else return;
    if(auto candidate=place(edited,insertion_,true,true)) {pause();commit(*candidate);}
}
void ui::pane::animation_timeline::delete_action() {
    if(!selected_action()) return;
    cancel_gesture();pause();auto copy=*current();
    for(auto& l:copy.layers) std::erase_if(l.actions,[&](const auto& a){return a.id==selected_;});
    selected_={};commit(copy);
}
void ui::pane::animation_timeline::move_action(QString item,qint64 start,row_head_position row) {
    if(!current()) return;
    for(const auto& l:current()->layers) for(auto a:l.actions) if(a.id==id(item)) {
        a.start=start;if(auto candidate=place(a,row,true,true)){pause();commit(*candidate);}return;
    }
}
void ui::pane::animation_timeline::resize_action(QString item,qint64 start,qint64 end) {
    if(!current()) return;
    for(int i=0;i<int(current()->layers.size());++i) for(auto a:current()->layers[i].actions) if(a.id==id(item)) {
        a.start=start;a.duration=end-start;
        if(auto candidate=place(a,{row_head_position::placement::on_row,int(current()->layers.size())-1-i},true,true)){pause();commit(*candidate);}return;
    }
}
void ui::pane::animation_timeline::seek(sm::animation_time time) {
    if(!current()) return;
    pause();cancel_gesture();time_=std::max<sm::animation_time>(0,time);
    evaluate(*current(),time_);timeline_->set_head_time(time_);
    time_label_->setText(QString("%1 / %2 ms").arg(time_).arg(current()->duration()));
    if(!selected_action()) start_->setValue(int(std::min<qint64>(INT_MAX,time_)));
}
void ui::pane::animation_timeline::play() {
    if(!current() || current()->duration()==0) return;
    cancel_gesture();if(time_>=current()->duration()) time_=0;
    playback_start_=time_;clock_.restart();timer_.start();play_->setText("Pause");timeline_->set_follow_head(true);
    evaluate(*current(),time_);timeline_->set_head_time(time_);
}
void ui::pane::animation_timeline::pause() {timer_.stop();play_->setText("Play");timeline_->set_follow_head(false);}
void ui::pane::animation_timeline::tick() {
    auto* a=current();if(!a){pause();return;}
    time_=std::min(a->duration(),playback_start_+clock_.elapsed());evaluate(*a,time_);timeline_->set_head_time(time_);
    time_label_->setText(QString("%1 / %2 ms").arg(time_).arg(a->duration()));
    if(time_>=a->duration()) pause();
}
void ui::pane::animation_timeline::rotation_begin(const authored_rotation& rotation) {
    if(!current()) return;
    pause(); cancel_gesture();
    gesture g; g.action.start=time_; g.action.duration=duration_->value(); g.action.easing=sm::easing(easing_->currentIndex()); g.row=insertion_;
    std::visit([&](const auto& value){g.action.data=value;},rotation);
    gesture_=std::move(g);
    message("Drag to author the rotation; release to create the action. Escape cancels.");
}
void ui::pane::animation_timeline::rotation_update(const authored_rotation& rotation) {
    if(!gesture_) return;
    auto& g=*gesture_;
    std::visit([&](const auto& value){g.action.data=value;},rotation);
    const double angle=std::visit([](const auto& value){return value.angle;},rotation);
    g.moved=std::abs(angle)>1e-8;
    if(auto candidate=place(g.action,g.row,false,true)) {
        time_=g.action.start+g.action.duration;
        present(*candidate,time_,g.action.id);
        message(QString("Rotation preview: %1° over %2 ms. Release to create; Escape cancels.").arg(angle*degrees,0,'f',1).arg(g.action.duration));
    }
}
void ui::pane::animation_timeline::rotation_complete(const authored_rotation& rotation) {
    if(!gesture_) return;
    rotation_update(rotation); auto g=*gesture_; gesture_.reset();
    const double angle=std::visit([](const auto& value){return value.angle;},rotation);
    if(g.moved && std::abs(angle)>1e-8) if(auto candidate=place(g.action,g.row,false,true)) {
        selected_=g.action.id;time_=g.action.start+g.action.duration;commit(*candidate);return;
    }
    refresh();
}
void ui::pane::animation_timeline::focus_action_editor(QString item) {
    select_action(std::move(item));
    const auto* action=selected_action();
    if(action && (std::holds_alternative<sm::rigid_rotation>(action->data) ||
       std::holds_alternative<sm::ik_rotation>(action->data))) {
        angle_->setFocus(Qt::MouseFocusReason); angle_->selectAll();
    }
}
void ui::pane::animation_timeline::cancel_gesture() {
    if(!gesture_) return;
    gesture_.reset();refresh();message("Rotation cancelled.");
}
