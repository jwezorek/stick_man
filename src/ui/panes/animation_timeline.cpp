#include "animation_timeline.hpp"
#include "../canvas/canvas_manager.hpp"
#include "../canvas/bone_item.hpp"
#include "../canvas/node_item.hpp"
#include "../tools/animate_tool.hpp"
#include "../tools/select_tool_panel.hpp"
#include "../tools/motion_path_fit.hpp"
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

class translation_action_adornment final : public ui::canvas::interactive_adornment {
    enum class role { control1, control2, endpoint };
    struct handle { std::size_t segment=0; role kind=role::endpoint; QGraphicsEllipseItem* item=nullptr; };
    ui::canvas::scene& scene_;
    sm::motion_path path_;
    sm::reference_frame frame_;
    sm::point local_offset_{};
    QGraphicsPathItem* curve_=nullptr;
    QGraphicsEllipseItem* origin_handle_=nullptr;
    std::vector<QGraphicsLineItem*> guides_;
    std::vector<handle> handles_;
    std::optional<std::size_t> dragging_;
    std::function<void(const sm::motion_path&)> preview_;
    std::function<void(const sm::motion_path&)> commit_;
    std::function<void()> cancel_;

    QPointF origin() const { return ui::to_qt_pt(frame_.local_to_world(local_offset_)); }
    QPointF world(sm::point p) const { return ui::to_qt_pt(frame_.local_to_world(local_offset_+p)); }
    sm::point local(QPointF p) const { return frame_.world_to_local(ui::from_qt_pt(p))-local_offset_; }
    double hit_tolerance() const { return 9.0/std::max(0.001,std::abs(scene_.scale())); }
    void set_cursor(Qt::CursorShape c){if(!scene_.views().isEmpty())scene_.views().first()->viewport()->setCursor(c);}
    void clear_cursor(){if(!scene_.views().isEmpty())scene_.views().first()->viewport()->unsetCursor();}
    static sm::point direction(sm::point p){double d=std::sqrt(p.x*p.x+p.y*p.y);return d>1e-9?(1.0/d)*p:sm::point{1,0};}
    QGraphicsEllipseItem* make_handle(double diameter,bool filled) {
        auto* h=new QGraphicsEllipseItem(-diameter/2,-diameter/2,diameter,diameter);h->setFlag(QGraphicsItem::ItemIgnoresTransformations);
        QPen pen(QColor("#35d0c5"),2.0);pen.setCosmetic(true);h->setPen(pen);h->setBrush(filled?QBrush(QColor("#35d0c5")):QBrush(Qt::NoBrush));h->setZValue(20002);scene_.addItem(h);return h;
    }
    void rebuild_handles() {
        for(auto& h:handles_)delete h.item;handles_.clear();for(auto* g:guides_)delete g;guides_.clear();
        auto add=[&](std::size_t segment,role kind,sm::point p,bool filled){auto* item=make_handle(filled?11:9,filled);item->setPos(world(p));handles_.push_back({segment,kind,item});};
        auto guide=[&](sm::point a,sm::point b){auto* line=new QGraphicsLineItem(QLineF(world(a),world(b)));QPen pen(QColor(53,208,197,150),1,Qt::DashLine);pen.setCosmetic(true);line->setPen(pen);line->setZValue(19999);scene_.addItem(line);guides_.push_back(line);};
        std::visit([&](const auto& g){using T=std::decay_t<decltype(g)>;
            if constexpr(std::is_same_v<T,sm::line_path>) add(0,role::endpoint,g.end,true);
            else if constexpr(std::is_same_v<T,sm::cubic_bezier_path>){guide(g.start,g.control1);guide(g.control2,g.end);add(0,role::control1,g.control1,false);add(0,role::control2,g.control2,false);add(0,role::endpoint,g.end,true);}
            else for(std::size_t i=0;i<g.segments.size();++i){const auto& c=g.segments[i];guide(c.start,c.control1);guide(c.control2,c.end);add(i,role::control1,c.control1,false);add(i,role::control2,c.control2,false);add(i,role::endpoint,c.end,true);}
        },path_.geometry());
    }
    void update_graphics() {
        QPainterPath qp(origin());
        std::visit([&](const auto& g){using T=std::decay_t<decltype(g)>;
            if constexpr(std::is_same_v<T,sm::line_path>) qp.lineTo(world(g.end));
            else if constexpr(std::is_same_v<T,sm::cubic_bezier_path>) qp.cubicTo(world(g.control1),world(g.control2),world(g.end));
            else for(const auto& c:g.segments) qp.cubicTo(world(c.control1),world(c.control2),world(c.end));
        },path_.geometry());curve_->setPath(qp);rebuild_handles();origin_handle_->setPos(origin());
    }
    void set_handle(std::size_t index,sm::point p) {
        auto geometry=path_.geometry();const auto h=handles_[index];
        std::visit([&](auto& g){using T=std::decay_t<decltype(g)>;
            if constexpr(std::is_same_v<T,sm::line_path>) g.end=p;
            else if constexpr(std::is_same_v<T,sm::cubic_bezier_path>){
                if(h.kind==role::control1)g.control1=p;else if(h.kind==role::control2)g.control2=p;else {auto d=p-g.end;g.end=p;g.control2+=d;}
            } else {
                auto& c=g.segments[h.segment];
                if(h.kind==role::control1){
                    c.control1=p;if(h.segment>0){auto& prev=g.segments[h.segment-1];double l=sm::distance(prev.control2,c.start);prev.control2=c.start-l*direction(p-c.start);}
                } else if(h.kind==role::control2){
                    c.control2=p;if(h.segment+1<g.segments.size()){auto& next=g.segments[h.segment+1];double l=sm::distance(next.control1,c.end);next.control1=c.end-l*direction(p-c.end);}
                } else {
                    auto old=c.end,d=p-old;c.end=p;c.control2+=d;
                    if(h.segment+1<g.segments.size()){auto& next=g.segments[h.segment+1];next.start=p;next.control1+=d;}
                }
            }
        },geometry);path_.set_geometry(std::move(geometry));update_graphics();
    }
    std::optional<std::size_t> hit(QPointF p) const {
        double best=hit_tolerance();std::optional<std::size_t> result;
        for(std::size_t i=0;i<handles_.size();++i){double d=ui::distance(p,handles_[i].item->pos());if(d<=best){best=d;result=i;}}
        return result;
    }
public:
    translation_action_adornment(ui::canvas::scene& scene,sm::motion_path path,sm::reference_frame frame,sm::point local_offset,
        std::function<void(const sm::motion_path&)> preview,std::function<void(const sm::motion_path&)> commit,std::function<void()> cancel)
        :scene_(scene),path_(std::move(path)),frame_(frame),local_offset_(local_offset),preview_(std::move(preview)),commit_(std::move(commit)),cancel_(std::move(cancel)){
        curve_=new QGraphicsPathItem;QPen pen(QColor("#35d0c5"),2.5,Qt::DotLine,Qt::RoundCap,Qt::RoundJoin);pen.setCosmetic(true);curve_->setPen(pen);curve_->setBrush(Qt::NoBrush);curve_->setZValue(20000);scene_.addItem(curve_);
        origin_handle_=make_handle(8,false);update_graphics();
    }
    ~translation_action_adornment() override {clear_cursor();for(auto& h:handles_)delete h.item;for(auto* g:guides_)delete g;delete curve_;delete origin_handle_;}
    bool keyPressEvent(QKeyEvent* event) override {if(!dragging_||event->key()!=Qt::Key_Escape)return false;dragging_.reset();clear_cursor();if(cancel_)cancel_();return true;}
    bool mousePressEvent(QGraphicsSceneMouseEvent* event) override {if(event->button()!=Qt::LeftButton)return false;auto h=hit(event->scenePos());if(!h)return false;dragging_=h;set_cursor(Qt::ClosedHandCursor);return true;}
    bool mouseMoveEvent(QGraphicsSceneMouseEvent* event) override {
        if(!dragging_){if(hit(event->scenePos())){set_cursor(Qt::OpenHandCursor);return true;}clear_cursor();return false;}
        auto index=*dragging_; // update_graphics rebuilds handles, but preserves descriptor ordering.
        set_handle(index,local(event->scenePos()));if(preview_)preview_(path_);return true;
    }
    bool mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override {if(!dragging_||event->button()!=Qt::LeftButton)return false;mouseMoveEvent(event);dragging_.reset();clear_cursor();if(commit_)commit_(path_);return true;}
    void cancel() override {if(!dragging_)return;dragging_.reset();clear_cursor();if(cancel_)cancel_();}
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
    layer_->setToolTip("Layers run bottom to top. Reference-relative actions move upward when needed to follow actions that move their reference frame.");
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
sm::object_id ui::pane::animation_timeline::character_root_bone() const {
    auto c=project_.core().character(character_);
    return c ? c->get().character_root_bone() : sm::object_id{};
}
ui::tool::select_tool_panel& ui::pane::animation_timeline::animation_tool_panel() const {
    auto& animation_tool=static_cast<tool::animate&>(tools_.tool_from_id(tool::id::animate));
    return *static_cast<tool::select_tool_panel*>(animation_tool.settings_widget());
}
void ui::pane::animation_timeline::begin(sm::object_id character,sm::object_id animation,sm::topology& working) {
    character_=character;animation_=animation;working_=&working;selected_={};time_=0;insertion_={};
    bone_->clear(); effector_->clear(); pivot_node_->clear();
    std::vector<std::pair<sm::object_id,std::string>> reference_bones;
    for(auto s:working.skeletons()) {
        for(auto b:s->bones()) {
            bone_->addItem(QString::fromStdString(b->name()),text(b->id()));
            reference_bones.emplace_back(b->id(),b->name());
        }
        for(auto n:s->nodes()) {
            const auto name=QString::fromStdString(n->name());
            effector_->addItem(name,text(n->id())); pivot_node_->addItem(name,text(n->id()));
        }
    }
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
    working_=nullptr; selected_={}; last_evaluation_.reset(); hide();
}
void ui::pane::animation_timeline::message(QString text) {status_->setText(std::move(text));}
void ui::pane::animation_timeline::evaluate(const sm::animation& a,sm::animation_time time) {
    const auto& data=project_.core().animation_data(character_);
    last_evaluation_.reset();
    const auto* base=data.find_pose(a.base_pose); if(!base || !working_) return;
    last_evaluation_=sm::evaluate_animation(a,*base,character_root_bone(),*working_,time);
    if(!last_evaluation_->invalid_actions.empty()) message("Some actions have missing or invalid targets and are skipped.");
    else if(!last_evaluation_->unsupported_actions.empty()) message("Some action types are not previewed in this phase.");
    canvases_.active_canvas().sync_to_model();
    canvases_.active_canvas().update();
}
void ui::pane::animation_timeline::present(const sm::animation& a,sm::animation_time time,std::optional<sm::object_id> provisional) {
    const int rows=int(a.layers.size()); timeline_->set_rows(rows);
    const auto root_bone=character_root_bone();
    std::vector<timeline_item> items;
    auto path_name=[](sm::motion_path_kind kind)->QString {
        switch(kind){case sm::motion_path_kind::straight:return "Straight";case sm::motion_path_kind::curve:return "Curve";case sm::motion_path_kind::spline:return "Spline";}
        return "Path";
    };
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
        } else if(const auto* translation=std::get_if<sm::rigid_translation>(&action.data)) {
            supported=true;color=timeline_color::green;
            invalid=translation->skeletons.empty();
            for(auto sid:translation->skeletons) invalid=invalid || !working_->contains_skeleton(sid);
            if(translation->reference==sm::translation_reference::bone) invalid=invalid || !working_->get<sm::bone>(translation->reference_bone);
            else invalid=invalid || !working_->get<sm::bone>(root_bone);
            label=QString("Translate %1 skeleton%2 (%3)").arg(translation->skeletons.size()).arg(translation->skeletons.size()==1?"":"s").arg(path_name(translation->path.kind()));
        } else if(const auto* translation=std::get_if<sm::ik_translation>(&action.data)) {
            supported=true;color=timeline_color::orange;
            auto effector=working_->get<sm::node>(translation->effector);invalid=!effector;
            for(auto pin:translation->pins) invalid=invalid || !working_->get<sm::node>(pin);
            if(translation->reference==sm::translation_reference::bone) invalid=invalid || !working_->get<sm::bone>(translation->reference_bone);
            else invalid=invalid || !working_->get<sm::bone>(root_bone);
            label=QString("IK translate %1 (%2)").arg(effector?QString::fromStdString(effector->get().name()):"missing effector").arg(path_name(translation->path.kind()));
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
    const bool translation = action && (std::holds_alternative<sm::rigid_translation>(action->data) || std::holds_alternative<sm::ik_translation>(action->data));
    bone_label_->setVisible(bone_rotation); bone_->setVisible(bone_rotation);
    pivot_label_->setVisible(bone_rotation); pivot_->setVisible(bone_rotation);
    propagation_label_->setVisible(bone_rotation); propagation_->setVisible(bone_rotation);
    effector_label_->setVisible(ik_rotation); effector_->setVisible(ik_rotation);
    pivot_node_label_->setVisible(ik_rotation); pivot_node_->setVisible(ik_rotation);
    angle_label_->setVisible(bone_rotation || ik_rotation); angle_->setVisible(bone_rotation || ik_rotation);
    easing_->setEnabled(!translation);
}
void ui::pane::animation_timeline::sync_animation_tool_properties() {
    auto& panel=animation_tool_panel();
    if(const auto* action=selected_action()) {
        if(const auto* t=std::get_if<sm::rigid_translation>(&action->data)) {
            panel.set_animation_translation({t->path.kind(),t->reference,t->reference_bone},false);return;
        }
        if(const auto* t=std::get_if<sm::ik_translation>(&action->data)) {
            panel.set_animation_translation({t->path.kind(),t->reference,t->reference_bone},true);return;
        }
    }
    panel.set_animation_translation(panel.animation_translation(),false);
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
    const auto* rigid_translation=action ? std::get_if<sm::rigid_translation>(&action->data):nullptr;
    const auto* ik_translation=action ? std::get_if<sm::ik_translation>(&action->data):nullptr;
    remove_->setEnabled(action);start_->setEnabled(action);
    if(rotation) {
        auto bone=working_ ? working_->get<sm::bone>(rotation->bone) : sm::maybe_bone_ref{};
        selection_label_->setText(QString("Selected action — Rotate %1").arg(bone?QString::fromStdString(bone->get().name()):"missing bone"));
    } else if(ik) {
        auto effector=working_ ? working_->get<sm::node>(ik->effector) : sm::maybe_node_ref{};
        selection_label_->setText(QString("Selected action — IK rotate %1").arg(effector?QString::fromStdString(effector->get().name()):"missing effector"));
    } else if(rigid_translation) {
        selection_label_->setText(QString("Selected action — Translate %1 skeleton%2").arg(rigid_translation->skeletons.size()).arg(rigid_translation->skeletons.size()==1?"":"s"));
    } else if(ik_translation) {
        auto effector=working_ ? working_->get<sm::node>(ik_translation->effector) : sm::maybe_node_ref{};
        selection_label_->setText(QString("Selected action — IK translate %1").arg(effector?QString::fromStdString(effector->get().name()):"missing effector"));
    } else selection_label_->setText(action ? "Selected action" : "No action selected");
    if(action) {
        start_->setValue(int(std::min<qint64>(INT_MAX,action->start)));
        duration_->setValue(int(std::min<qint64>(INT_MAX,action->duration)));
        easing_->setCurrentIndex((rigid_translation||ik_translation)?int(sm::easing::linear):int(action->easing));
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
    sync_animation_tool_properties();
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
void ui::pane::animation_timeline::preview_selected_path(const sm::motion_path& path) {
    auto* a=current();auto* action=selected_action();if(!a||!action)return;
    auto preview=*a;
    for(auto& layer:preview.layers) for(auto& candidate:layer.actions) if(candidate.id==selected_) {
        if(auto* t=std::get_if<sm::rigid_translation>(&candidate.data))t->path=path;
        else if(auto* t=std::get_if<sm::ik_translation>(&candidate.data))t->path=path;
    }
    pause();evaluate(preview,time_);present(preview,time_);
    message(QString("Motion path length: %1 — release to commit; Escape cancels.").arg(path.length(),0,'f',1));
}
void ui::pane::animation_timeline::commit_selected_path(const sm::motion_path& path) {
    edit_selected_action([&](auto& action){
        if(auto* t=std::get_if<sm::rigid_translation>(&action.data))t->path=path;
        else if(auto* t=std::get_if<sm::ik_translation>(&action.data))t->path=path;
    });
}
void ui::pane::animation_timeline::refresh_action_adornment() {
    auto& scene=canvases_.active_canvas();
    scene.clear_interactive_adornment();
    auto* a=current(); auto* action=selected_action();
    if(!a || !action || !working_ || !last_evaluation_) return;
    const auto* rigid_rotation=std::get_if<sm::rigid_rotation>(&action->data);
    const auto* ik_rotation=std::get_if<sm::ik_rotation>(&action->data);
    const auto* rigid_translation=std::get_if<sm::rigid_translation>(&action->data);
    const auto* ik_translation=std::get_if<sm::ik_translation>(&action->data);
    if(!rigid_rotation&&!ik_rotation&&!rigid_translation&&!ik_translation)return;
    const auto found=last_evaluation_->contexts.find(action->id);
    if(found==last_evaluation_->contexts.end())return;
    const auto& context=found->second;
    try {
        if(rigid_rotation || ik_rotation) {
            if(!context.rotation)return;
            const QPointF pivot_point=ui::to_qt_pt(context.rotation->pivot);
            const QPointF rotating_point=ui::to_qt_pt(context.rotation->rotating);
            const double angle=rigid_rotation?rigid_rotation->angle:ik_rotation->angle;
            const auto radius=ui::distance(pivot_point,rotating_point);if(!(radius>0.0)||!std::isfinite(radius))return;
            const auto start_theta=ui::angle_through_points(pivot_point,rotating_point);
            scene.set_interactive_adornment(std::make_shared<rotation_action_adornment>(scene,pivot_point,radius,start_theta,angle,
                [this](double value){preview_selected_angle(value);},[this](double value){commit_selected_angle(value);},
                [this]{refresh();message("Action edit cancelled.");}));
            return;
        }

        if(!context.translation_reference_frame)return;
        const sm::motion_path* path=rigid_translation?&rigid_translation->path:&ik_translation->path;
        const sm::point local_offset=ik_translation?ik_translation->effector_start:sm::point{};
        scene.set_interactive_adornment(std::make_shared<translation_action_adornment>(scene,*path,*context.translation_reference_frame,local_offset,
            [this](const sm::motion_path& p){preview_selected_path(p);},[this](const sm::motion_path& p){commit_selected_path(p);},
            [this]{refresh();message("Action edit cancelled.");}));
    } catch(...) {}
}
void ui::pane::animation_timeline::translation_properties_changed() {
    if(updating_ || !working_) return;
    auto* action=selected_action();auto* a=current();if(!action||!a)return;
    const auto* rt=std::get_if<sm::rigid_translation>(&action->data);
    const auto* it=std::get_if<sm::ik_translation>(&action->data);
    if(!rt&&!it)return;

    auto settings=animation_tool_panel().animation_translation();
    const auto old_reference=rt?rt->reference:it->reference;
    const auto old_bone=rt?rt->reference_bone:it->reference_bone;
    // A missing reference is displayed as a blank combo rather than another bone.
    // Preserve that missing ID for unrelated edits so Path changes do not silently
    // retarget (or become impossible) before the user repairs the reference.
    if(settings.reference==sm::translation_reference::bone && settings.reference_bone.is_nil() &&
       old_reference==sm::translation_reference::bone) settings.reference_bone=old_bone;
    const bool reference_changed=settings.reference!=old_reference ||
        (settings.reference==sm::translation_reference::bone && settings.reference_bone!=old_bone);

    std::optional<sm::point> new_effector_start;
    if(it && reference_changed) {
        const auto& data=project_.core().animation_data(character_);const auto* base=data.find_pose(a->base_pose);if(!base)return;
        const auto root_bone=character_root_bone();
        try {
            auto probe=*a;
            for(auto& layer:probe.layers)for(auto& candidate:layer.actions)if(candidate.id==action->id)
                if(auto* t=std::get_if<sm::ik_translation>(&candidate.data)) {t->reference=settings.reference;t->reference_bone=settings.reference_bone;}
            probe=sm::place_animation_action(probe,action->id,root_bone,project_.core().topology());
            const auto report=sm::evaluate_animation(probe,*base,root_bone,*working_,action->start);
            const auto context=report.contexts.find(action->id);
            if(context==report.contexts.end() || !context->second.translation_reference_frame || !context->second.translation_anchor_world) {
                refresh();message("The selected translation reference or effector is missing.");return;
            }
            new_effector_start=context->second.translation_reference_frame->world_to_local(*context->second.translation_anchor_world);
        } catch(const std::exception& error){refresh();message(error.what());return;}
    }
    edit_selected_action([&](auto& candidate){
        if(auto* t=std::get_if<sm::rigid_translation>(&candidate.data)) {
            if(t->path.kind()!=settings.path)t->path=tool::convert_motion_path(t->path,settings.path);
            t->reference=settings.reference;t->reference_bone=settings.reference_bone;
        } else if(auto* t=std::get_if<sm::ik_translation>(&candidate.data)) {
            if(t->path.kind()!=settings.path)t->path=tool::convert_motion_path(t->path,settings.path);
            t->reference=settings.reference;t->reference_bone=settings.reference_bone;
            if(new_effector_start)t->effector_start=*new_effector_start;
        }
    });
}
void ui::pane::animation_timeline::capture_selected_pins() {
    auto* action=selected_action();if(!action||!working_)return;
    const auto* translation=std::get_if<sm::ik_translation>(&action->data);if(!translation)return;
    auto effector=working_->get<sm::node>(translation->effector);if(!effector){message("The IK effector is missing.");return;}
    std::vector<sm::object_id> pins;
    const auto owner=effector->get().owner().id();
    for(auto pin:canvases_.active_canvas().pinned_node_ids()) if(auto node=working_->get<sm::node>(pin);node&&pin!=translation->effector&&node->get().owner().id()==owner)pins.push_back(pin);
    std::ranges::sort(pins);pins.erase(std::unique(pins.begin(),pins.end()),pins.end());
    edit_selected_action([pins=std::move(pins)](auto& candidate) mutable {if(auto* t=std::get_if<sm::ik_translation>(&candidate.data))t->pins=std::move(pins);});
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
    const bool translation=std::holds_alternative<sm::rigid_translation>(authored)||std::holds_alternative<sm::ik_translation>(authored);
    g.action.easing=translation?sm::easing::linear:sm::easing(easing_->currentIndex());gesture_=std::move(g);
    message(translation?"Drag to author the translation path; release to create the action. Escape cancels.":"Drag to author the rotation; release to create the action. Escape cancels.");
}
void ui::pane::animation_timeline::action_update(const authored_action& authored) {
    if(!gesture_)return;auto& g=*gesture_;g.action.data=authored;
    QString preview_text;
    if(const auto* r=std::get_if<sm::rigid_rotation>(&authored)){g.moved=std::abs(r->angle)>1e-8;preview_text=QString("Rotation preview: %1°").arg(r->angle*degrees,0,'f',1);}
    else if(const auto* r=std::get_if<sm::ik_rotation>(&authored)){g.moved=std::abs(r->angle)>1e-8;preview_text=QString("Rotation preview: %1°").arg(r->angle*degrees,0,'f',1);}
    else if(const auto* t=std::get_if<sm::rigid_translation>(&authored)){g.moved=t->path.length()>1e-6;preview_text=QString("Translation preview: %1 units (%2)").arg(t->path.length(),0,'f',1).arg(t->path.kind()==sm::motion_path_kind::straight?"Straight":t->path.kind()==sm::motion_path_kind::curve?"Curve":"Spline");}
    else if(const auto* t=std::get_if<sm::ik_translation>(&authored)){g.moved=t->path.length()>1e-6;preview_text=QString("IK translation preview: %1 units (%2)").arg(t->path.length(),0,'f',1).arg(t->path.kind()==sm::motion_path_kind::straight?"Straight":t->path.kind()==sm::motion_path_kind::curve?"Curve":"Spline");}
    if(auto candidate=place(g.action,g.row,false,true)) {
        // The Selection tool itself owns the live manipulation during the gesture.
        // Do not reset/re-evaluate the detached topology here; doing so would move
        // the drag anchor out from under the next mouse-move event.
        time_=g.action.start+g.action.duration;present(*candidate,time_,g.action.id);
        message(QString("%1 over %2 ms. Release to create; Escape cancels.").arg(preview_text).arg(g.action.duration));
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
    const auto* action=selected_action();
    if(action && (std::holds_alternative<sm::rigid_rotation>(action->data) || std::holds_alternative<sm::ik_rotation>(action->data))) {
        angle_->setFocus(Qt::MouseFocusReason); angle_->selectAll();
    }
}
void ui::pane::animation_timeline::cancel_gesture() {
    if(!gesture_) return;
    gesture_.reset();refresh();message("Action cancelled.");
}
