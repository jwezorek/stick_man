#pragma once

#include <QtWidgets>
#include <unordered_map>
#include <functional>
#include "util.h"
#include "../model/project.h"

/*------------------------------------------------------------------------------------------------*/

namespace ui {

	class stick_man;
    class canvas_manager;
	class canvas;
    class skeleton_pane;
    class project;

	enum class selection_type {
		none = 0,
		node,
		bone,
		skeleton,
		mixed
	};

    using current_canvas_fn = std::function<canvas&()>;

    class selection_properties;

	class abstract_properties_widget : public QWidget {
	protected:
		QVBoxLayout* layout_;
		QLabel* title_;
        current_canvas_fn get_current_canv_;
        selection_properties* parent_;
        mdl::project* proj_;

        void do_property_name_change(const std::string& new_name);
        void handle_rename(mdl::skel_piece p, ui::string_edit* name_edit, const std::string& new_name);

	public:
		abstract_properties_widget(const current_canvas_fn& fn, 
            selection_properties* parent, QString title);
		void set_title(QString title);
		void init(mdl::project& proj);
		virtual void populate(mdl::project& proj);
		virtual void set_selection(const ui::canvas& canv) = 0;
		virtual void lose_selection();
	};

	class single_or_multi_props_widget : public abstract_properties_widget {
	public:
		single_or_multi_props_widget(const current_canvas_fn& fn, selection_properties* parent, QString title);
		void set_selection(const ui::canvas& canv) override;
		virtual void set_selection_common(const ui::canvas& canv) = 0;
		virtual void set_selection_single(const ui::canvas& canv) = 0;
		virtual void set_selection_multi(const ui::canvas& canv) = 0;
		virtual bool is_multi(const ui::canvas& canv) = 0;
	};

	class selection_properties : public QStackedWidget {
		std::unordered_map<selection_type, abstract_properties_widget*> props_;
        skeleton_pane* skel_pane_;

        void handle_selection_changed(canvas& canv);

	public:
		selection_properties(const current_canvas_fn& fn, skeleton_pane* pane);
		abstract_properties_widget* current_props() const;
		void set(const ui::canvas& canv);
		void init(canvas_manager& canvases, mdl::project& proj);
        skeleton_pane& skel_pane();
	};
}