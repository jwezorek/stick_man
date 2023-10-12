#pragma once

#include <QWidget>
#include <QtWidgets>
#include <string>
#include <vector>
#include <unordered_map>
#include <span>
#include <string_view>
#include "../core/sm_skeleton.h"

namespace ui {

    class canvas_manager;

    class project : public QObject {

        Q_OBJECT

        std::unordered_map<std::string, std::vector<std::string>> tabs_;
        sm::world world_;

    public:
        project();

        const sm::world& world() const;
        sm::world& world();
        void clear();

        auto tabs() const {
            namespace rv = std::ranges::views;
            return tabs_ | rv::transform([](auto&& p) {return p.first; });
        }

        std::span<const std::string> skel_names_on_tab(std::string_view name) const;

        auto skeletons_on_tab(std::string_view name) const {
            namespace rv = std::ranges::views;
            return skel_names_on_tab(name) |
                rv::transform(
                    [this](const std::string& str)->sm::const_skel_ref {
                        auto s = world_.skeleton(str);
                        return s->get();
                    }
            );
        }

        auto skeletons_on_tab(std::string_view name) {
            namespace rv = std::ranges::views;
            auto const_this = const_cast<const project*>(this);
            return const_this->skeletons_on_tab(name) |
                rv::transform(
                    [](sm::const_skel_ref s)->sm::skel_ref {
                        return const_cast<sm::skeleton&>(s.get());
                    }
                );
        }

        std::string to_json() const;
        void from_json(const std::string& str);

    signals:

        void contents_changed(project& model);
    };

}