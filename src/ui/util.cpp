#include "util.h"
#include <QtWidgets>
#include <sstream>
#include <numbers>

/*------------------------------------------------------------------------------------------------*/

namespace {

    int to_sixteenth_of_deg(double theta) {
        return static_cast<int>(
            theta * ((180.0 * 16.0) / std::numbers::pi)
            );
    }

    class title_bar : public QWidget {
        QLabel* label_;
    public:
        title_bar(const QString& lbl) {
            auto vert = new QVBoxLayout();
            vert->setContentsMargins(0, 0, 0, 0);

            auto horz = new QHBoxLayout();
            horz->setContentsMargins(0, 0, 0, 0);

            auto icon = new QLabel();
            icon->setPixmap(QPixmap(":/images/tool_palette_thumb.png"));
            horz->addWidget(icon);
            horz->addWidget(label_ = new QLabel(lbl));
            horz->addStretch();

            QFrame* line = new QFrame();
            line->setFrameShape(QFrame::HLine);
            line->setFrameShadow(QFrame::Sunken);
            line->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
            QString styleSheet = "QFrame { background-color: #7c7c7c; }";
            line->setStyleSheet(styleSheet);

            vert->addLayout(horz);
            vert->addWidget(line);

            setLayout(vert);
        }

        void set_text(const QString& str) {
            label_->setText(str);
        }
    };

}

/*------------------------------------------------------------------------------------------------*/

ui::FlowLayout::FlowLayout(QWidget* parent, int margin, int hSpacing, int vSpacing)
    : QLayout(parent), m_hSpace(hSpacing), m_vSpace(vSpacing)
{
    setContentsMargins(margin, margin, margin, margin);
}

ui::FlowLayout::FlowLayout(int margin, int hSpacing, int vSpacing)
    : m_hSpace(hSpacing), m_vSpace(vSpacing)
{
    setContentsMargins(margin, margin, margin, margin);
}

ui::FlowLayout::~FlowLayout()
{
    QLayoutItem* item;
    while ((item = takeAt(0)))
        delete item;
}

void ui::FlowLayout::addItem(QLayoutItem* item)
{
    itemList.append(item);
}

int ui::FlowLayout::horizontalSpacing() const
{
    if (m_hSpace >= 0) {
        return m_hSpace;
    } else {
        return smartSpacing(QStyle::PM_LayoutHorizontalSpacing);
    }
}

int ui::FlowLayout::verticalSpacing() const
{
    if (m_vSpace >= 0) {
        return m_vSpace;
    } else {
        return smartSpacing(QStyle::PM_LayoutVerticalSpacing);
    }
}

int ui::FlowLayout::count() const
{
    return itemList.size();
}

QLayoutItem* ui::FlowLayout::itemAt(int index) const
{
    return itemList.value(index);
}

QLayoutItem* ui::FlowLayout::takeAt(int index)
{
    if (index >= 0 && index < itemList.size())
        return itemList.takeAt(index);
    return nullptr;
}

Qt::Orientations ui::FlowLayout::expandingDirections() const
{
    return { };
}

bool ui::FlowLayout::hasHeightForWidth() const
{
    return true;
}

int ui::FlowLayout::heightForWidth(int width) const
{
    int height = doLayout(QRect(0, 0, width, 0), true);
    return height;
}

void ui::FlowLayout::setGeometry(const QRect& rect)
{
    QLayout::setGeometry(rect);
    doLayout(rect, false);
}

QSize ui::FlowLayout::sizeHint() const
{
    return minimumSize();
}

QSize ui::FlowLayout::minimumSize() const
{
    QSize size;
    for (const QLayoutItem* item : qAsConst(itemList))
        size = size.expandedTo(item->minimumSize());

    const QMargins margins = contentsMargins();
    size += QSize(margins.left() + margins.right(), margins.top() + margins.bottom());
    return size;
}

int ui::FlowLayout::doLayout(const QRect& rect, bool testOnly) const
{
    int left, top, right, bottom;
    getContentsMargins(&left, &top, &right, &bottom);
    QRect effectiveRect = rect.adjusted(+left, +top, -right, -bottom);
    int x = effectiveRect.x();
    int y = effectiveRect.y();
    int lineHeight = 0;
    //! [9]

    //! [10]
    for (QLayoutItem* item : qAsConst(itemList)) {
        const QWidget* wid = item->widget();
        int spaceX = horizontalSpacing();
        if (spaceX == -1)
            spaceX = wid->style()->layoutSpacing(
                QSizePolicy::PushButton, QSizePolicy::PushButton, Qt::Horizontal);
        int spaceY = verticalSpacing();
        if (spaceY == -1)
            spaceY = wid->style()->layoutSpacing(
                QSizePolicy::PushButton, QSizePolicy::PushButton, Qt::Vertical);
        //! [10]
        //! [11]
        int nextX = x + item->sizeHint().width() + spaceX;
        if (nextX - spaceX > effectiveRect.right() && lineHeight > 0) {
            x = effectiveRect.x();
            y = y + lineHeight + spaceY;
            nextX = x + item->sizeHint().width() + spaceX;
            lineHeight = 0;
        }

        if (!testOnly)
            item->setGeometry(QRect(QPoint(x, y), item->sizeHint()));

        x = nextX;
        lineHeight = qMax(lineHeight, item->sizeHint().height());
    }
    return y + lineHeight - rect.y() + bottom;
}

int ui::FlowLayout::smartSpacing(QStyle::PixelMetric pm) const
{
    QObject* parent = this->parent();
    if (!parent) {
        return -1;
    } else if (parent->isWidgetType()) {
        QWidget* pw = static_cast<QWidget*>(parent);
        return pw->style()->pixelMetric(pm, nullptr, pw);
    } else {
        return static_cast<QLayout*>(parent)->spacing();
    }
}

/*------------------------------------------------------------------------------------------------*/

double ui::number_edit::to_acceptable_value(double v) const {
    auto validr = static_cast<const QDoubleValidator*>(validator());
    if (v < validr->bottom()) {
        return validr->bottom();
    } else if (v > validr->top()) {
        return validr->top();
    }
    return v;
}

void ui::number_edit::make_acceptable_value() {
    auto old_val = value();
    auto new_val = old_val;
    if (!hasAcceptableInput()) {
        new_val = (old_val) ? to_acceptable_value(*old_val) : default_val_;
    }
    set_value(new_val);
}

void ui::number_edit::handle_done_editing() {
    make_acceptable_value();
    if (value() != old_val_) {
        emit value_changed(*value());
    }
    old_val_ = {};
}

void ui::number_edit::keyPressEvent(QKeyEvent* e) {
    QLineEdit::keyPressEvent(e);
    if (e->key() == Qt::Key_Return) {
        handle_done_editing();
    } 
}

void ui::number_edit::focusInEvent(QFocusEvent* event)
{
    QLineEdit::focusInEvent(event);
    old_val_ = value();
}

void ui::number_edit::focusOutEvent(QFocusEvent* event)
{
    QLineEdit::focusOutEvent(event);
    handle_done_editing();
}

ui::number_edit::number_edit(double val, double default_val, double min, double max, int decimals) :
        default_val_(default_val),
        decimals_(decimals) {
    auto validator = new QDoubleValidator(min, max, decimals, this);
    validator->setNotation(QDoubleValidator::StandardNotation);
    setValidator(validator);
    set_value(val);
    make_acceptable_value();
}

bool ui::number_edit::is_indeterminate() const {
    return text() == "";
}

void ui::number_edit::set_indeterminate() {
    setText("");
}

void ui::number_edit::set_value(double value) {
    value = to_acceptable_value(value);

    std::stringstream ss;
    ss << std::fixed << std::setprecision(decimals_) << value;
    setText(ss.str().c_str());
}

void ui::number_edit::set_value(std::optional<double> v) {
    if (!v) {
        set_indeterminate();
    } else {
        set_value(*v);
    }
}

std::optional<double> ui::number_edit::value() const {
    if (is_indeterminate()) {
        return {};
    }
    return text().toDouble();
}

/*------------------------------------------------------------------------------------------------*/

ui::TabWidget::TabBar::TabBar(QWidget* a_parent) : 
        QTabBar(a_parent) {
    setWidth(size().width());
}

QSize ui::TabWidget::TabBar::tabSizeHint(int index) const
{
    return QSize(_size.width() / (count() ? count() : 1), size().height());
}

void ui::TabWidget::TabBar::setWidth(int a_width)
{
    _size = QSize(a_width, size().height());
    QTabBar::resize(_size);
}

ui::TabWidget::TabWidget(QWidget* a_parent) : 
        _tabBar(new TabBar(this)), 
        QTabWidget(a_parent) {
    setTabBar(_tabBar);
}

void ui::TabWidget::resizeEvent(QResizeEvent* e)  {
    _tabBar->setWidth(size().width());
    QTabWidget::resizeEvent(e);
}

/*------------------------------------------------------------------------------------------------*/

ui::labeled_numeric_val::labeled_numeric_val(QString txt, double val, double min,
        double max, int decimals, bool horz) {
    auto layout = (horz) ?
        static_cast<QLayout*>(new QHBoxLayout(this)) : 
        static_cast<QLayout*>(new QVBoxLayout(this));
    layout->addWidget(new QLabel(txt));
    layout->addWidget(num_edit_ = new number_edit(val, 0, min, max, decimals));
}

ui::number_edit* ui::labeled_numeric_val::num_edit() const {
    return num_edit_;
}

/*------------------------------------------------------------------------------------------------*/

QWidget* ui::custom_title_bar(const QString& lbl) {
    return new title_bar(lbl);
}

void ui::set_custom_title_bar_txt(QDockWidget* pane, const QString& txt) {
    auto* tb = static_cast<title_bar*>(pane->titleBarWidget());
    tb->set_text(txt);
}

void ui::clear_layout(QLayout* layout, bool deleteWidgets)
{
    while (QLayoutItem* item = layout->takeAt(0)) {
        if (deleteWidgets) {
            if (QWidget* widget = item->widget())
                widget->deleteLater();
        }
        if (QLayout* childLayout = item->layout()) {
            clear_layout(childLayout, deleteWidgets);
        }
        delete item;
    }
}

QColor ui::lerp_colors(const QColor& color1, const QColor& color2, double factor) {
    auto r1 = color1.red();
    auto g1 = color1.green();
    auto b1 = color1.blue();
    auto a1 = color1.alpha();

    auto r2 = color2.red();
    auto g2 = color2.green();
    auto b2 = color2.blue();
    auto a2 = color2.alpha();

    int red = std::round(r1 + (r2 - r1) * factor);
    int green = std::round(g1 + (g2 - g1) * factor);
    int blue = std::round(b1 + (b2 - b1) * factor);
    int alpha = std::round(a1 + (a2 - a1) * factor);

    return QColor(red, green, blue, alpha);
}

QRectF ui::rect_from_circle(QPointF center, double radius) {
    auto topleft = center - QPointF(radius, radius);
    return { topleft, QSizeF(2.0 * radius, 2.0 * radius) };
}

void ui::set_arc(
    QGraphicsEllipseItem* gei, QPointF center, double radius,
    double start_theta, double span_theta) {
    gei->setRect(rect_from_circle(center, radius));
    gei->setStartAngle(to_sixteenth_of_deg(-start_theta));
    gei->setSpanAngle(to_sixteenth_of_deg(-span_theta));
}

double ui::radians_to_degrees(double radians) {
	return radians * (180.0 / std::numbers::pi_v<double>);
}

QPointF ui::to_qt_pt(const sm::point& pt) {
	return { pt.x, pt.y };
}

sm::point ui::from_qt_pt(QPointF pt) {
	return { pt.x(), pt.y() };
}

double ui::angle_through_points(QPointF origin, QPointF pt) {
	auto diff = pt - origin;
	return std::atan2(diff.y(), diff.x());
}

double ui::distance(QPointF p1, QPointF p2) {
	return sm::distance(from_qt_pt(p1), from_qt_pt(p2));
}

double ui::normalize_angle(double theta) {
	auto cosine = std::cos(theta);
	auto sine = std::sin(theta);
	return std::atan2(sine, cosine);
}

double ui::clamp_above(double v, double floor) {
	return (v > floor) ? v : floor;
}

double ui::clamp_below(double v, double ceiling) {
	return (v < ceiling) ? v : ceiling;
}

double ui::clamp(double v, double floor, double ceiling) {
	return clamp_below(clamp_above(v, floor), ceiling);

}