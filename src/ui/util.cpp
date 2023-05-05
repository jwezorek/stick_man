#include "util.h"

/*------------------------------------------------------------------------------------------------*/

namespace {

    class title_bar : public QWidget {
    public:
        title_bar(const QString& lbl) {
            auto vert = new QVBoxLayout();
            vert->setContentsMargins(0, 0, 0, 0);

            auto horz = new QHBoxLayout();
            horz->setContentsMargins(0, 0, 0, 0);

            auto icon = new QLabel();
            icon->setPixmap(QPixmap(":/images/tool_palette_thumb.png"));
            auto label = new QLabel(lbl);
            horz->addWidget(icon);
            horz->addWidget(label);
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
    };

}

QWidget* ui::custom_title_bar(const QString& lbl) {
    return new title_bar(lbl);
}