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
    button("Select / Rotate",[this]{tools_.set_current_tool(canvases_,tool::id::selection);});
    button("Pan",[this]{tools_.set_current_tool(canvases_,tool::id::pan);});
    button("Zoom",[this]{tools_.set_current_tool(canvases_,tool::id::zoom);});
    time_label_ = new QLabel; transport->addWidget(time_label_); transport->addStretch();
    timeline_ = new timeline; timeline_->setObjectName("animation_timeline");
    timeline_->set_snap_interval(10); timeline_->set_snap_enabled(true); layout->addWidget(timeline_);
    parameters_ = new QWidget; auto* form = new QGridLayout(parameters_); form->setContentsMargins(0,0,0,0); layout->addWidget(parameters_);
    bone_ = new QComboBox; bone_->setObjectName("rotation_bone"); bone_->setMinimumContentsLength(12);
    pivot_ = new QComboBox; pivot_->setObjectName("rotation_pivot"); pivot_->addItems({"Root","Tip"});
    start_ = milliseconds("rotation_start",0,parameters_);
    duration_ = milliseconds("rotation_duration",1,parameters_); duration_->setValue(1000);
    angle_ = new QDoubleSpinBox; angle_->setObjectName("rotation_angle"); angle_->setRange(-360000,360000);
    angle_->setDecimals(3); angle_->setSuffix("°"); angle_->setValue(90); angle_->setKeyboardTracking(false);
    layer_ = new QComboBox; layer_->setObjectName("rotation_layer");
    const QStringList labels{"Bone","Pivot","Start","Duration","Angle","Layer"};
    QList<QWidget*> fields{bone_,pivot_,start_,duration_,angle_,layer_};
    for(int col=0;col<fields.size();++col) {form->addWidget(new QLabel(labels[col]),0,col);form->addWidget(fields[col],1,col);}
    auto* edits = new QHBoxLayout; layout->addLayout(edits);
    auto* fresh = new QPushButton("New rotation"); edits->addWidget(fresh);
    connect(fresh,&QPushButton::clicked,this,[this]{selected_={}; timeline_->set_selected_item({}); refresh_parameters();});
    auto* add = new QPushButton("Add rotation"); add->setObjectName("add_rotation"); edits->addWidget(add);
    connect(add,&QPushButton::clicked,this,&animation_timeline::add_rotation);
    apply_ = new QPushButton("Apply changes"); apply_->setObjectName("apply_rotation"); edits->addWidget(apply_);
    connect(apply_,&QPushButton::clicked,this,&animation_timeline::apply_changes);
    remove_ = new QPushButton("Delete action"); remove_->setObjectName("delete_rotation"); edits->addWidget(remove_);
    connect(remove_,&QPushButton::clicked,this,&animation_timeline::delete_action);
    edits->addStretch();
    status_ = new QLabel("Drag a bone or its tip with Select / Rotate to create a rotation. Timing is linear.");
    status_->setWordWrap(true); layout->addWidget(status_);
    connect(timeline_,&timeline::headMoved,this,&animation_timeline::seek);
    connect(timeline_,&timeline::rowHeadMoved,this,[this](row_head_position row){insertion_=row;refresh_parameters();});
    connect(timeline_,&timeline::itemSelected,this,&animation_timeline::select_action);
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
    bone_->clear();
    for(auto s:working.skeletons()) for(auto b:s->bones()) bone_->addItem(QString::fromStdString(b->name()),text(b->id()));
    auto& selection=static_cast<tool::select&>(tools_.tool_from_id(tool::id::selection));
    selection.set_animation_input(tool::select::animation_input{
        [this](QPointF p){gesture_press(p);},[this](QPointF p){gesture_move(p);},
        [this](QPointF p){gesture_release(p);},[this]{cancel_gesture();}});
    tools_.set_current_tool(canvases_,tool::id::selection);
    show(); refresh(); timeline_->set_visible_range(0,std::max<qint64>(5000,current()->duration()));
}
void ui::pane::animation_timeline::end() {
    pause(); cancel_gesture();
    static_cast<tool::select&>(tools_.tool_from_id(tool::id::selection)).set_animation_input({});
    working_=nullptr; selected_={}; hide();
}
void ui::pane::animation_timeline::message(QString text) {status_->setText(std::move(text));}
void ui::pane::animation_timeline::evaluate(const sm::animation& a,sm::animation_time time) {
    const auto& data=project_.core().animation_data(character_);
    const auto* base=data.find_pose(a.base_pose); if(!base) return;
    auto report=sm::evaluate_animation(a,*base,*working_,time);
    if(!report.invalid_actions.empty()) message("Some actions have missing or invalid bone targets and are skipped.");
    else if(!report.unsupported_actions.empty()) message("Only rigid rotation actions are previewed in this phase.");
    canvases_.active_canvas().sync_to_model();
    canvases_.active_canvas().update();
}
void ui::pane::animation_timeline::present(const sm::animation& a,sm::animation_time time,std::optional<sm::object_id> provisional) {
    const int rows=int(a.layers.size()); timeline_->set_rows(rows);
    std::vector<timeline_item> items;
    for(int layer=0;layer<rows;++layer) for(const auto& action:a.layers[layer].actions) {
        auto* rotation=std::get_if<sm::rigid_rotation>(&action.data);
        auto b=rotation ? working_->get<sm::bone>(rotation->bone) : std::optional<sm::bone_ref>{};
        const auto label=rotation ? QString("Rotate %1 (%2°)").arg(b?QString::fromStdString(b->get().name()):"missing bone").arg(rotation->angle*degrees,0,'f',1) : "Unsupported action";
        items.push_back({text(action.id),action.start,action.duration,rows-1-layer,label,timeline_color::blue,
            provisional==action.id,!rotation,rotation && !b});
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
void ui::pane::animation_timeline::refresh_parameters() {
    auto* a=current(); if(!a) return;
    updating_=true;
    layer_->clear();
    for(int row=0;row<=int(a->layers.size());++row) {
        layer_->addItem(row==0?"New top layer":row==int(a->layers.size())?"New bottom layer":"New layer between");
        if(row<int(a->layers.size())) layer_->addItem(QString("Layer %1").arg(a->layers.size()-row));
    }
    auto* action=selected_action(); auto* rotation=action ? std::get_if<sm::rigid_rotation>(&action->data):nullptr;
    apply_->setEnabled(rotation);remove_->setEnabled(action);
    if(rotation) {
        bone_->setCurrentIndex(bone_->findData(text(rotation->bone)));pivot_->setCurrentIndex(int(rotation->pivot));
        start_->setValue(int(std::min<qint64>(INT_MAX,action->start)));duration_->setValue(int(std::min<qint64>(INT_MAX,action->duration)));
        angle_->setValue(rotation->angle*degrees);
    } else {start_->setValue(int(std::min<qint64>(INT_MAX,time_)));}
    insertion_.index=std::clamp(insertion_.index,0,insertion_.kind==row_head_position::placement::between_rows?int(a->layers.size()):std::max(0,int(a->layers.size())-1));
    if(a->layers.empty()) insertion_={};
    layer_->setCurrentIndex(2*insertion_.index+(insertion_.kind==row_head_position::placement::on_row?1:0));
    timeline_->set_row_head(insertion_);updating_=false;
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
    auto* action=selected_action();if(!action || !std::holds_alternative<sm::rigid_rotation>(action->data)) return;
    if(bone_->currentIndex()<0) {message("Select a bone first.");return;}
    auto edited=*action;edited.start=start_->value();edited.duration=duration_->value();
    edited.data=sm::rigid_rotation{id(bone_->currentData().toString()),sm::rotation_pivot(pivot_->currentIndex()),angle_->value()/degrees};
    if(auto candidate=place(edited,insertion_,true,true)) {pause();commit(*candidate);}
}
void ui::pane::animation_timeline::add_rotation() {
    if(!current() || bone_->currentIndex()<0) {message("Select a bone first.");return;}
    cancel_gesture();pause();sm::animation_action action;action.start=start_->value();action.duration=duration_->value();
    action.data=sm::rigid_rotation{id(bone_->currentData().toString()),sm::rotation_pivot(pivot_->currentIndex()),angle_->value()/degrees};
    if(auto candidate=place(action,insertion_,false,true)) {selected_=action.id;commit(*candidate);}
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
void ui::pane::animation_timeline::gesture_press(QPointF position) {
    if(!current()) return;
    pause();cancel_gesture();
    auto& scene=canvases_.active_canvas();auto* item=scene.top_item(position);
    sm::maybe_bone_ref bone;
    if(auto* b=dynamic_cast<canvas::item::bone*>(item)) bone=b->model();
    else if(auto* n=dynamic_cast<canvas::item::node*>(item)) bone=n->model().parent_bone();
    if(!bone) {scene.clear_selection();return;}
    if(!working_->get<sm::bone>(bone->get().id())) return;
    scene.set_selection(item,true);
    auto pivot=sm::rotation_pivot(pivot_->currentIndex());
    auto origin=pivot==sm::rotation_pivot::root?bone->get().parent_node().world_pos():bone->get().child_node().world_pos();
    if(std::hypot(position.x()-origin.x,position.y()-origin.y)<1e-6) {message("Drag the bone away from its pivot.");return;}
    gesture g;g.action.start=time_;g.action.duration=duration_->value();
    g.action.data=sm::rigid_rotation{bone->get().id(),pivot,0};g.row=insertion_;g.pivot=origin;g.press=position;
    g.previous_angle=std::atan2(position.y()-origin.y,position.x()-origin.x);gesture_=g;
    message("Drag to rotate; release to create the action. Escape cancels.");
}
void ui::pane::animation_timeline::gesture_move(QPointF position) {
    if(!gesture_) return;
    auto& g=*gesture_;
    if(!g.moved && QLineF(g.press,position).length()<3) return;
    if(std::hypot(position.x()-g.pivot.x,position.y()-g.pivot.y)<1e-6) return;
    g.moved=true;
    double angle=std::atan2(position.y()-g.pivot.y,position.x()-g.pivot.x);
    g.total_angle+=std::remainder(angle-g.previous_angle,2*std::numbers::pi);g.previous_angle=angle;
    std::get<sm::rigid_rotation>(g.action.data).angle=g.total_angle;
    if(auto candidate=place(g.action,g.row,false,true)) {
        evaluate(*candidate,g.action.start+g.action.duration);present(*candidate,g.action.start+g.action.duration,g.action.id);
        message(QString("Rotation preview: %1° over %2 ms. Release to create; Escape cancels.").arg(g.total_angle*degrees,0,'f',1).arg(g.action.duration));
    }
}
void ui::pane::animation_timeline::gesture_release(QPointF position) {
    if(!gesture_) return;
    gesture_move(position);auto g=*gesture_;gesture_.reset();
    if(g.moved && std::abs(g.total_angle)>1e-8) if(auto candidate=place(g.action,g.row,false,true)) {
        selected_=g.action.id;time_=g.action.start+g.action.duration;commit(*candidate);return;
    }
    refresh();
}
void ui::pane::animation_timeline::cancel_gesture() {
    if(!gesture_) return;
    gesture_.reset();refresh();message("Rotation cancelled.");
}
