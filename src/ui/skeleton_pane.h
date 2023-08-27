#pragma once

#include "canvas.h"
#include <QWidget>
#include <QtWidgets>

/*------------------------------------------------------------------------------------------------*/

namespace ui {

	class tool_manager;
	class stick_man;

	class abstract_properties_widget : public QScrollArea {
	private:
		ui::canvas* canv_;
	protected:
		QVBoxLayout* layout_;
		QLabel* title_;
		tool_manager* mgr_;
	public:
		abstract_properties_widget(ui::tool_manager* mgr, QWidget* parent, QString title);
		void init();
		virtual void populate();
		ui::canvas& canvas();
		virtual void set_selection(const ui::canvas& canv) = 0;
		virtual void lose_selection();
	};

	class selection_properties : public QStackedWidget {
	public:
		selection_properties(ui::tool_manager* mgr);
		abstract_properties_widget* current_props() const;
		void set(ui::sel_type typ, const ui::canvas& canv);
		void init();
	};

    class skeleton_pane : public QDockWidget {

		stick_man* main_wnd_;
		QTreeView* skeleton_tree_;
		selection_properties* sel_properties_;

		void sync_with_model();

    public:

        skeleton_pane(QMainWindow* wnd);
		selection_properties& sel_properties();
		void init();
    };

}