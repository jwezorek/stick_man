#pragma once

#include "tool.h"

namespace ui {
    namespace tool {

        class pcnt_validator : public QValidator {

            Q_OBJECT

        public:
            explicit pcnt_validator(QObject* parent = nullptr);

            QValidator::State validate(QString& input, int& pos) const override;
        };

        class zoom_combobox : public QComboBox {
            Q_OBJECT

            static const std::array<double, 9> scales_;

        public:
            zoom_combobox(QWidget* parent = nullptr);
            double nth_scale(int n) const;
            void sync_zoom_text(double scale);
            std::vector<int> magnification_levels() const;

        signals:
            
            void zoom_changed(double scale);

        private:
            void onEditingFinished();
            void onChange(const QString& str);
            
        };

        class zoom : public base {

            QWidget* settings_;
            zoom_combobox* magnify_;
            canvas::manager* canvases_;

            void handleButtonClick(int level);
            

        public:
            zoom();
            void init(canvas::manager& canvases, mdl::project& model) override;
            void mouseReleaseEvent(canvas::scene& c, QGraphicsSceneMouseEvent* event) override;
            virtual QWidget* settings_widget() override; 
            std::vector<int> magnification_levels() const;
            void do_zoom(double scale) const;
        };
    }

}