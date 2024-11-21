#include "skeleton_pane.h"
#include "main_skeleton.h"
#include "../util.h"
#include "../canvas/skel_item.h"
#include "../canvas/bone_item.h"
#include "../canvas/scene.h"
#include "../canvas/canvas_manager.h"
#include "../tools/tool.h"
#include "../stick_man.h"
#include "../../model/project.h"
#include "../../core/sm_bone.h"
#include "../../core/sm_skeleton.h"
#include "../../core/sm_visit.h"
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
	canvases_(nullptr),
	project_(nullptr),
	QDockWidget(tr(""), mw) {

	setTitleBarWidget(custom_title_bar("skeleton view"));

	// Create a QTabWidget
	QTabWidget* tab_widget = new QTabWidget(this);

	// Create the skeleton pane (with QSplitter)
	main_skel_pane_ = new main_skeleton(mw);

	QWidget* skeleton_tab = new QWidget();
	QVBoxLayout* skeleton_layout = new QVBoxLayout();
	skeleton_layout->addWidget(main_skel_pane_);
	skeleton_tab->setLayout(skeleton_layout);

	// Add panes to the tab widget
	tab_widget->addTab(skeleton_tab, tr("Skeleton"));

	QWidget* animations_tab = new QWidget();
	animations_tab->setLayout(new QVBoxLayout()); // Placeholder layout
	tab_widget->addTab(animations_tab, tr("Animations"));

	QWidget* skins_tab = new QWidget();
	skins_tab->setLayout(new QVBoxLayout()); // Placeholder layout
	tab_widget->addTab(skins_tab, tr("Skins"));

	// Set the tab widget as the main widget of the dock
	setWidget(tab_widget);

	main_skel_pane_->connect_tree_change_handler();
}

ui::pane::selection_properties& ui::pane::skeleton::sel_properties() {
	return main_skel_pane_->sel_properties();
}

void ui::pane::skeleton::init(canvas::manager& canvases, mdl::project& proj) {
	project_ = &proj;
	canvases_ = &canvases;
	main_skel_pane_->init(canvases, proj);
}

bool ui::pane::skeleton::validate_props_name_change(const std::string& new_name) {
	return main_skel_pane_->validate_props_name_change(new_name);
}
