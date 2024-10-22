#include "zoom_tool.h"
#include "../canvas/canvas_manager.h"
#include "../canvas/scene.h"
#include <QRegularExpression>
#include <qdebug.h>

namespace {

}

/*------------------------------------------------------------------------------------------------*/

void ui::tool::zoom::handleButtonClick(int level)
{
}

void ui::tool::zoom::do_zoom(double scale) {
    auto& canv = canvases_->active_canvas();
    canv.set_scale(scale);
}

ui::tool::zoom::zoom() :
    base("zoom", "zoom_icon.png", ui::tool::id::zoom),
    settings_(nullptr) {
}

void ui::tool::zoom::init(canvas::manager& canvases, mdl::project& model) {
    canvases_ = &canvases;
}

void ui::tool::zoom::mouseReleaseEvent(canvas::scene& c, QGraphicsSceneMouseEvent* event) {
    auto zoom = c.closest_zoom_level();
    zoom += (!event->modifiers().testFlag(Qt::AltModifier)) ? 1 : -1;
    auto pt = event->scenePos();
    c.set_zoom_level(zoom, pt);
    magnify_->sync_zoom_text(c.scale());
}

QWidget* ui::tool::zoom::settings_widget() {
    
    if (!settings_) { 
        settings_ = new QWidget();
        auto* flow = new ui::FlowLayout(settings_);
        flow->addWidget(new QLabel("magnify"));
        flow->addWidget(magnify_ = new zoom_combobox());
        magnify_->connect(magnify_, &zoom_combobox::zoom_changed,
            [&](double scale) {
                do_zoom(scale);
            }
        );
    }
    return settings_;
}

const std::array<double, 9> ui::tool::zoom_combobox::scales_ = {
        0.25, 0.4, 0.5, 2.0 / 3.0, 1.0, 1.5, 2.0, 2.5,  4.0
};

/*------------------------------------------------------------------------------------------------*/

ui::tool::zoom_combobox::zoom_combobox(QWidget* parent) : QComboBox(parent) {
    
    for (auto scale : scales_) {
        int val = static_cast<int>(scale * 100.0);
        QString txt = QString::number(val) + "%";
        addItem(txt, scale);
    }

    setEditable(true);
    setCurrentIndex(scales_.size() / 2);
    setInsertPolicy(QComboBox::NoInsert); 

    QLineEdit* lineEdit = this->lineEdit();
    pcnt_validator* validator = new pcnt_validator(this);
    lineEdit->setValidator(validator);

    connect(lineEdit, &QLineEdit::editingFinished, this, &zoom_combobox::onEditingFinished);
    connect(this, &QComboBox::textActivated, this, &zoom_combobox::onChange);
    connect(this, &QComboBox::currentIndexChanged, this, 
        [](int i) {
            qDebug() << "QComboBox::currentIndexChanged";
        }
    );
    connect(this, &QComboBox::currentTextChanged, this,
        [](const QString& str) {
            qDebug() << "QComboBox::currentTextChanged: " << str;
        }
    );
}

double ui::tool::zoom_combobox::nth_scale(int n) const {
    return scales_[n];
}

void ui::tool::zoom_combobox::sync_zoom_text(double scale) {
    for (auto canned_scale : scales_) {
        int canned = static_cast<int>(canned_scale * 100.0);
        int tool_scale = static_cast<int>(scale * 100.0);
        if (tool_scale == canned) {
            lineEdit()->setText(QString::number(canned) + "%");
            return;
        }
        lineEdit()->clear();
    }
}

void ui::tool::zoom_combobox::onEditingFinished() {

   QLineEdit* lineEdit = this->lineEdit();
   QString currentText = lineEdit->text();

   // Check if the text ends with '%' already
   if (!currentText.endsWith("%")) {
       bool ok;
       int value = currentText.toInt(&ok);

       // Ensure value is within the allowed range and append '%'
       if (ok && value >= 1 && value <= 500) {
           lineEdit->setText(QString::number(value) + "%");
       }
   }

   onChange(this->currentText());

}

void ui::tool::zoom_combobox::onChange(const QString& str) {

    auto txt = str.toStdString();
    auto num_str = txt.substr(0, txt.size() - 1);
    double scale = static_cast<double>(std::stoi(num_str)) / 100.0;
    emit zoom_changed(scale);
}

/*------------------------------------------------------------------------------------------------*/

ui::tool::pcnt_validator::pcnt_validator(QObject* parent)
    : QValidator(parent)
{
}

QValidator::State ui::tool::pcnt_validator::validate(QString& input, int& pos) const
{
    Q_UNUSED(pos); // Not using the position for this validator

    // Empty input is acceptable (intermediate state)
    if (input.isEmpty()) {
        return QValidator::Intermediate;
    }

    // Regular expression to match an unsigned integer, optionally followed by '%'
    QRegularExpression regex("^\\d+%?$");

    // Test the input against the regular expression
    QRegularExpressionMatch match = regex.match(input);

    if (match.hasMatch()) {
        // Extract numeric part (remove '%' if present)
        QString numeric_part = input;
        if (input.endsWith('%')) {
            numeric_part.chop(1); // Remove the trailing '%'
        }

        // Convert numeric part to integer
        bool ok;
        int value = numeric_part.toInt(&ok);

        // Ensure the integer is within the range [5, 500]
        if (ok && value >= 5 && value <= 500) {
            return QValidator::Acceptable;
        }
        else {
            return QValidator::Intermediate; // If it's out of range, but matches pattern, it's intermediate
        }
    }

    return QValidator::Invalid; // Doesn't match the required format
}
