#include "animation_timeline.hpp"
#include "../canvas/canvas_manager.hpp"
#include "../canvas/bone_item.hpp"
#include "../canvas/node_item.hpp"
#include "../tools/selection_tool.hpp"
#include "../tools/tool_manager.hpp"
#include "../util.hpp"
#include <numbers>
#include <cmath>
#include <algorithm>
#include <memory>
#include <type_traits>

namespace {
QString text(sm::object_id id) { return QString::fromStdString(id.to_string()); }
sm::object_id id(const QString& text) { return sm::object_id::from_string(text.toStdString()).value_or(sm::object_id{}); }
constexpr double degrees = 180.0/std::numbers::pi;
QSpinBox* milliseconds(const QString& name, int minimum, QWidget* parent) {
    auto* box = new QSpinBox(parent); box->setObjectName(name); box->setRange(minimum, INT_MAX);
    box->setSuffix(" ms"); box->setKeyboardTracking(false); return box;
}

sm::animation actions_before(const sm::animation& animation, const sm::animation_action& selected) {
    sm::animation before = animation;
    before.layers.clear();
    for (const auto& layer : animation.layers) {
        auto found = std::ranges::find_if(layer.actions, [&](const auto& action) { return action.id == selected.id; });
        if (found == layer.actions.end()) {
            before.layers.push_back(layer);
            continue;
        }
        sm::animation_layer prefix;
        for (const auto& action : layer.actions)
            if (action.id != selected.id && action.start < selected.start) prefix.actions.push_back(action);
        before.layers.push_back(std::move(prefix));
        break;
    }
    return before;
}

class rotation_action_adornment final : public ui::canvas::interactive_adornment {
    ui::canvas::scene& scene_;
    QPointF pivot_;
    double radius_ = 0.0;
    double start_theta_ = 0.0;
    double angle_ = 0.0;
    double drag_angle_ = 0.0;
    double previous_pointer_theta_ = 0.0;
    bool dragging_ = false;
    QGraphicsEllipseItem* arc_ = nullptr;
    QGraphicsLineItem* radius_line_ = nullptr;
    QGraphicsEllipseItem* pivot_handle_ = nullptr;
    QGraphicsEllipseItem* angle_handle_ = nullptr;
    std::function<void(double)> preview_;
    std::function<void(double)> commit_;
    std::function<void()> cancel_;

    QPointF start_point() const {
        return pivot_ + QPointF(radius_*std::cos(start_theta_), radius_*std::sin(start_theta_));
    }
    QPointF end_point() const {
        const auto theta = start_theta_ + angle_;
        return pivot_ + QPointF(radius_*std::cos(theta), radius_*std::sin(theta));
    }
    double hit_tolerance() const { return 9.0 / std::max(0.001, std::abs(scene_.scale())); }
    bool hits_angle_handle(const QPointF& point) const { return ui::distance(point, end_point()) <= hit_tolerance(); }
    void set_cursor(Qt::CursorShape cursor) {
        if (!scene_.views().isEmpty()) scene_.views().first()->viewport()->setCursor(cursor);
    }
    void clear_cursor() {
        if (!scene_.views().isEmpty()) scene_.views().first()->viewport()->unsetCursor();
    }
    void update_graphics() {
        ui::set_arc(arc_, pivot_, radius_, start_theta_, angle_);
        radius_line_->setLine(QLineF(pivot_, start_point()));
        pivot_handle_->setPos(pivot_);
        angle_handle_->setPos(end_point());
    }
public:
    rotation_action_adornment(ui::canvas::scene& scene, QPointF pivot, double radius,
        double start_theta, double angle, std::function<void(double)> preview,
        std::function<void(double)> commit, std::function<void()> cancel)
        : scene_(scene), pivot_(pivot), radius_(radius), start_theta_(start_theta), angle_(angle),
          preview_(std::move(preview)), commit_(std::move(commit)), cancel_(std::move(cancel)) {
        const QColor accent("#35d0c5");
        arc_ = new QGraphicsEllipseItem;
        QPen arc_pen(accent, 2.5, Qt::DotLine, Qt::RoundCap, Qt::RoundJoin); arc_pen.setCosmetic(true);
        arc_->setPen(arc_pen); arc_->setBrush(Qt::NoBrush); arc_->setZValue(20000); scene_.addItem(arc_);

        radius_line_ = new QGraphicsLineItem;
        QPen radius_pen(accent); radius_pen.setWidthF(1.0); radius_pen.setCosmetic(true); radius_pen.setStyle(Qt::DashLine);
        radius_pen.setColor(QColor(accent.red(), accent.green(), accent.blue(), 150));
        radius_line_->setPen(radius_pen); radius_line_->setZValue(19999); scene_.addItem(radius_line_);

        auto make_handle = [&](double diameter, bool filled) {
            auto* handle = new QGraphicsEllipseItem(-diameter/2.0, -diameter/2.0, diameter, diameter);
            handle->setFlag(QGraphicsItem::ItemIgnoresTransformations);
            QPen pen(accent, 2.0); pen.setCosmetic(true); handle->setPen(pen);
            handle->setBrush(filled ? QBrush(accent) : QBrush(Qt::NoBrush)); handle->setZValue(20001); scene_.addItem(handle);
            return handle;
        };
        pivot_handle_ = make_handle(8.0, false);
        angle_handle_ = make_handle(12.0, true);
        update_graphics();
    }
    ~rotation_action_adornment() override {
        clear_cursor();
        delete arc_; delete radius_line_; delete pivot_handle_; delete angle_handle_;
    }
    bool keyPressEvent(QKeyEvent* event) override {
        if (!dragging_ || event->key() != Qt::Key_Escape) return false;
        dragging_ = false; clear_cursor(); if (cancel_) cancel_(); return true;
    }
    bool mousePressEvent(QGraphicsSceneMouseEvent* event) override {
        if (event->button() != Qt::LeftButton || !hits_angle_handle(event->scenePos())) return false;
        dragging_ = true; drag_angle_ = angle_;
        previous_pointer_theta_ = ui::angle_through_points(pivot_, event->scenePos());
        set_cursor(Qt::ClosedHandCursor);
        return true;
    }
    bool mouseMoveEvent(QGraphicsSceneMouseEvent* event) override {
        if (!dragging_) {
            if (hits_angle_handle(event->scenePos())) { set_cursor(Qt::OpenHandCursor); return true; }
            clear_cursor(); return false;
        }
        const auto theta = ui::angle_through_points(pivot_, event->scenePos());
        drag_angle_ += sm::angular_distance(previous_pointer_theta_, theta);
        previous_pointer_theta_ = theta; angle_ = drag_angle_; update_graphics();
        if (preview_) preview_(angle_);
        return true;
    }
    bool mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override {
        if (!dragging_ || event->button() != Qt::LeftButton) return false;
        mouseMoveEvent(event); dragging_ = false; clear_cursor();
        const auto final_angle = angle_; if (commit_) commit_(final_angle);
        return true;
    }
    void cancel() override {
        if (!dragging_) return;
        dragging_ = false; clear_cursor(); if (cancel_) cancel_();
    }
};
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
    connect(start_,qOverload<int>(&QSpinBox::valueChanged),this,[this](int value){
        if(!updating_) edit_selected_action([&](auto& action){action.start=value;});
    });
    connect(duration_,qOverload<int>(&QSpinBox::valueChanged),this,[this](int value){
        if(!updating_) edit_selected_action([&](auto& action){action.duration=value;});
    });
    connect(easing_,qOverload<int>(&QComboBox::currentIndexChanged),this,[this](int index){
        if(!updating_ && index>=0) edit_selected_action([&](auto& action){action.easing=sm::easing(index);});
    });
    connect(bone_,qOverload<int>(&QComboBox::currentIndexChanged),this,[this](int index){
        if(updating_ || index<0) return;
        edit_selected_action([&](auto& action){if(auto* r=std::get_if<sm::rigid_rotation>(&action.data)) r->bone=id(bone_->itemData(index).toString());});
    });
    connect(pivot_,qOverload<int>(&QComboBox::currentIndexChanged),this,[this](int index){
        if(!updating_ && index>=0) edit_selected_action([&](auto& action){if(auto* r=std::get_if<sm::rigid_rotation>(&action.data)) r->pivot=sm::rotation_pivot(index);});
    });
    connect(propagation_,qOverload<int>(&QComboBox::currentIndexChanged),this,[this](int index){
        if(!updating_ && index>=0) edit_selected_action([&](auto& action){if(auto* r=std::get_if<sm::rigid_rotation>(&action.data)) r->propagation=sm::rotation_propagation(index);});
    });
    connect(effector_,qOverload<int>(&QComboBox::currentIndexChanged),this,[this](int index){
        if(updating_ || index<0) return;
        edit_selected_action([&](auto& action){if(auto* r=std::get_if<sm::ik_rotation>(&action.data)) r->effector=id(effector_->itemData(index).toString());});
    });
    connect(pivot_node_,qOverload<int>(&QComboBox::currentIndexChanged),this,[this](int index){
        if(updating_ || index<0) return;
        edit_selected_action([&](auto& action){if(auto* r=std::get_if<sm::ik_rotation>(&action.data)) r->pivot_node=id(pivot_node_->itemData(index).toString());});
    });
    connect(angle_,qOverload<double>(&QDoubleSpinBox::valueChanged),this,[this](double value){
        if(updating_) return;
        edit_selected_action([&](auto& action){
            if(auto* r=std::get_if<sm::rigid_rotation>(&action.data)) r->angle=value/degrees;
            else if(auto* r=std::get_if<sm::ik_rotation>(&action.data)) r->angle=value/degrees;
        });
    });
    connect(layer_,qOverload<int>(&QComboBox::activated),this,[this](int index){
        if(updating_) return;
        const row_head_position row{index%2==0 ? row_head_position::placement::between_rows : row_head_position::placement::on_row,index/2};
        if(selected_action()) edit_selected_action([](auto&){},row);
        else { insertion_=row; timeline_->set_row_head(insertion_); }
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
    canvases_.active_canvas().clear_interactive_adornment();
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
    evaluate(*a,time_);present(*a,time_);refresh_parameters();refresh_action_adornment();
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
    remove_->setEnabled(action);start_->setEnabled(action);
    if(rotation) {
        auto bone=working_ ? working_->get<sm::bone>(rotation->bone) : sm::maybe_bone_ref{};
        selection_label_->setText(QString("Selected action — Rotate %1").arg(bone?QString::fromStdString(bone->get().name()):"missing bone"));
    } else if(ik) {
        auto effector=working_ ? working_->get<sm::node>(ik->effector) : sm::maybe_node_ref{};
        selection_label_->setText(QString("Selected action — IK rotate %1").arg(effector?QString::fromStdString(effector->get().name()):"missing effector"));
    } else selection_label_->setText(action ? "Selected action" : "No action selected");
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
void ui::pane::animation_timeline::preview_selected_angle(double angle) {
    auto* a=current(); auto* action=selected_action(); if(!a || !action) return;
    auto preview=*a;
    for(auto& layer:preview.layers) for(auto& candidate:layer.actions) if(candidate.id==selected_) {
        if(auto* r=std::get_if<sm::rigid_rotation>(&candidate.data)) r->angle=angle;
        else if(auto* r=std::get_if<sm::ik_rotation>(&candidate.data)) r->angle=angle;
    }
    pause(); evaluate(preview,time_); present(preview,time_);
    QSignalBlocker block(angle_); angle_->setValue(angle*degrees);
    message(QString("Rotation angle: %1° — release to commit; Escape cancels.").arg(angle*degrees,0,'f',1));
}
void ui::pane::animation_timeline::commit_selected_angle(double angle) {
    if(const auto* action=selected_action()) {
        const double current_angle=std::visit([](const auto& data)->double {
            using T=std::decay_t<decltype(data)>;
            if constexpr(std::is_same_v<T,sm::rigid_rotation> || std::is_same_v<T,sm::ik_rotation>) return data.angle;
            else return 0.0;
        },action->data);
        if(std::abs(current_angle-angle)<1e-12) {refresh();return;}
    }
    edit_selected_action([&](auto& action){
        if(auto* r=std::get_if<sm::rigid_rotation>(&action.data)) r->angle=angle;
        else if(auto* r=std::get_if<sm::ik_rotation>(&action.data)) r->angle=angle;
    });
}
void ui::pane::animation_timeline::refresh_action_adornment() {
    auto& scene=canvases_.active_canvas();
    scene.clear_interactive_adornment();
    auto* a=current(); auto* action=selected_action();
    if(!a || !action || !working_) return;
    const auto* rigid=std::get_if<sm::rigid_rotation>(&action->data);
    const auto* ik=std::get_if<sm::ik_rotation>(&action->data);
    if(!rigid && !ik) return;
    const auto& data=project_.core().animation_data(character_);
    const auto* base=data.find_pose(a->base_pose); if(!base) return;

    QPointF pivot_point, rotating_point; double angle=rigid ? rigid->angle : ik->angle; bool valid=false;
    try {
        // Draw/edit the action in the pose it receives as input, not in the pose
        // after this action (or higher layers) have already transformed it.
        auto before=actions_before(*a,*action);
        sm::evaluate_animation(before,*base,*working_,time_);
        if(rigid) {
            if(auto bone=working_->get<sm::bone>(rigid->bone)) {
                auto& pivot=rigid->pivot==sm::rotation_pivot::root ? bone->get().parent_node() : bone->get().child_node();
                auto& rotating=rigid->pivot==sm::rotation_pivot::root ? bone->get().child_node() : bone->get().parent_node();
                pivot_point=ui::to_qt_pt(pivot.world_pos()); rotating_point=ui::to_qt_pt(rotating.world_pos()); valid=true;
            }
        } else {
            auto pivot=working_->get<sm::node>(ik->pivot_node); auto effector=working_->get<sm::node>(ik->effector);
            if(pivot && effector && pivot->get().id()!=effector->get().id()) {
                pivot_point=ui::to_qt_pt(pivot->get().world_pos()); rotating_point=ui::to_qt_pt(effector->get().world_pos()); valid=true;
            }
        }
        sm::evaluate_animation(*a,*base,*working_,time_);
    } catch(...) {
        try { sm::evaluate_animation(*a,*base,*working_,time_); } catch(...) {}
        return;
    }
    if(!valid) return;
    const auto radius=ui::distance(pivot_point,rotating_point);
    if(!(radius>0.0) || !std::isfinite(radius)) return;
    const auto start_theta=ui::angle_through_points(pivot_point,rotating_point);
    scene.set_interactive_adornment(std::make_shared<rotation_action_adornment>(scene,pivot_point,radius,start_theta,angle,
        [this](double value){preview_selected_angle(value);},
        [this](double value){commit_selected_angle(value);},
        [this]{refresh();message("Action edit cancelled.");}));
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
void ui::pane::animation_timeline::rotation_begin(const authored_rotation& rotation) {
    if(!current()) return;
    pause(); cancel_gesture(); canvases_.active_canvas().clear_interactive_adornment();
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
