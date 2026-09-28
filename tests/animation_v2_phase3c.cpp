#include "ui/stick_man.hpp"
#include "ui/canvas/canvas_manager.hpp"
#include "ui/canvas/node_item.hpp"
#include "ui/panes/animation_editor.hpp"
#include "ui/panes/animation_pane.hpp"
#include "ui/animation_playback.hpp"
#include "ui/widgets/pose_strip.hpp"
#include "ui/canvas/artwork_layer.hpp"
#include "core/sm_constraint_geometry.hpp"
#include "json.hpp"
#include <QtWidgets>
#include <cmath>
#include <iostream>
#include <numbers>
#include <stdexcept>
#include <cstdlib>
#include <new>

// A one-shot allocator fault exercises display reconstruction without adding
// fault-injection APIs to the model. The sampler's scratch topology is on stack.
namespace { bool fail_preview_allocation = false; }
void* operator new(std::size_t size) {
    if (fail_preview_allocation && size == sizeof(sm::topology)) {
        fail_preview_allocation = false;
        throw std::bad_alloc();
    }
    if (auto* memory = std::malloc(size ? size : 1)) return memory;
    throw std::bad_alloc();
}
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }

namespace {
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
void near(double a, double b) {
    if (std::abs(a-b) >= 1e-6) throw std::runtime_error("coordinate: " + std::to_string(a) + " expected " + std::to_string(b));
}
sm::point displayed(ui::canvas::scene& scene, sm::object_id id) {
    for (auto* n : scene.node_items()) if (n->model().id() == id) return n->model().world_pos();
    throw std::runtime_error("display node missing");
}
void endpoints() {
    ui::stick_man window;
    auto& p = window.project();
    auto& core = p.core();
    sm::node_ref r = core.create_skeleton({0, 0}).root_node();
    sm::node_ref t = core.create_skeleton({10, 0}).root_node();
    auto b = core.create_bone("bone", r, t).value();
    t->set_world_pos({20, 0}); // rest length 10, effective length 20, scale 2
    const auto rid = r->id(), tid = t->id();
    std::vector<sm::const_skel_ref> rig{r->owner()};
    auto cid = p.make_character(rig).value();
    sm::animation a;
    a.name = "preview";
    a.keyframes.resize(3);
    for (auto& k : a.keyframes) k.pose = sm::capture_skeletal_pose(p.topology(), std::vector<sm::object_id>{r->owner().id()});
    a.keyframes[0].pose.bone_rotations[b->id()] = 8*std::numbers::pi;
    a.keyframes[1].pose.root_positions[rid] = {20, 0};
    a.keyframes[1].pose.bone_rotations[b->id()] = std::numbers::pi/2;
    a.keyframes[2].pose.root_positions[rid] = {40, 0};
    a.reconcile_transitions();
    for (auto& tr : a.transitions) tr.duration_seconds = 2;
    p.edit_animation_data(cid, [&](auto& data) { data.animations.push_back(a); });
    p.mark_saved();
    auto* browser = window.findChild<ui::pane::animation*>();
    require(browser->open_animation(cid, a.id), "open");
    auto* editor = window.findChild<ui::pane::animation_editor*>();
    auto* clock = editor->findChild<ui::animation_playback*>();
    auto* strip = editor->findChild<ui::pose_strip*>();
    auto& scene = window.canvases().active_canvas();
    const auto editing = p.topology().to_json(), persistent = core.topology().to_json();
    near(displayed(scene, tid).x, 20);
    editor->findChild<QToolButton*>("animation_transport_end")->click();
    near(displayed(scene, tid).x, 60);
    near(p.display_topology().get<sm::bone>(b->id())->get().length(), 10);
    near(p.display_topology().get<sm::bone>(b->id())->get().scale(), 2);
    require(p.topology().to_json() == editing, "preview mutated editing topology");
    require(core.topology().to_json() == persistent, "preview mutated persistent topology");
    require(p.animation_session_keyframe() == a.keyframes[0].id, "preview selected a key");
    require(!p.can_undo() && !p.is_dirty(), "preview created history");
    const auto assets = sm::animation_assets_to_json(core.animation_data(cid));
    const auto thumb = strip->render_preview(a.keyframes[0].id);
    const auto payload = p.serialize();
    int captures = 0;
    auto capture_connection = QObject::connect(&p, &mdl::project::animation_preview_changed, &p, [&] { ++captures; });
    auto* selected_item = scene.node_items().front();
    p.exit_animation_preview();
    for (auto* node : scene.node_items()) if (node->model().id() == tid) selected_item = node;
    scene.set_selection(selected_item, true);
    p.set_show_previous_pose(true);
    editor->preview_time(1);
    near(displayed(scene, rid).x, 10);
    near(displayed(scene, tid).x, 10 + 20/std::sqrt(2.0));
    near(displayed(scene, tid).y, 20/std::sqrt(2.0));
    require(scene.selection().empty(), "preview kept editable selection");
    const auto at_one = p.display_topology().to_json();
    editor->preview_time(3);
    editor->preview_time(1);
    require(p.display_topology().to_json() == at_one, "preview depends on sample history");
    require(strip->render_preview(a.keyframes[0].id).cacheKey() == thumb.cacheKey(), "preview invalidated thumbnails");
    require(p.show_previous_pose(), "preview lost ghost preference");
    p.transform(std::vector<mdl::handle>{rid}, [](sm::node& n) { n.set_world_pos({500,500}); });
    p.transform(std::vector<mdl::handle>{b->id()}, [](sm::bone& b) { b.set_length(100); });
    p.transform_node_positions({{rid,{0,0}}}, {{rid,{500,500}}});
    QGraphicsSceneMouseEvent press(QEvent::GraphicsSceneMousePress);
    press.setScenePos(QPointF(10, 0)); press.setButton(Qt::LeftButton); press.setButtons(Qt::LeftButton);
    window.tool_mgr().mousePressEvent(scene, &press);
    QGraphicsSceneMouseEvent release(QEvent::GraphicsSceneMouseRelease);
    release.setScenePos(QPointF(300, 300)); release.setButton(Qt::LeftButton);
    window.tool_mgr().mouseReleaseEvent(scene, &release);
    require(p.display_topology().to_json() == at_one && p.topology().to_json() == editing,
        "gesture/property path edited preview or editing geometry");
    for (auto [time, x, y] : std::vector<std::tuple<double,double,double>>{{0,20,0},{2,20,20},{4,60,0}}) {
        editor->preview_time(time);
        near(displayed(scene, tid).x, x); near(displayed(scene, tid).y, y);
    }
    require(sm::animation_assets_to_json(core.animation_data(cid)) == assets, "preview rewrote stored scalars");
    require(p.serialize() == payload, "preview serialized transient geometry");
    require(captures == 0, "playback triggered authoring pose refresh");
    QObject::disconnect(capture_connection);
    fail_preview_allocation = true;
    editor->preview_time(1);
    require(!fail_preview_allocation && p.preview_status() == mdl::animation_display_status::reconstruction_failed,
        "reconstruction failure status missing");
    require(!p.animation_preview_active() && !clock->playing() && p.topology().to_json() == editing,
        "reconstruction partially published geometry");
    require(p.display_topology().to_json() == editing && !p.can_undo() && !p.is_dirty(),
        "reconstruction failure changed source/history");
    p.exit_animation_preview();
    require(scene.selected_nodes().size() == 1 && scene.selected_nodes().front()->model().id() == tid,
        "preview lost editing canvas selection");
    near(displayed(scene, tid).x, 20);
    editor->findChild<QToolButton*>("animation_transport_play")->click();
    require(clock->playing() && p.animation_preview_active(), "Play did not evaluate immediately");
    clock->pause();
    require(p.animation_preview_active(), "Pause exited preview");
    const auto held = p.display_topology().to_json();
    require(!clock->playing(), "Pause did not stop clock");
    editor->findChild<QToolButton*>("animation_transport_play")->click();
    require(clock->playing(), "resume failed");
    strip->keyframe_selected(a.keyframes[1].id);
    require(!clock->playing() && !p.animation_preview_active(), "card did not exit preview");
    near(displayed(scene, tid).x, 20); near(displayed(scene, tid).y, 20);
    const double preserved_time = clock->time();
    editor->preview_time(1);
    p.rename_animation_keyframe(std::string("middle"));
    require(!p.animation_preview_active() && clock->time() == preserved_time, "rename did not preserve clock/editing flow");
    editor->preview_time(1);
    p.duplicate_animation_keyframe();
    auto* stored = core.animation_data(cid).find_animation(a.id);
    require(stored->keyframes[2].pose.root_positions.at(rid) == sm::point{20,0}, "Duplicate captured preview");
    require(stored->keyframes[2].pose.bone_rotations.at(b->id()) == std::numbers::pi/2, "Duplicate used preview angle");
    require(clock->time() == 0 && !p.animation_preview_active(), "insertion did not reset preview");
    editor->preview_time(1); p.undo();
    require(!p.animation_preview_active() && stored->keyframes.size() == 3, "undo preview isolation");
    editor->preview_time(1); p.redo();
    require(!p.animation_preview_active() && stored->keyframes.size() == 4, "redo preview isolation");
    editor->preview_time(1); p.delete_animation_keyframe();
    require(!p.animation_preview_active() && stored->keyframes.size() == 3, "Delete preview isolation");
    editor->preview_time(1); p.add_animation_keyframe();
    require(stored->keyframes.back().pose.root_positions.at(rid) == sm::point{40,0}, "Add captured preview");
    p.undo(); p.undo(); p.undo(); p.undo(); // add, delete, duplicate, rename
    require(!p.can_undo() && !p.is_dirty(), "undo failed to restore original history");
    editor->preview_time(1);
    browser->leave_animation();
    require(!p.is_dirty(), "playback committed an edit");
    require(p.serialize() == payload, "session end serialized preview");
    require(browser->open_animation(cid, a.id), "reenter");
    require(!p.animation_preview_active() && !clock->playing() && clock->time() == 0, "reentry preview state");
    // A real authoring edit preceding preview must still commit at session end.
    p.rename_animation_keyframe(std::string("authored before playback"));
    editor->preview_time(1);
    browser->leave_animation();
    require(p.is_dirty(), "preview discarded a prior authored edit");
    p.undo();
    require(!p.is_dirty(), "authored session did not commit as one document edit");
    require(browser->open_animation(cid,a.id), "reopen before replacement");
    editor->preview_time(1);
    auto replacement = p.serialize();
    require(replacement.has_value(), "replacement archive");
    require(p.deserialize_result(*replacement) == sm::project_result::success, "project replacement");
    require(!p.animation_preview_active() && !clock->playing(), "replacement retained preview");
    require(browser->open_animation(cid,a.id), "reopen before new document");
    editor->preview_time(1);
    p.new_document();
    require(!p.animation_mode() && !p.animation_preview_active() && !clock->playing(), "new project left preview callbacks");
    QApplication::processEvents();
}

void playback_uses_outgoing_keyframe_pins() {
    ui::stick_man window;
    auto& p = window.project();
    auto& core = p.core();
    auto root = core.create_skeleton({0, 0}).root_node();
    auto tip = core.create_skeleton({10, 0}).root_node();
    auto bone = core.create_bone("bone", root, tip).value();
    const auto rid = root->id(), tid = tip->id();
    auto cid = p.make_character(std::vector<sm::const_skel_ref>{root->owner()}).value();
    const auto rig = core.character(cid)->get().rig().skeleton_ids();

    sm::animation a;
    a.name = "pins";
    a.keyframes.resize(3);
    for (auto& key : a.keyframes) key.pose = sm::capture_skeletal_pose(core.topology(), rig);
    a.keyframes[0].pinned_nodes.insert(tid);
    a.keyframes[1].pose.root_positions[rid] = {10, 0};
    a.keyframes[1].pose.bone_rotations[bone->id()] = std::numbers::pi / 2;
    a.keyframes[2].pose.root_positions[rid] = {20, 0};
    a.keyframes[2].pose.bone_rotations[bone->id()] = 0;
    a.reconcile_transitions();
    for (auto& transition : a.transitions) transition.duration_seconds = 2;
    p.edit_animation_data(cid, [&](auto& data) { data.animations.push_back(a); });
    p.mark_saved();

    auto* browser = window.findChild<ui::pane::animation*>();
    require(browser->open_animation(cid, a.id), "open pin animation");
    auto* editor = window.findChild<ui::pane::animation_editor*>();
    auto& scene = window.canvases().active_canvas();

    editor->preview_time(1.0);
    require(scene.is_node_pinned(tid), "outgoing source pin was not displayed during transition");
    near(displayed(scene, tid).x, 10.0, 0.006);
    near(displayed(scene, tid).y, 0.0, 0.006);

    editor->preview_time(2.0);
    require(!scene.is_node_pinned(tid), "destination unpin did not take effect at exact keyframe");
    near(displayed(scene, rid).x, 10.0);
    near(displayed(scene, tid).x, 10.0);
    near(displayed(scene, tid).y, 10.0);

    editor->preview_time(3.0);
    require(!scene.is_node_pinned(tid), "destination unpin did not govern the next transition");
    near(displayed(scene, rid).x, 15.0);
    near(displayed(scene, tid).x, 15.0 + 10.0 / std::sqrt(2.0), 1e-6);
    near(displayed(scene, tid).y, 10.0 / std::sqrt(2.0), 1e-6);

    browser->leave_animation();
    require(!p.is_dirty(), "playback-only pin sampling marked project dirty");
}

void constrained_multiroot_artwork_and_failures() {
    constexpr double pi = std::numbers::pi;
    ui::stick_man window;
    auto& p = window.project(); auto& core = p.core();
    sm::node_ref root = core.create_skeleton({0,0}).root_node();
    sm::node_ref x = core.create_skeleton({10,0}).root_node();
    sm::node_ref y = core.create_skeleton({0,10}).root_node();
    auto bx = core.create_bone("x", root, x).value();
    auto by = core.create_bone("y", root, y).value();
    sm::node_ref root2 = core.create_skeleton({100,100}).root_node();
    sm::node_ref tip2 = core.create_skeleton({110,100}).root_node();
    auto b2 = core.create_bone("other", root2, tip2).value();
    require(core.add_rotation_constraint(bx->id(), sm::rotation_reference::world(), {-3*pi/4,3*pi/2}).has_value(), "limit");
    require(core.add_rigid_triangle_constraint(bx->id(), by->id()).has_value(), "triangle");
    auto cid = p.make_character(std::vector<sm::const_skel_ref>{root->owner(),root2->owner()}).value();
    const auto rig = core.character(cid)->get().rig().skeleton_ids();
    sm::animation a; a.name = "projected"; a.keyframes.resize(2);
    for (auto& k : a.keyframes) k.pose = sm::capture_skeletal_pose(core.topology(), rig);
    a.keyframes[0].pose.bone_rotations[bx->id()] = 2*pi/3;
    a.keyframes[0].pose.bone_rotations[by->id()] = 2*pi/3+pi/2;
    a.keyframes[1].pose.bone_rotations[bx->id()] = -2*pi/3;
    a.keyframes[1].pose.bone_rotations[by->id()] = -2*pi/3+pi/2;
    a.keyframes[1].pose.root_positions[root->id()] = {20,10};
    a.keyframes[1].pose.root_positions[root2->id()] = {120,80};
    a.reconcile_transitions(); a.transitions[0].duration_seconds = 2;
    sm::animation empty; empty.name = "empty";
    sm::animation single; single.name = "single"; single.keyframes.push_back(a.keyframes[0]);
    // Keyframe IDs are project-global assets.
    single.keyframes[0].id = sm::object_id::generate();
    p.edit_animation_data(cid, [&](auto& data) { data.animations = {a,empty,single}; });
    std::vector<std::uint8_t> pixels(4*4*4,255);
    auto image = sm::image_resource::from_rgba(4,4,pixels);
    p.edit_artwork(cid, [&](auto& art) {
        art.insert_frame("sprite", {image,{}}); art.add_slot("part", {bx->id()});
        art.add_appearance("Look", {{{"part",{{"default","sprite"}}}}});
    });
    auto& other_root = core.create_skeleton({200,200}).root_node();
    auto other_cid = p.make_character(std::vector<sm::const_skel_ref>{other_root.owner()}).value();
    sm::animation other; other.name = "other character"; other.keyframes.resize(1);
    other.keyframes[0].pose = sm::capture_skeletal_pose(core.topology(), core.character(other_cid)->get().rig().skeleton_ids());
    p.edit_animation_data(other_cid,[&](auto& data) { data.animations.push_back(other); });
    p.mark_saved();
    auto* browser = window.findChild<ui::pane::animation*>();
    require(browser->open_animation(cid,a.id), "open constrained");
    auto* editor = window.findChild<ui::pane::animation_editor*>();
    auto* clock = editor->findChild<ui::animation_playback*>();
    auto& scene = window.canvases().active_canvas();
    auto& layer = scene.artwork(); layer.set_active_appearance(cid,"Look");
    auto* strip = editor->findChild<ui::pose_strip*>();
    const auto thumb = strip->render_preview(a.keyframes[0].id);
    const auto editing = p.topology().to_json(), persistent = core.topology().to_json();
    const auto assets = sm::animation_assets_to_json(core.animation_data(cid));
    editor->preview_time(0.8);
    require(p.preview_status() == mdl::animation_display_status::sampled, "projected preview failed");
    const auto pt = displayed(scene,x->id());
    near(pt.x,8-10/std::sqrt(2.0)); near(pt.y,4+10/std::sqrt(2.0));
    near(displayed(scene,root2->id()).x,108); near(displayed(scene,root2->id()).y,92);
    near(displayed(scene,tip2->id()).x,118);
    near(p.display_topology().get<sm::bone>(bx->id())->get().scaled_length(),10);
    require(sm::constraint_geometry(p.display_topology()).validate() == sm::result::success, "display is infeasible");
    require(layer.hit_test(QPointF(8,4)).has_value(), "static artwork did not follow preview root");
    require(!layer.hit_test(QPointF(0,0)), "static artwork remained on editing pose");
    require(strip->render_preview(a.keyframes[0].id).cacheKey() == thumb.cacheKey(), "artwork thumbnail cache changed");
    p.refresh_canvas(p,false); scene.set_scale(1.5);
    require(p.animation_preview_active(), "render refresh/zoom exited preview");
    p.set_show_previous_pose(true);
    require(p.animation_preview_active(), "ghost preference exited preview");
    require(core.topology().to_json() == persistent && p.topology().to_json() == editing, "constraint preview mutation");
    require(sm::animation_assets_to_json(core.animation_data(cid)) == assets, "constraint preview authored data");

    auto* stored = core.animation_data(cid).find_animation(a.id);
    stored->keyframes[1].pose.bone_rotations[bx->id()] = pi;
    stored->keyframes[1].pose.bone_rotations[by->id()] = 3*pi/2;
    // Explicitly simulate an authoring notification: it invalidates a paused view.
    p.animation_preview_changed();
    require(!p.animation_preview_active(), "pose change failed to invalidate");
    editor->findChild<QToolButton*>("animation_transport_play")->click();
    editor->preview_time(2);
    require(p.preview_status() == mdl::animation_display_status::sampling_failed && p.preview_error().has_value(), "failure status missing");
    require(!clock->playing() && !p.animation_preview_active(), "failure left transport/preview running");
    require(p.topology().to_json() == editing && p.display_topology().to_json() == editing, "failure changed editing view");
    require(!p.can_undo() && !p.is_dirty(), "failure created history");
    auto* status = editor->findChild<QLabel*>("animation_preview_status");
    require(status && status->text().contains("failed"), "nonmodal failure indication missing");
    QApplication::processEvents();
    require(p.preview_status() == mdl::animation_display_status::sampling_failed, "failure retried/cleared automatically");
    *stored = a;
    editor->preview_time(0.8);
    require(!p.preview_error() && !status->text().contains("failed"), "success did not clear failure");
    require(browser->open_animation(cid,empty.id), "switch to empty while paused");
    editor->preview_time(0);
    require(p.preview_status() == mdl::animation_display_status::empty && !clock->playing() && !p.animation_preview_active(), "empty preview");
    require(browser->open_animation(cid,single.id), "switch to single");
    require(!p.animation_preview_active(), "single entered preview implicitly");
    editor->findChild<QToolButton*>("animation_transport_play")->click();
    require(p.animation_preview_active() && !clock->playing(), "single Play did not hold a sample");
    near(displayed(scene,x->id()).x,-5);
    require(browser->open_animation(cid,a.id), "switch from single");
    editor->findChild<QToolButton*>("animation_transport_play")->click();
    require(clock->playing(), "switch fixture did not play");
    require(browser->open_animation(other_cid,other.id), "switch character while playing");
    require(!clock->playing() && !p.animation_preview_active() && scene.node_items().size() == 1,
        "character switch retained preview/items");
    near(displayed(scene,other_root.id()).x,200);
    browser->leave_animation();
    require(!p.is_dirty(), "playback-only session marked dirty");
}

void model_destruction_detaches_views() {
    ui::tool::manager tools;
    ui::canvas::manager canvases(tools);
    ui::pane::animation_editor editor;
    auto model = std::make_unique<mdl::project>();
    canvases.init(*model); tools.init(canvases,*model);
    auto& core = model->core();
    auto& root = core.create_skeleton({0,0}).root_node();
    auto cid = model->make_character(std::vector<sm::const_skel_ref>{root.owner()}).value();
    sm::animation a; a.name = "lifetime"; a.keyframes.resize(2);
    a.keyframes[0].pose = sm::capture_skeletal_pose(core.topology(), core.character(cid)->get().rig().skeleton_ids());
    a.keyframes[1].pose = a.keyframes[0].pose;
    a.reconcile_transitions();
    model->edit_animation_data(cid,[&](auto& data) { data.animations.push_back(a); });
    require(model->begin_animation_session(cid,a.id) == sm::result::success,"lifetime begin");
    canvases.show_animation_session(true); editor.begin(*model,canvases,cid,a.id);
    auto* select_settings = tools.tool_from_id(ui::tool::id::selection).settings_widget();
    auto* constraint_settings = tools.tool_from_id(ui::tool::id::constraint).settings_widget();
    editor.findChild<QToolButton*>("animation_transport_play")->click();
    require(editor.findChild<ui::animation_playback*>()->playing(), "destruction fixture not playing");
    model.reset();
    for (auto* button : select_settings->findChildren<QPushButton*>()) button->click();
    for (auto* combo : constraint_settings->findChildren<QComboBox*>()) combo->setCurrentIndex(1);
    QApplication::processEvents();
    QImage image(100,100,QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent);
    QPainter painter(&image);
    canvases.active_canvas().render(&painter);
    require(canvases.active_canvas().node_items().empty(), "destroyed model retained canvas references");
}
}
int main(int argc, char** argv) {
    QApplication app(argc, argv);
    try { endpoints(); playback_uses_outgoing_keyframe_pins(); constrained_multiroot_artwork_and_failures(); model_destruction_detaches_views(); std::cout << "PASS Animation V2 Phase 3C\n"; return 0; }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}

