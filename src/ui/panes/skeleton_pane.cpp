#include "skeleton_pane.hpp"
#include "main_skeleton_pane.hpp"
#include "../canvas/skel_item.hpp"
#include "../canvas/bone_item.hpp"
#include "../canvas/scene.hpp"
#include "../canvas/canvas_manager.hpp"
#include "../tools/tool.hpp"
#include "../stick_man.hpp"
#include "../../model/project.hpp"
#include "../../core/sm_bone.hpp"
#include "../../core/sm_skeleton.hpp"
#include "../../core/sm_visit.hpp"
#include <QIcon>
#include <numbers>
#include <ranges>
#include <unordered_map>
#include <unordered_set>
#include <functional>
#include <variant>
#include <type_traits>
#include <qDebug>
#include <stack>
using namespace std::placeholders;
namespace r = std::ranges;
namespace rv = std::ranges::views;
/*------------------------------------------------------------------------------------------------*/

ui::pane::skeleton::skeleton(ui::stick_man* mw) :
	QDockWidget(tr("Skeleton"), mw) {
	setWindowIcon(QIcon(":/images/add_bone_icon.png"));
	setWidget(main_skel_pane_ = new main_skeleton_pane(this, mw));
}
ui::pane::selection_properties& ui::pane::skeleton::sel_properties() {
	return main_skel_pane_->sel_properties();
}

void ui::pane::skeleton::init(canvas::manager& canvases, mdl::project& proj) {
	main_skel_pane_->init(canvases, proj);
}
bool ui::pane::skeleton::validate_props_name_change(const std::string& new_name) {
	return main_skel_pane_->validate_props_name_change(new_name);
}
