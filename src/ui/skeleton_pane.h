#pragma once

#include "canvas.h"
#include <QWidget>
#include <QtWidgets>
#include "../core/sm_types.h"

/*------------------------------------------------------------------------------------------------*/

namespace ui {

	class tool_manager;
	class stick_man;

	class abstract_properties_widget : public QScrollArea {
	protected:
		QVBoxLayout* layout_;
		QLabel* title_;
		stick_man* main_wnd_;
	public:
		abstract_properties_widget(ui::stick_man* mw, QWidget* parent, QString title);
		void init();
		virtual void populate();
		ui::canvas& canvas();
		virtual void set_selection(const ui::canvas& canv) = 0;
		virtual void lose_selection();
	};

	class selection_properties : public QStackedWidget {
	public:
		selection_properties(ui::stick_man* mgr);
		abstract_properties_widget* current_props() const;
		void set(ui::sel_type typ, const ui::canvas& canv);
		void init();
	};

    class skeleton_pane : public QDockWidget {

		stick_man* main_wnd_;
		QTreeView* skeleton_tree_;
		selection_properties* sel_properties_;
		QMetaObject::Connection conn_;
		QMetaObject::Connection canv_sel_conn_;

		void expand_selected_items();

		void connect_tree_sel_handler();
		void disconnect_tree_sel_handler();

		void connect_canv_sel_handler();
		void disconnect_canv_sel_handler();

		void sync_with_model();
		void handle_canv_sel_change();
		void handle_tree_change(QStandardItem* item);
		void handle_tree_selection_change(const QItemSelection&, const QItemSelection&);

		QTreeView* create_skeleton_tree();
		std::vector<QStandardItem*> selected_items() const;
		ui::canvas& canvas();

		void select_item(QStandardItem* item, bool select);
		void select_items(const std::vector<QStandardItem*>& items, bool emit_signal = true);

    public:

        skeleton_pane(ui::stick_man* mgr);
		selection_properties& sel_properties();
		void init();
		bool validate_props_name_change(const std::string& new_name);
		void handle_props_name_change(const std::string& new_name);
    };

}