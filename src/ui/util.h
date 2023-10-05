#pragma once

#include "../core/sm_types.h"
#include <QWidget>
#include <QtWidgets>
#include <ranges>
#include <string>
#include <vector>
#include <functional>

/*------------------------------------------------------------------------------------------------*/

namespace ui {

	const QColor k_accent_color = QColor::fromRgb(64, 53, 130);

	QWidget* horz_separator();

    class FlowLayout : public QLayout
    {
    public:
        explicit FlowLayout(QWidget* parent, int margin = -1, int hSpacing = -1, int vSpacing = -1);
        explicit FlowLayout(int margin = -1, int hSpacing = -1, int vSpacing = -1);
        ~FlowLayout();

        void addItem(QLayoutItem* item) override;
        int horizontalSpacing() const;
        int verticalSpacing() const;
        Qt::Orientations expandingDirections() const override;
        bool hasHeightForWidth() const override;
        int heightForWidth(int) const override;
        int count() const override;
        QLayoutItem* itemAt(int index) const override;
        QSize minimumSize() const override;
        void setGeometry(const QRect& rect) override;
        QSize sizeHint() const override;
        QLayoutItem* takeAt(int index) override;

    private:
        int doLayout(const QRect& rect, bool testOnly) const;
        int smartSpacing(QStyle::PixelMetric pm) const;

        QList<QLayoutItem*> itemList;
        int m_hSpace;
        int m_vSpace;
    };

	class hyperlink_button : public QPushButton {
	public:
		hyperlink_button(QString txt);
	};

	using string_edit_validator = std::function<bool(const std::string&)>;
	class string_edit : public QLineEdit {
		Q_OBJECT

		void keyPressEvent(QKeyEvent* e) override;
		void focusInEvent(QFocusEvent* event) override;
		void focusOutEvent(QFocusEvent* event) override;
		void handle_done_editing();

		std::string old_val_;
		string_edit_validator valid_fn_;
	public:
		string_edit(const std::string& = "", string_edit_validator valid_fn = {});
		std::string value() const;
		void set_validator(string_edit_validator fn);
	signals:
		void value_changed(std::string val);
	};

	class labeled_field : public QWidget {
		QLabel* lbl_;
		string_edit* val_;
	public:
		labeled_field(QString lbl, QString val);
		string_edit* value();
		void set_label(QString str);
		void set_value(QString str);
		void set_color(QColor color);
	};

	class labeled_hyperlink : public QWidget {
		QLabel* lbl_;
		hyperlink_button* val_;
	public:
		labeled_hyperlink(QString lbl, QString val);
		hyperlink_button* hyperlink();
	};

    class number_edit : public QLineEdit {

        Q_OBJECT

        void keyPressEvent(QKeyEvent* e) override;
        void focusInEvent(QFocusEvent* event) override;
        void focusOutEvent(QFocusEvent* event) override;
        void handle_done_editing();
        double to_acceptable_value(double v) const;
        void make_acceptable_value();
        static std::string format_string(int decimals);

        double default_val_;
        int  decimals_;
        std::optional<double> old_val_;

    public:
        number_edit(double val, double default_val, double min, double max, int decimals);
        bool is_indeterminate() const;
        void set_indeterminate();
        void set_value(double v);
        void set_value(std::optional<double> v);
        std::optional<double> value() const;

    signals:
        void value_changed(double val);
    };

    class labeled_numeric_val : public QWidget {

        number_edit* num_edit_;

    public:
        labeled_numeric_val(QString txt, double val, 
                double min = 0.0, double max = 1.0, int decimals = 2, 
                bool horz = true);
        number_edit* num_edit() const;
    };

    class TabWidget : public QTabWidget
    {
        class TabBar : public QTabBar
        {
            QSize _size;
        public:
            TabBar(QWidget* a_parent);
            QSize tabSizeHint(int index) const;
            void setWidth(int a_width);
        };
        TabBar* _tabBar;
    public:
        TabWidget(QWidget* parent);
        void resizeEvent(QResizeEvent* e) override;
    };

	struct field_info {
		std::string str;
		double val;
		double min;
		double max;
	};

	class tabbed_values : public TabWidget {

		Q_OBJECT

	private:
		QWidget* primary_tab_;
		std::vector<std::vector<number_edit*>> num_editors_;

		void handle_value_changed(number_edit* num_edit);

	protected:
		virtual double to_nth_tab(int n, int field, double val) = 0;
		virtual double from_nth_tab(int n, int field, double val) = 0;
		void set_value(int tab_index, int field_index, std::optional<double> val);
		std::optional<double> get_tab_zero_value(int tab_index, int field_index);
		std::tuple<int, int> indices_from_editor(const number_edit* num_edit) const;
	public:
		tabbed_values(QWidget* parent, const std::vector<std::string>& tabs,
			const std::vector<field_info>& fields, int hgt);

		int num_tabs() const;
		int num_fields() const;

		std::optional<double> value(int field);
		void set_value(int field, std::optional<double> tab0_val);
		void lock_to_primary_tab();
		void unlock();

	signals:
		void value_changed(int field_index);
	};

    QWidget* custom_title_bar(const QString& lbl);
    void set_custom_title_bar_txt(QDockWidget* pane, const QString& txt);
    void clear_layout(QLayout* layout, bool deleteWidgets = true);

    template<typename T, typename U>
    auto as_range_view_of_type(const U& collection) {
        return collection |
            std::ranges::views::transform(
                [](auto itm)->T* {  return dynamic_cast<T*>(itm); }
            ) | std::ranges::views::filter(
                [](T* ptr)->bool { return ptr;  }
            );
    }

    template<typename T, typename U>
    std::vector<T*> to_vector_of_type(const U& collection) {
        return as_range_view_of_type<T>(collection) | std::ranges::to<std::vector<T*>>();
    }

    QColor lerp_colors(const QColor& color1, const QColor& color2, qreal factor);
    QRectF rect_from_circle(QPointF center, double radius);
    void set_arc(
        QGraphicsEllipseItem* gei, QPointF center, double radius,
        double start_theta, double span_theta
    );
	double radians_to_degrees(double radians);
	double degrees_to_radians(double degrees);
	QPointF to_qt_pt(const sm::point& pt);
	sm::point from_qt_pt(QPointF pt);
	double angle_through_points(QPointF origin, QPointF pt);
	double distance(QPointF p1, QPointF p2);
	double normalize_angle(double theta);
	double clamp_above(double v, double floor);
	double clamp_below(double v, double ceiling);
	double clamp(double v, double floor, double ceiling);
    std::string get_prefixed_string(
        const std::string& prefix, const std::string& str, char separator = '-'
    );
    std::string make_unique_name(const std::vector<std::string>& used_names,
        const std::string& prefix);
    void to_text_file(const std::string& file_path, const std::string& text);
}