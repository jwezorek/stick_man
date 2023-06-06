#pragma once

#include <QWidget>
#include <QtWidgets>

/*------------------------------------------------------------------------------------------------*/

namespace ui {

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

    QWidget* custom_title_bar(const QString& lbl);
    void set_custom_title_bar_txt(QDockWidget* pane, const QString& txt);
    void clear_layout(QLayout* layout, bool deleteWidgets = true);


}