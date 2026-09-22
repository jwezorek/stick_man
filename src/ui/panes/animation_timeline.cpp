#include "animation_timeline.hpp"
#include "../canvas/canvas_manager.hpp"
#include "../tools/animate_tool.hpp"
#include "../tools/select_tool_panel.hpp"
#include "../tools/tool_manager.hpp"
#include "../animation_action_editor.hpp"
#include <algorithm>

namespace {
QString text(sm::object_id id) { return QString::fromStdString(id.to_string()); }
sm::object_id id(const QString& text) { return sm::object_id::from_string(text.toStdString()).value_or(sm::object_id{}); }
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
    selection_label_ = new QLabel("No action selected", parameters_);
    auto selection_font = selection_label_->font(); selection_font.setBold(true); selection_label_->setFont(selection_font);
    parameters_layout->addWidget(selection_label_);
    auto* common = new QGridLayout; parameters_layout->addLayout(common);
    start_ = milliseconds("action_start",0,parameters_);
    duration_ = milliseconds("action_duration",1,parameters_); duration_->setValue(1000);
    easing_ = new QComboBox; easing_->setObjectName("action_easing"); easing_->addItems({"Linear","Ease in","Ease out","Ease in/out","Smoothstep"});
    layer_ = new QComboBox; layer_->setObjectName("action_layer");
    layer_->setToolTip("Layers run bottom to top. Reference-relative actions move upward when needed to follow actions that move their reference frame.");
    const QStringList common_labels{"Start","Duration","Easing","Layer"};
    QList<QWidget*> common_fields{start_,duration_,easing_,layer_};
    for(int col=0;col<common_fields.size();++col) {common->addWidget(new QLabel(common_labels[col]),0,col);common->addWidget(common_fields[col],1,col);}

    action_properties_ = new animation_editing::action_properties(parameters_);
    parameters_layout->addWidget(action_properties_);
    action_properties_->set_edit_callback([this](sm::action_data data) {
        edit_selected_action([data=std::move(data)](auto& action) mutable { action.data=std::move(data); });
    });
    auto* edits = new QHBoxLayout; layout->addLayout(edits);
    remove_ = new QPushButton("Delete action"); remove_->setObjectName("delete_rotation"); edits->addWidget(remove_);
    connect(remove_,&QPushButton::clicked,this,&animation_timeline::delete_action);
    edits->addStretch();
    status_ = new QLabel("Use the Selection/Animate tool to author rotation or translation actions. Translation path/reference options are in Tool Properties.");
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
    connect(start_,qOverload<int>(&QSpinBox::valueChanged),this,[this](int value){
        if(!updating_) edit_selected_action([&](auto& action){action.start=value;});
    });
    connect(duration_,qOverload<int>(&QSpinBox::valueChanged),this,[this](int value){
        if(!updating_) edit_selected_action([&](auto& action){action.duration=value;});
    });
    connect(easing_,qOverload<int>(&QComboBox::currentIndexChanged),this,[this](int index){
        if(!updating_ && index>=0) edit_selected_action([&](auto& action){action.easing=sm::easing(index);});
    });
    connect(layer_,qOverload<int>(&QComboBox::activated),this,[this](int index){
        if(updating_) return;
        const row_head_position row{index%2==0 ? row_head_position::placement::between_rows : row_head_position::placement::on_row,index/2};
        if(selected_action()) edit_selected_action([](auto&){},row);
        else { insertion_=row; timeline_->set_row_head(insertion_); }
    });
    timer_.setInterval(16); connect(&timer_,&QTimer::timeout,this,&animation_timeline::tick);
    connect(&project_,&mdl::project::project_changed,this,[this]{
        evaluator_.invalidate();
        if(working_) {pause();cancel_gesture();refresh();}
    });
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
sm::object_id ui::pane::animation_timeline::character_root_bone() const {
    auto c=project_.core().character(character_);
    return c ? c->get().character_root_bone() : sm::object_id{};
}
ui::tool::select_tool_panel& ui::pane::animation_timeline::animation_tool_panel() const {
    auto& animation_tool=static_cast<tool::animate&>(tools_.tool_from_id(tool::id::animate));
    return *static_cast<tool::select_tool_panel*>(animation_tool.settings_widget());
}
void ui::pane::animation_timeline::begin(sm::object_id character,sm::object_id animation,sm::topology& working) {
    evaluator_.invalidate();
    character_=character;animation_=animation;working_=&working;selected_={};time_=0;insertion_={};
    action_properties_->set_topology(&working);
    std::vector<std::pair<sm::object_id,std::string>> reference_bones;
    for(auto skeleton:working.skeletons()) for(auto bone:skeleton->bones())
        reference_bones.emplace_back(bone->id(),bone->name());
    const auto& data=project_.core().animation_data(character_);
    const auto* a=data.find_animation(animation_);
    const auto* base=a ? data.find_pose(a->base_pose) : nullptr;
    sm::point animation_root_origin{};
    double animation_root_angle=0.0;
    const auto root_bone=character_root_bone();
    if(base) if(auto frame=sm::translation_reference_frame(sm::translation_reference::animation_root,{},root_bone,*base,working)) {
        animation_root_origin=frame->origin;
        animation_root_angle=frame->angle;
    }

    auto& panel=animation_tool_panel();
    panel.set_reference_bones(reference_bones);
    panel.set_animation_mode(true);
    panel.set_animation_property_changed([this]{translation_properties_changed();});
    panel.set_capture_pins_requested([this]{capture_selected_pins();});

    auto& animation_tool=static_cast<tool::animate&>(tools_.tool_from_id(tool::id::animate));
    animation_tool.set_animation_authoring(tool::animate::animation_authoring{
        root_bone,
        animation_root_origin,
        animation_root_angle,
        [this](const authored_action& action){action_begin(action);},
        [this](const authored_action& action){action_update(action);},
        [this](const authored_action& action){action_complete(action);},
        [this]{cancel_gesture();},
        [this](QString why){cancel_gesture();message(std::move(why));}});
    tools_.set_current_tool(canvases_,tool::id::selection);
    show(); refresh();
    if(current()) timeline_->set_visible_range(0,std::max<qint64>(5000,current()->duration()));
}
void ui::pane::animation_timeline::end() {
    pause(); cancel_gesture();
    canvases_.active_canvas().clear_interactive_adornment();
    auto& animation_tool=static_cast<tool::animate&>(tools_.tool_from_id(tool::id::animate));
    animation_tool.set_animation_authoring({});
    auto& panel=animation_tool_panel();
    panel.set_animation_property_changed({});
    panel.set_capture_pins_requested({});
    panel.set_animation_mode(false);
    action_properties_->set_action(nullptr);
    action_properties_->set_topology(nullptr);
    evaluator_.invalidate();
    working_=nullptr; selected_={}; last_evaluation_.reset(); hide();
}
void ui::pane::animation_timeline::message(QString text) {status_->setText(std::move(text));}
void ui::pane::animation_timeline::reject_action(QString text) {
    message(text);
    QMessageBox::warning(this,"Cannot place action",text);
}
void ui::pane::animation_timeline::evaluate(const sm::animation& a,sm::animation_time time) {
    const auto& data=project_.core().animation_data(character_);
    last_evaluation_.reset();
    const auto* base=data.find_pose(a.base_pose); if(!base || !working_) return;
    last_evaluation_=evaluator_.evaluate(a,*base,character_root_bone(),*working_,time);
    if(!last_evaluation_->invalid_actions.empty()) message("Some actions have missing or invalid targets and are skipped.");
    else if(!last_evaluation_->unsupported_actions.empty()) message("Some action types are not previewed in this phase.");
    canvases_.active_canvas().sync_to_model();
    canvases_.active_canvas().update();
}
void ui::pane::animation_timeline::present(const sm::animation& a,sm::animation_time time,std::optional<sm::object_id> provisional) {
    const int rows=int(a.layers.size()); timeline_->set_rows(rows);
    std::vector<timeline_item> items;
    for(int layer=0;layer<rows;++layer) for(const auto& action:a.layers[layer].actions) {
        const auto presentation=animation_editing::timeline_item_for(action.data,*working_,character_root_bone());
        items.push_back({text(action.id),action.start,action.duration,rows-1-layer,presentation.label,presentation.color,
            provisional==action.id,false,presentation.invalid});
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
    evaluate(*a,time_);present(*a,time_);refresh_parameters();refresh_action_adornment();
    undo_->setEnabled(project_.can_undo());redo_->setEnabled(project_.can_redo());
}
void ui::pane::animation_timeline::sync_animation_tool_properties() {
    const auto* action=selected_action();
    animation_editing::sync_translation_tool_properties(animation_tool_panel(),action ? &action->data : nullptr);
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
    remove_->setEnabled(action);start_->setEnabled(action);
    selection_label_->setText(action && working_ ? animation_editing::selection_text_for(action->data,*working_) : "No action selected");
    if(action) {
        start_->setValue(int(std::min<qint64>(INT_MAX,action->start)));
        duration_->setValue(int(std::min<qint64>(INT_MAX,action->duration)));
        const bool easing=animation_editing::uses_easing(action->data);
        easing_->setCurrentIndex(easing?int(action->easing):int(sm::easing::linear));
        easing_->setEnabled(easing);
    } else {
        start_->setValue(int(std::min<qint64>(INT_MAX,time_)));
        easing_->setCurrentIndex(int(sm::easing::linear));
        easing_->setEnabled(true);
    }
    action_properties_->set_action(action);
    insertion_.index=std::clamp(insertion_.index,0,insertion_.kind==row_head_position::placement::between_rows?int(a->layers.size()):std::max(0,int(a->layers.size())-1));
    if(a->layers.empty()) insertion_={};
    layer_->setCurrentIndex(2*insertion_.index+(insertion_.kind==row_head_position::placement::on_row?1:0));
    timeline_->set_row_head(insertion_); updating_=false;
    sync_animation_tool_properties();
}
std::optional<sm::animation> ui::pane::animation_timeline::place(sm::animation_action action,row_head_position row,bool replace,bool explain) {
    auto* a=current();if(!a) return {};
    auto fail=[&](QString text)->std::optional<sm::animation>{if(explain)reject_action(std::move(text));return {};};
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
    const auto action_id=action.id;
    copy.layers[index].actions.push_back(std::move(action));
    try {
        return sm::place_animation_action(copy,action_id,character_root_bone(),project_.core().topology());
    } catch(const std::exception& error) {return fail(error.what());}
}
bool ui::pane::animation_timeline::commit(const sm::animation& animation) {
    try {
        project_.edit_animation_data(character_,[&](auto& data){for(auto& a:data.animations) if(a.id==animation_) {a=animation;return;}});
        return true;
    } catch(const std::exception& error) {message(error.what());return false;}
}
void ui::pane::animation_timeline::select_action(QString item) {
    cancel_gesture();pause();selected_=id(item);
    auto* a=current();
    if(a) for(int i=0;i<int(a->layers.size());++i) for(const auto& action:a->layers[i].actions)
        if(action.id==selected_) {
            insertion_={row_head_position::placement::on_row,int(a->layers.size())-1-i};
            const auto end=action.start+action.duration;
            if(time_<action.start || time_>end) time_=end;
        }
    if(a) {
        evaluate(*a,time_);
        timeline_->set_selected_item(text(selected_));
        timeline_->set_head_time(time_);
        time_label_->setText(QString("%1 / %2 ms").arg(time_).arg(a->duration()));
    }
    refresh_parameters();
    refresh_action_adornment();
}
void ui::pane::animation_timeline::edit_selected_action(
        const std::function<void(sm::animation_action&)>& edit, std::optional<row_head_position> row) {
    auto* action=selected_action(); if(!action || updating_) return;
    auto edited=*action; edit(edited);
    auto target_row=row.value_or(insertion_);
    if(auto candidate=place(edited,target_row,true,true)) {
        pause();
        if(!commit(*candidate)) refresh();
    } else refresh_parameters();
}
void ui::pane::animation_timeline::preview_selected_data(sm::action_data data) {
    auto* a=current();auto* action=selected_action();if(!a||!action)return;
    auto preview=*a;
    for(auto& layer:preview.layers) for(auto& candidate:layer.actions) if(candidate.id==selected_) candidate.data=data;
    pause();evaluate(preview,time_);present(preview,time_);
    action_properties_->preview(data);
    message(animation_editing::interactive_preview_message(data));
}
void ui::pane::animation_timeline::commit_selected_data(sm::action_data data) {
    if(const auto* action=selected_action(); action && animation_editing::editor_equivalent(action->data,data)) {
        refresh();return;
    }
    edit_selected_action([data=std::move(data)](auto& action) mutable {action.data=std::move(data);});
}
void ui::pane::animation_timeline::refresh_action_adornment() {
    auto& scene=canvases_.active_canvas();
    scene.clear_interactive_adornment();
    auto* action=selected_action();
    if(!action || !working_ || !last_evaluation_) return;
    const auto found=last_evaluation_->contexts.find(action->id);
    if(found==last_evaluation_->contexts.end())return;
    try {
        animation_editing::install_adornment(scene,*action,found->second,{
            [this](sm::action_data data){preview_selected_data(std::move(data));},
            [this](sm::action_data data){commit_selected_data(std::move(data));},
            [this]{refresh();message("Action edit cancelled.");}
        });
    } catch(...) {}
}
void ui::pane::animation_timeline::translation_properties_changed() {
    if(updating_ || !working_) return;
    auto* action=selected_action();if(!action)return;
    if(auto edited=animation_editing::apply_translation_tool_properties(action->data,animation_tool_panel().animation_translation()))
        edit_selected_action([data=std::move(*edited)](auto& candidate) mutable {candidate.data=std::move(data);});
}
void ui::pane::animation_timeline::capture_selected_pins() {
    auto* action=selected_action();if(!action||!working_)return;
    const auto pinned=canvases_.active_canvas().pinned_node_ids();
    std::vector<sm::object_id> pinned_ids(pinned.begin(),pinned.end());
    auto result=animation_editing::capture_pins(action->data,*working_,pinned_ids);
    if(!result.error.isEmpty()){message(result.error);return;}
    if(result.data) edit_selected_action([data=std::move(*result.data)](auto& candidate) mutable {candidate.data=std::move(data);});
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
    evaluate(*current(),time_);timeline_->set_head_time(time_);refresh_action_adornment();
    time_label_->setText(QString("%1 / %2 ms").arg(time_).arg(current()->duration()));
    if(!selected_action()) start_->setValue(int(std::min<qint64>(INT_MAX,time_)));
}
void ui::pane::animation_timeline::play() {
    if(!current() || current()->duration()==0) return;
    cancel_gesture();if(time_>=current()->duration()) time_=0;
    playback_start_=time_;clock_.restart();timer_.start();play_->setText("Pause");timeline_->set_follow_head(true);
    evaluate(*current(),time_);timeline_->set_head_time(time_);refresh_action_adornment();
}
void ui::pane::animation_timeline::pause() {timer_.stop();play_->setText("Play");timeline_->set_follow_head(false);}
void ui::pane::animation_timeline::tick() {
    auto* a=current();if(!a){pause();return;}
    time_=std::min(a->duration(),playback_start_+clock_.elapsed());evaluate(*a,time_);timeline_->set_head_time(time_);refresh_action_adornment();
    time_label_->setText(QString("%1 / %2 ms").arg(time_).arg(a->duration()));
    if(time_>=a->duration()) pause();
}
void ui::pane::animation_timeline::action_begin(const authored_action& authored) {
    if(!current())return;
    pause();cancel_gesture();canvases_.active_canvas().clear_interactive_adornment();
    gesture g;g.action.start=time_;g.action.duration=duration_->value();g.row=insertion_;g.action.data=authored;
    g.action.easing=animation_editing::authoring_easing(authored,sm::easing(easing_->currentIndex()));gesture_=std::move(g);
    message(animation_editing::authoring_begin_message(authored));
}
void ui::pane::animation_timeline::action_update(const authored_action& authored) {
    if(!gesture_)return;auto& g=*gesture_;g.action.data=authored;
    const auto presentation=animation_editing::authoring_update(authored);g.moved=presentation.moved;
    if(auto candidate=place(g.action,g.row,false,false)) {
        // The Selection tool itself owns the live manipulation during the gesture.
        // Do not reset/re-evaluate the detached topology here; doing so would move
        // the drag anchor out from under the next mouse-move event.
        time_=g.action.start+g.action.duration;present(*candidate,time_,g.action.id);
        message(QString("%1 over %2 ms. Release to create; Escape cancels.").arg(presentation.preview).arg(g.action.duration));
    }
}
void ui::pane::animation_timeline::action_complete(const authored_action& authored) {
    if(!gesture_)return;action_update(authored);auto g=*gesture_;gesture_.reset();
    if(g.moved)if(auto candidate=place(g.action,g.row,false,true)){
        selected_=g.action.id;time_=g.action.start+g.action.duration;
        if(commit(*candidate))return;
        selected_={};refresh();return;
    }
    refresh();
}
void ui::pane::animation_timeline::focus_action_editor(QString item) {
    select_action(std::move(item));
    action_properties_->focus_primary_editor();
}
void ui::pane::animation_timeline::cancel_gesture() {
    if(!gesture_) return;
    gesture_.reset();refresh();message("Action cancelled.");
}
