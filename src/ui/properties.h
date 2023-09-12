#pragma once

#include <QtWidgets>

namespace ui {

	class stick_man;
	class canvas;

	enum class selection_type {
		none = 0,
		node,
		bone,
		skeleton,
		mixed
	};

	class abstract_properties_widget : public QScrollArea {
	protected:
		QVBoxLayout* layout_;
		QLabel* title_;
		stick_man* main_wnd_;
	public:
		abstract_properties_widget(ui::stick_man* mw, QWidget* parent, QString title);
		void set_title(QString title);
		void init();
		virtual void populate();
		ui::canvas& canvas();
		virtual void set_selection(const ui::canvas& canv) = 0;
		virtual void lose_selection();
	};

	class single_or_multi_props_widget : public abstract_properties_widget {
	public:
		single_or_multi_props_widget(ui::stick_man* mw, QWidget* parent);
		void set_selection(const ui::canvas& canv) override;
		virtual void set_selection_single(const ui::canvas& canv) = 0;
		virtual void set_selection_multi(const ui::canvas& canv) = 0;
		virtual bool is_multi(const ui::canvas& canv) = 0;
	};

	class selection_properties : public QStackedWidget {
		stick_man* main_wnd_;
	public:
		selection_properties(stick_man* mnd_wnd);
		abstract_properties_widget* current_props() const;
		void set(const ui::canvas& canv);
		void init();
	};
}