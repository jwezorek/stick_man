#include "animation_pane.hpp"
#include "../widgets/timeline.hpp"
#include "../canvas/canvas_manager.hpp"
#include "../canvas/skel_item.hpp"
#include "../canvas/artwork_layer.hpp"
#include "../stick_man.hpp"
#include "../tools/tool_manager.hpp"
#include <algorithm>

namespace {
constexpr int kind_role = Qt::UserRole, owner_role = Qt::UserRole+1, id_role = Qt::UserRole+2;
enum kind { character, poses, animations, default_pose, pose, animation };
sm::object_id object_id(const QVariant& v) { return sm::object_id::from_string(v.toString().toStdString()).value_or(sm::object_id{}); }
QString text(sm::object_id id) { return QString::fromStdString(id.to_string()); }
QIcon icon(kind k) {
    QPixmap image(20,20); image.fill(Qt::transparent); QPainter p(&image); p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(QColor("#aaa5d8"),1.7));
    if (k == character) { p.drawEllipse(QPointF(10,4),2,2); p.drawLine(10,6,10,13); p.drawLine(5,8,15,8); p.drawLine(10,13,6,18); p.drawLine(10,13,14,18); }
    else if (k == poses || k == animations) {
        p.drawRoundedRect(2,5,16,13,2,2); p.drawLine(3,3,9,3);
        if (k == poses) p.drawEllipse(7,9,6,6); else p.drawPolygon(QPolygon{{8,8},{14,12},{8,16}});
    } else if (k == animation) { p.drawRoundedRect(2,3,16,14,2,2); p.drawPolygon(QPolygon{{8,6},{14,10},{8,14}}); }
    else { p.drawEllipse(3,3,14,14); if (k == default_pose) { p.drawLine(6,10,9,13); p.drawLine(9,13,15,7); } else p.drawLine(6,10,14,10); }
    return QIcon(image);
}
QString key(QTreeWidgetItem* item) { return item->data(0,owner_role).toString()+"/"+item->data(0,kind_role).toString()+"/"+item->data(0,id_role).toString(); }
}
ui::pane::animation::animation(QWidget* parent) : QDockWidget(tr("Animation"),parent) {
    auto* content = new QWidget; auto* layout = new QVBoxLayout(content); layout->setContentsMargins(6,6,6,6);
    tree_ = new QTreeWidget; tree_->setHeaderHidden(true); tree_->setObjectName("animation_asset_tree");
    tree_->setEditTriggers(QAbstractItemView::EditKeyPressed | QAbstractItemView::SelectedClicked);
    tree_->setContextMenuPolicy(Qt::CustomContextMenu); layout->addWidget(tree_);
    auto* buttons = new QHBoxLayout;
    pose_button_ = new QPushButton("+ Pose"); animation_button_ = new QPushButton("+ Animation"); delete_button_ = new QPushButton("Delete");
    for (auto b : {pose_button_,animation_button_,delete_button_}) buttons->addWidget(b);
    layout->addLayout(buttons); setWidget(content);
    connect(pose_button_, &QPushButton::clicked, this, &animation::create_pose);
    connect(animation_button_, &QPushButton::clicked, this, &animation::create_animation);
    connect(delete_button_, &QPushButton::clicked, this, &animation::delete_current);
    connect(tree_, &QTreeWidget::itemSelectionChanged, this, &animation::update_buttons);
    connect(tree_, &QTreeWidget::customContextMenuRequested, this, &animation::context_menu);
    connect(tree_, &QTreeWidget::itemChanged, this, [this](auto item, int) { if (!syncing_) rename_item(item); });
    connect(tree_->itemDelegate(), &QAbstractItemDelegate::closeEditor, this, [this] { QTimer::singleShot(0,this,&animation::offer_edit); });
    connect(tree_, &QTreeWidget::itemDoubleClicked, this, [this](auto item, int) {
        auto k = item->data(0,kind_role).toInt();
        if (k == ::animation) open_animation(object_id(item->data(0,owner_role)),object_id(item->data(0,id_role)));
        else if (k == pose || k == default_pose) apply_current();
    });
    update_buttons();
}
ui::pane::animation::~animation() {
    // Canvas items must not outlive their working rig. The window owns all widgets.
    if (working_ && canvases_) { canvases_->clear(); canvases_->active_canvas().artwork().set_preview_topology(nullptr); }
}
void ui::pane::animation::init(canvas::manager& canvases, mdl::project& project) {
    canvases_ = &canvases; project_ = &project;
    connect(&project,&mdl::project::project_changed,this,&animation::refresh);
    connect(&project,&mdl::project::new_project_opened,this,[this] { leave_animation(); refresh(); });
    connect(&canvases,&canvas::manager::selection_changed,this,&animation::update_buttons);
    auto* window = qobject_cast<QMainWindow*>(parentWidget());
    timeline_pane_ = new QDockWidget("Animation Timeline",window); timeline_pane_->setObjectName("animation_timeline_pane");
    timeline_pane_->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
    auto* content = new QWidget; auto* layout = new QVBoxLayout(content);
    auto* controls = new QWidget; auto* bar = new QHBoxLayout(controls); bar->setContentsMargins(0,0,0,0);
    for (const auto& label : {"Start", "Play", "Stop", "Record"}) bar->addWidget(new QPushButton(label));
    auto* speed = new QComboBox; speed->addItems({"0.25x", "0.5x", "1x", "2x"}); speed->setCurrentIndex(2);
    bar->addWidget(new QLabel("Recording speed")); bar->addWidget(speed); bar->addStretch(); controls->setEnabled(false);
    layout->addWidget(controls); timeline_ = new ui::timeline; timeline_->setEnabled(false); timeline_->set_rows(0); layout->addWidget(timeline_);
    layout->addWidget(new QLabel("Empty animation — action editing and playback will be added in a later stage."));
    timeline_pane_->setWidget(content); window->addDockWidget(Qt::BottomDockWidgetArea,timeline_pane_); timeline_pane_->hide();
    banner_ = new QWidget(window->centralWidget()); banner_->setObjectName("animation_mode_banner");
    banner_->setStyleSheet("#animation_mode_banner { background: #504465; border-radius: 4px; } QLabel { color: white; }");
    auto* banner_layout = new QHBoxLayout(banner_); banner_label_ = new QLabel; banner_layout->addWidget(banner_label_); banner_layout->addStretch();
    auto* leave = new QPushButton("Leave Animation"); leave->setObjectName("leave_animation"); banner_layout->addWidget(leave);
    connect(leave,&QPushButton::clicked,this,&animation::leave_animation);
    if (auto* layout = qobject_cast<QVBoxLayout*>(window->centralWidget()->layout())) layout->insertWidget(0,banner_);
    banner_->hide(); refresh();
}
sm::object_id ui::pane::animation::selected_character() const {
    if (auto* item = tree_->currentItem()) return object_id(item->data(0,owner_role));
    if (canvases_) if (auto* c = canvases_->active_canvas().selected_character()) return c->id();
    return {};
}
void ui::pane::animation::update_buttons() {
    bool available = project_ && !project_->animation_mode() && !selected_character().is_nil();
    pose_button_->setEnabled(available); animation_button_->setEnabled(available);
    auto* item = tree_->currentItem(); int k = item ? item->data(0,kind_role).toInt() : -1;
    delete_button_->setEnabled(available && (k == pose || k == ::animation));
}
QTreeWidgetItem* ui::pane::animation::find_asset(sm::object_id id) const {
    for (QTreeWidgetItemIterator it(tree_); *it; ++it) if (object_id((*it)->data(0,id_role)) == id) return *it;
    return nullptr;
}
void ui::pane::animation::refresh() {
    if (!project_) return;
    QSet<QString> expanded, existing;
    QString selected = tree_->currentItem() ? key(tree_->currentItem()) : QString{};
    for (QTreeWidgetItemIterator it(tree_); *it; ++it) { existing.insert(key(*it)); if ((*it)->isExpanded()) expanded.insert(key(*it)); }
    syncing_ = true; QSignalBlocker block(tree_); tree_->clear();
    auto add = [&](QTreeWidgetItem* parent, kind k, QString label, sm::object_id owner, sm::object_id id = {}) {
        auto* item = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(tree_);
        item->setText(0,label); item->setIcon(0,icon(k)); item->setData(0,kind_role,k); item->setData(0,owner_role,text(owner)); item->setData(0,id_role,text(id));
        if (k == pose || k == ::animation) item->setFlags(item->flags() | Qt::ItemIsEditable);
        item->setExpanded(!existing.contains(key(item)) || expanded.contains(key(item)));
        if (key(item) == selected) tree_->setCurrentItem(item);
        return item;
    };
    for (auto c : project_->core().characters()) {
        auto* root = add(nullptr,character,QString::fromStdString(c->name()),c->id());
        auto* pg = add(root,poses,"Poses",c->id()); auto* ag = add(root,animations,"Animations",c->id());
        const auto& assets = c->animation_data();
        for (const auto& p : assets.poses) {
            auto* item = add(pg,p.id == assets.default_pose ? default_pose : pose,QString::fromStdString(p.name),c->id(),p.id);
            if (!sm::pose_compatible(p,project_->topology(),c->rig().skeleton_ids())) {
                item->setToolTip(0,"This pose no longer matches the rig. Update or recreate it before applying.");
                item->setForeground(0,QColor("#d99d50"));
            }
        }
        for (const auto& a : assets.animations) {
            auto* item = add(ag,::animation,QString::fromStdString(a.name),c->id(),a.id);
            QFont f = item->font(0); f.setBold(a.id == active_animation_); item->setFont(0,f);
        }
    }
    syncing_ = false; update_buttons();
}
void ui::pane::animation::select_and_rename(sm::object_id id) {
    show(); raise();
    if (auto* item = find_asset(id)) { tree_->setCurrentItem(item); for (auto* p = item->parent(); p; p = p->parent()) p->setExpanded(true); tree_->scrollToItem(item); tree_->editItem(item); }
}
void ui::pane::animation::create_pose() {
    auto cid = selected_character(); if (!project_ || project_->animation_mode() || cid.is_nil()) return;
    const auto& c = project_->core().character(cid).value().get();
    auto p = sm::capture_pose(project_->topology(),c.rig().skeleton_ids(),"Pose "+std::to_string(c.animation_data().poses.size()));
    auto id = p.id; project_->edit_animation_data(cid,[&](auto& data) { data.poses.push_back(p); }); select_and_rename(id);
}
void ui::pane::animation::create_animation() {
    auto cid = selected_character(); if (!project_ || project_->animation_mode() || cid.is_nil()) return;
    const auto& data = project_->core().animation_data(cid);
    sm::animation a; a.name = "Animation "+std::to_string(data.animations.size()+1); a.base_pose = data.default_pose;
    project_->edit_animation_data(cid,[&](auto& data) { data.animations.push_back(a); });
    pending_edit_ = std::pair{cid,a.id}; select_and_rename(a.id);
}
void ui::pane::animation::offer_edit() {
    if (!pending_edit_) return;
    auto [cid,aid] = *pending_edit_; pending_edit_.reset();
    auto c = project_->core().character(cid); if (!c) return;
    auto a = c->get().animation_data().find_animation(aid); if (!a) return;
    QMessageBox prompt(QMessageBox::Question,"Edit animation",QString("Edit \"%1\" now?\nEditing opens the Animation Timeline and enters Animation Mode.").arg(QString::fromStdString(a->name)),QMessageBox::NoButton,this);
    auto* edit = prompt.addButton("Edit Animation",QMessageBox::AcceptRole); prompt.addButton("Not Now",QMessageBox::RejectRole);
    prompt.exec(); if (prompt.clickedButton() == edit) open_animation(cid,aid);
}
void ui::pane::animation::rename_item(QTreeWidgetItem* item) {
    if (!project_ || project_->animation_mode()) return;
    auto cid = object_id(item->data(0,owner_role)), id = object_id(item->data(0,id_role));
    auto name = item->text(0).trimmed().toStdString(); auto k = item->data(0,kind_role).toInt();
    if (name.empty()) { QTimer::singleShot(0,this,&animation::refresh); return; }
    // Defer rebuilding the tree until the delegate has finished committing the editor.
    QTimer::singleShot(0,this,[this,cid,id,name,k] {
        if (!project_->core().character(cid)) return;
        project_->edit_animation_data(cid,[&](auto& data) {
            if (k == pose) for (auto& p : data.poses) if (p.id == id) p.name = name;
            if (k == ::animation) for (auto& a : data.animations) if (a.id == id) a.name = name;
        });
    });
}
void ui::pane::animation::apply_current() {
    auto* item = tree_->currentItem(); if (!item) return;
    try { project_->apply_pose(object_id(item->data(0,owner_role)),object_id(item->data(0,id_role))); }
    catch (const std::exception& e) { QMessageBox::warning(this,"Cannot apply pose",e.what()); }
}
void ui::pane::animation::duplicate_current() {
    auto* item = tree_->currentItem(); if (!item) return;
    auto cid = object_id(item->data(0,owner_role)), id = object_id(item->data(0,id_role)), created = sm::object_id::generate();
    int k = item->data(0,kind_role).toInt();
    project_->edit_animation_data(cid,[&](auto& data) {
        if (k == ::animation) { auto copy = *data.find_animation(id); copy.id = created; copy.name += " copy";
            for (auto& l : copy.layers) for (auto& a : l.actions) a.id = sm::object_id::generate(); data.animations.push_back(std::move(copy)); }
        else { auto copy = *data.find_pose(id); copy.id = created; copy.name += " copy"; data.poses.push_back(std::move(copy)); }
    }); select_and_rename(created);
}
void ui::pane::animation::delete_current() {
    auto* item = tree_->currentItem(); if (!item || project_->animation_mode()) return;
    auto cid = object_id(item->data(0,owner_role)), id = object_id(item->data(0,id_role)); int k = item->data(0,kind_role).toInt();
    if (k != pose && k != ::animation) return;
    QStringList dependents;
    if (k == pose) for (const auto& a : project_->core().animation_data(cid).animations) if (a.base_pose == id) dependents << QString::fromStdString(a.name);
    if (!dependents.empty() && QMessageBox::question(this,"Pose is used by animations",
        "Reassign these animations to Default and delete the pose?\n"+dependents.join(", "),QMessageBox::Yes|QMessageBox::Cancel,QMessageBox::Cancel) != QMessageBox::Yes) return;
    project_->edit_animation_data(cid,[&](auto& data) {
        if (k == ::animation) std::erase_if(data.animations,[&](auto& a) { return a.id == id; });
        else { for (auto& a : data.animations) if (a.base_pose == id) a.base_pose = data.default_pose; std::erase_if(data.poses,[&](auto& p) { return p.id == id; }); }
    });
}
void ui::pane::animation::context_menu(QPoint point) {
    auto* item = tree_->itemAt(point); if (!item || project_->animation_mode()) return;
    tree_->setCurrentItem(item); int k = item->data(0,kind_role).toInt();
    auto cid = object_id(item->data(0,owner_role)), id = object_id(item->data(0,id_role));
    QMenu menu(this);
    if (k == ::animation) menu.addAction("Edit Animation",this,[this,cid,id] { open_animation(cid,id); });
    if (k == pose || k == default_pose) {
        menu.addAction("Apply Pose",this,&animation::apply_current);
        auto* bases = menu.addMenu("Set as Animation Base");
        for (const auto& a : project_->core().animation_data(cid).animations)
            bases->addAction(QString::fromStdString(a.name),this,[this,cid,id,aid=a.id] {
                project_->edit_animation_data(cid,[&](auto& data) { for (auto& a : data.animations) if (a.id == aid) a.base_pose = id; });
            });
        bases->setEnabled(!bases->actions().empty());
    }
    if (k == default_pose) menu.addAction("Update Default from Current",this,[this,cid,id] {
        auto p = sm::capture_pose(project_->topology(),project_->core().character(cid)->get().rig().skeleton_ids(),"Default"); p.id = id;
        project_->edit_animation_data(cid,[&](auto& data) { for (auto& old : data.poses) if (old.id == id) old = p; });
    });
    if (k == pose || k == ::animation) { menu.addSeparator(); menu.addAction("Rename",this,[this,item] { tree_->editItem(item); }); }
    if (k == pose || k == ::animation || k == default_pose) menu.addAction("Duplicate",this,&animation::duplicate_current);
    if (k == pose || k == ::animation) menu.addAction("Delete",this,&animation::delete_current);
    if (!menu.isEmpty()) menu.exec(tree_->viewport()->mapToGlobal(point));
}

bool ui::pane::animation::open_animation(sm::object_id cid, sm::object_id aid) {
    if (!project_ || working_) return false;
    auto c = project_->core().character(cid); if (!c) return false;
    const auto& data = c->get().animation_data(); auto* a = data.find_animation(aid);
    if (!a) return false;
    const auto* base = data.find_pose(a->base_pose);
    if (!base || !sm::pose_compatible(*base,project_->topology(),c->get().rig().skeleton_ids())) {
        QMessageBox::warning(this,"Cannot open animation","The base pose no longer matches the rig. Update the pose before opening this animation."); return false;
    }
    if (a->duration() != 0) {
        QMessageBox::information(this,"Animation preview","This stage supports opening empty animations only."); return false;
    }
    auto working = std::make_unique<sm::topology>();
    for (auto s : c->get().rig().skeletons()) if (!s->copy_to(*working)) return false;
    for (auto s : working->skeletons()) {
        s->clear_user_data(); for (auto n : s->nodes()) n->clear_user_data(); for (auto b : s->bones()) b->clear_user_data();
    }
    sm::apply_pose(*base,*working);
    auto* window = qobject_cast<ui::stick_man*>(parentWidget());
    if (window) window->tool_mgr().set_current_tool(*canvases_,tool::id::pan);
    canvases_->active_canvas().cancel_bone_pick();
    canvases_->active_canvas().artwork().cancel_transform();
    working_ = std::move(working); active_character_ = cid; active_animation_ = aid;
    canvases_->show_animation_preview(working_.get());
    project_->set_animation_mode(true);
    auto lock = [this](QWidget* w) { enabled_before_.emplace_back(w,w->isEnabled()); w->setEnabled(false); };
    auto* main = qobject_cast<QMainWindow*>(parentWidget());
    lock(canvases_); lock(main->menuBar());
    for (auto* dock : main->findChildren<QDockWidget*>()) if (dock != timeline_pane_) lock(dock);
    for (auto* toolbar : main->findChildren<QToolBar*>()) lock(toolbar);
    banner_label_->setText(QString("Animation Mode — %1 / %2").arg(QString::fromStdString(c->get().name()),QString::fromStdString(a->name)));
    banner_->show(); timeline_->set_head_time(0); timeline_->set_visible_range(0,5000); timeline_pane_->show();
    refresh();
    if (auto* item = find_asset(aid)) { tree_->setCurrentItem(item); tree_->scrollToItem(item); }
    return true;
}
void ui::pane::animation::leave_animation() {
    if (!working_) return;
    // Rebuild project-backed items before destroying their preview counterparts.
    canvases_->show_animation_preview(nullptr);
    working_.reset(); active_character_ = {}; active_animation_ = {};
    project_->set_animation_mode(false);
    for (auto& [widget, enabled] : enabled_before_) if (widget) widget->setEnabled(enabled);
    enabled_before_.clear(); banner_->hide(); timeline_pane_->hide();
    if (auto* window = qobject_cast<ui::stick_man*>(parentWidget())) window->tool_mgr().set_current_tool(*canvases_,tool::id::selection);
    refresh();
}
