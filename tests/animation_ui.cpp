#include "ui/stick_man.hpp"
#include "ui/panes/animation_pane.hpp"
#include "ui/widgets/timeline.hpp"
#include "ui/canvas/canvas_manager.hpp"
#include "ui/canvas/node_item.hpp"
#include <QtWidgets>
#include <iostream>
#include <stdexcept>
void require(bool ok, const char* msg) { if (!ok) throw std::runtime_error(msg); }
int main(int argc, char** argv) {
    QApplication app(argc, argv);
    const auto font_id = QFontDatabase::addApplicationFont("C:/Windows/Fonts/segoeui.ttf");
    if (font_id >= 0) app.setFont(QFont(QFontDatabase::applicationFontFamilies(font_id).front(),10));
    try {
        ui::stick_man window;
        window.resize(1200,800); window.show(); app.processEvents();
        auto& p = window.project();
        p.add_new_skeleton_root({10, 20});
        auto s = *p.topology().skeletons().begin();
        std::vector<sm::const_skel_ref> members{s};
        auto cid = p.make_character(members).value();
        auto* browser = window.findChild<ui::pane::animation*>();
        require(browser, "animation browser exists");
        auto* tree = browser->findChild<QTreeWidget*>();
        require(tree && tree->topLevelItemCount() == 1, "browser shows characters");
        p.transform_node_positions({{s->root_node().id(),{10,20}}},{{s->root_node().id(),{45,60}}});
        sm::animation a; a.name = "Empty"; a.base_pose = p.core().animation_data(cid).default_pose;
        p.edit_animation_data(cid, [&](auto& data) { data.animations.push_back(a); });
        require(tree->topLevelItem(0)->child(1)->childCount() == 1, "browser refreshes assets");
        require(browser->open_animation(cid, a.id), "open empty animation");
        app.processEvents(); window.grab().save("out/animation-mode.png");
        require(p.animation_mode(), "mode is active");
        require(window.canvases().active_canvas().node_items()[0]->model().world_x() == 10, "preview uses base pose");
        require(&window.canvases().active_canvas().node_items()[0]->model() != &s->root_node(), "preview is detached topology");
        p.add_new_skeleton_root({99, 99});
        require(std::ranges::distance(p.topology().skeletons()) == 1, "mode blocks topology changes");
        p.undo();
        require(p.core().animation_data(cid).animations.size() == 1, "undo locked during stub session");
        browser->leave_animation();
        require(!p.animation_mode(), "leave mode");
        require(p.topology().get<sm::node>(s->root_node().id())->get().world_x() == 45, "preview never changes project");
        p.undo();
        require(p.core().animation_data(cid).animations.empty(), "animation creation is undoable");
        p.redo();
        require(p.core().animation_data(cid).animations.size() == 1, "animation creation is redoable");
        p.apply_pose(cid,a.base_pose);
        require(s->root_node().world_x() == 10, "Apply Pose restores stored positions");
        p.undo(); require(s->root_node().world_x() == 45, "Apply Pose is undoable");
        tree->setCurrentItem(tree->topLevelItem(0));
        browser->create_pose();
        require(p.core().animation_data(cid).poses.size() == 2, "Pose button captures a named pose");
        require(p.core().animation_data(cid).poses.back().node_positions.at(s->root_node().id()).x == 45, "named pose captures current geometry");
        tree->closePersistentEditor(tree->currentItem());
        tree->currentItem()->setText(0,"Standing"); app.processEvents();
        require(p.core().animation_data(cid).poses.back().name == "Standing", "inline rename updates model");
        ui::timeline t;
        t.resize(640, 240); t.set_rows(3); t.set_visible_range(0, 2000);
        t.set_snap_interval(10); t.set_snap_enabled(true);
        t.set_items({{"a", 100, 500, 1, "Example", ui::timeline_color::blue}});
        require(t.time_at(t.x_at(500)) == 500, "timeline time mapping");
        t.set_head_time(750);
        require(t.head_time() == 750, "timeline uses caller time");
        t.show(); app.processEvents();
        auto mouse = [&](QEvent::Type type, QPointF pos, Qt::MouseButton button, Qt::MouseButtons buttons) {
            QMouseEvent e(type,pos,pos,button,buttons,Qt::NoModifier);
            QApplication::sendEvent(t.viewport(), &e);
        };
        t.set_snap_interval(1);
        mouse(QEvent::MouseButtonPress,{t.x_at(500),10},Qt::LeftButton,Qt::LeftButton);
        mouse(QEvent::MouseButtonRelease,{t.x_at(500),10},Qt::LeftButton,Qt::NoButton);
        require(t.head_time() == 500, "1 ms snapping must not advance the head");
        qint64 requested = -1; ui::row_head_position requested_row;
        QObject::connect(&t,&ui::timeline::itemMoveRequested,[&](QString id,qint64 start,ui::row_head_position row) { require(id == "a", "move identity"); requested = start; requested_row = row; });
        mouse(QEvent::MouseButtonPress,{t.x_at(300),84},Qt::LeftButton,Qt::LeftButton);
        mouse(QEvent::MouseMove,{t.x_at(600),120},Qt::NoButton,Qt::LeftButton);
        mouse(QEvent::MouseButtonRelease,{t.x_at(600),120},Qt::LeftButton,Qt::NoButton);
        require(requested == 400 && requested_row.index == 2, "drag requests new time and row");
        require(t.items()[0].start == 100, "widget does not mutate caller items");
        requested = -1;
        t.set_drop_validator([](auto,auto,auto,auto) { return false; });
        mouse(QEvent::MouseButtonPress,{t.x_at(300),84},Qt::LeftButton,Qt::LeftButton);
        mouse(QEvent::MouseButtonRelease,{t.x_at(600),120},Qt::LeftButton,Qt::NoButton);
        require(requested == -1, "invalid drops do not emit edits");
        t.set_drop_validator({});
        qint64 resized_start = -1, resized_end = -1;
        QObject::connect(&t,&ui::timeline::itemResizeRequested,[&](QString,qint64 start,qint64 end) { resized_start=start; resized_end=end; });
        mouse(QEvent::MouseButtonPress,{t.x_at(100),84},Qt::LeftButton,Qt::LeftButton);
        mouse(QEvent::MouseButtonRelease,{t.x_at(150),84},Qt::LeftButton,Qt::NoButton);
        require(resized_start == 150 && resized_end == 600, "left resize keeps end fixed");
        mouse(QEvent::MouseButtonPress,{t.x_at(600),84},Qt::LeftButton,Qt::LeftButton);
        mouse(QEvent::MouseButtonRelease,{t.x_at(800),84},Qt::LeftButton,Qt::NoButton);
        require(resized_start == 100 && resized_end == 800, "right resize keeps start fixed");
        t.set_snap_interval(5);
        mouse(QEvent::MouseButtonPress,{t.x_at(502),10},Qt::LeftButton,Qt::LeftButton);
        mouse(QEvent::MouseButtonRelease,{t.x_at(502),10},Qt::LeftButton,Qt::NoButton);
        require(t.head_time() == 500, "odd snap interval rounds to nearest time");
        QWheelEvent zoom({350,10},{350,10},{0,0},{0,120},Qt::NoButton,Qt::ControlModifier,Qt::NoScrollPhase,false);
        QApplication::sendEvent(t.viewport(), &zoom);
        t.horizontalScrollBar()->setValue(0);
        require(t.time_at(76) == 0, "zoom retains navigation to time zero");
        bool refreshed = false;
        QObject::connect(&t,&ui::timeline::itemSelected,[&](QString id) {
            if (!id.isEmpty()) { refreshed = true; t.set_items({}); }
        });
        mouse(QEvent::MouseButtonPress,{t.x_at(300),84},Qt::LeftButton,Qt::LeftButton);
        mouse(QEvent::MouseButtonRelease,{t.x_at(300),84},Qt::LeftButton,Qt::NoButton);
        require(refreshed && t.items().empty(), "owner may refresh items synchronously on selection");
        t.set_items({{"a",100,500,1,"Rotate arm",ui::timeline_color::blue},
            {"b",500,800,2,"Move hand",ui::timeline_color::teal,true},
            {"c",0,250,0,"Invalid action",ui::timeline_color::orange,false,false,true}});
        QPixmap rendered(t.size()); t.render(&rendered);
        require(!rendered.isNull(), "timeline paints");
        rendered.save("out/animation-timeline.png");
        std::cout << "animation UI passed\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
