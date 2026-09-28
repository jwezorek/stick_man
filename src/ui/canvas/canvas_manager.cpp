#include "artwork_layer.hpp"
#include "canvas_manager.hpp"
#include "canvas_item.hpp"
#include "skel_item.hpp"
#include <ranges>
#include <stdexcept>

namespace r = std::ranges;
namespace rv = std::ranges::views;

namespace {
    ui::canvas::item::skeleton* skeleton_item_by_id(ui::canvas::scene& canv, const sm::object_id& id) {
        auto skels = canv.skeleton_items();
        auto iter = r::find_if(skels,
            [&id](auto* skel)->bool {
                return id == skel->model().id();
            }
        );
        if (iter == skels.end()) {
            return nullptr;
        }
        return *iter;
    }
}

/*------------------------------------------------------------------------------------------------*/
ui::canvas::manager::manager(tool::input_handler& inp_handler) :
    inp_handler_(inp_handler),
    drag_mode_(drag_mode::none) {
    auto* view = new QGraphicsView();
    view->setRenderHint(QPainter::Antialiasing, true);
    view->setViewportUpdateMode(QGraphicsView::FullViewportUpdate);
    view->scale(1, -1);
    addTab(view, "untitled");
    tabBar()->hide();

    auto* canv = new ui::canvas::scene(inp_handler_);
    view->setScene(canv);
    canv->init();
    canv->set_drag_mode(drag_mode_);
    center_active_view();
}
void ui::canvas::manager::init(mdl::project& proj) {
    project_ = &proj;
    connect(&proj, &mdl::project::project_changed, this, [this](mdl::project& model) {
        // The animation canvas is backed by a detached working topology.  Do not
        // touch its items in response to ordinary project notifications; the
        // animation-specific refresh_canvas signal owns those updates.  This is
        // also important while the detached topology is being torn down.
        if (animation_session_active_ || model.animation_mode()) return;
        active_canvas().sync_to_model();
        emit canvas_refresh(model.core());
        active_canvas().sync_selection();
    });
    connect(&proj, &mdl::project::select_character, this, [this](sm::object_id id) {
        if (auto* item = active_canvas().character_item(id)) active_canvas().set_selection(item, true);
    });
    connect(&proj, &mdl::project::pre_new_bone_added, this, &manager::prepare_to_add_bone);
    connect(&proj, &mdl::project::new_bone_added, this, &manager::add_new_bone);
    connect(&proj, &mdl::project::new_skeleton_added, this, &manager::add_new_skeleton);
    connect(&proj, &mdl::project::new_project_opened, this, &manager::set_contents);
    connect(&proj, &mdl::project::refresh_canvas,
        [this](mdl::project& model, bool clear) {
            if (clear) {
                set_contents(model);
            }
            else {
                active_canvas().sync_to_model();
            }
        }
    );
    // Browser notifications must see the rebuilt scene after topology changes.
    active_canvas().init_artwork(proj);

    // Bind the initial (normally empty/untitled) project to the scene as well.
    // Previously scene::model_ was only assigned by set_contents(), which was
    // reached on Open/New but not on ordinary application startup.  Constraint
    // selection/adornments therefore had no model in a freshly started editor.
    set_contents(proj);
}
void ui::canvas::manager::clear() {
    active_canvas().clear();
}
void ui::canvas::manager::prepare_to_add_bone(sm::node& u, sm::node& v) {
    auto& canv = active_canvas();
    auto* deletee = skeleton_item_by_id(canv, v.owner().id());
    if (deletee) {
        canv.delete_item(deletee, false);
    }
}

void ui::canvas::manager::add_new_bone(sm::bone& bone) {
    auto& canv = active_canvas();
    canv.insert_item(bone);
    canv.sync_to_model();

    emit canvas_refresh(project_->core());
}
void ui::canvas::manager::add_new_skeleton(sm::skel_ref skel_ref) {
    auto& canv = active_canvas();
    auto& skel = skel_ref.get();
    canv.insert_item(skel.root_node());
    canv.insert_item(skel);

    emit canvas_refresh(project_->core());
}

QGraphicsView& ui::canvas::manager::active_view() const {
    return *static_cast<QGraphicsView*>(widget(0));
}
ui::canvas::scene& ui::canvas::manager::active_canvas() const {
    return *static_cast<ui::canvas::scene*>(active_view().scene());
}

void ui::canvas::manager::center_active_view() {
    active_view().centerOn(0, 0);
}

void ui::canvas::manager::set_canvas_name(const std::string& name) {
    setTabText(0, QString::fromStdString(name));
}

void ui::canvas::manager::set_contents(mdl::project& model) {
    active_canvas().set_contents(model);
    active_canvas().sync_to_model();

    // canvas_refresh drives the normal project panes (notably the Skeleton pane),
    // which are backed by the persistent sm::project and expect the canvas to
    // contain the corresponding persistent character/skeleton items.  During an
    // Animation Mode session the canvas instead contains a detached working
    // topology, so publishing a normal project refresh here mixes the two edit
    // contexts and can make those panes dereference missing/stale canvas items.
    if (!model.animation_mode())
        emit canvas_refresh(model.core());

    active_canvas().sync_selection();
}

void ui::canvas::manager::set_drag_mode(drag_mode dm) {
    drag_mode_ = dm;
    active_canvas().set_drag_mode(dm);
}

void ui::canvas::manager::show_animation_session(bool active) {
    animation_session_active_ = active;
    auto& scene = active_canvas();
    scene.artwork().set_preview_topology(active ? &project_->topology() : nullptr);
    set_contents(*project_);
}

void ui::canvas::manager::detach_animation_session_view() {
    if (!animation_session_active_) return;

    // Canvas items in Animation Mode directly reference the session's detached
    // working topology, and the artwork layer may also hold that topology as its
    // preview source.  Drop every such reference before mdl::project destroys
    // the animation session.  Keep animation_session_active_ true until the
    // persistent project view is installed by show_animation_session(false).
    auto& scene = active_canvas();
    scene.artwork().set_preview_topology(nullptr);
    scene.clear();
}

