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

    using tab_table = std::unordered_map<std::string, std::vector<std::string>>;

    class project : public QObject {

        Q_OBJECT

            tab_table tabs_;
        sm::world world_;

        void delete_skeleton_from_tab(const std::string& tab, const std::string& skel);

    public:
        project();

        const sm::world& world() const;
        sm::world& world();
        void clear();

        auto tabs() const {
            namespace rv = std::ranges::views;
            return tabs_ | rv::transform([](auto&& p) {return p.first; });
        }

        bool has_tab(const std::string& str) const;
        bool add_new_tab(const std::string& name);

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
        bool from_json(const std::string& str);
        void add_bone(const std::string& tab, sm::node& u, sm::node& v);
        void add_new_skeleton_root(const std::string& tab, sm::point loc);
        void replace_skeletons(const std::string& canvas_name, 
            const std::vector<std::string>& replacees,
            const std::vector<sm::skel_ref>& replacements);
        std::string canvas_name_from_skeleton(const std::string& skel) const;

    signals:
        void new_tab_added(const std::string& name);
        void pre_new_bone_added(sm::node& u, sm::node& v);
        void new_bone_added(sm::bone& bone);
        void new_project_opened(project& model);
        void new_skeleton_added(sm::skel_ref skel);
        //void pre_refresh_canvas(const std::string& canvas);
        void refresh_canvas(project& model, const std::string& canvas);
    };

    std::string unique_skeleton_name(const std::string& old_name, const std::vector<std::string>& used_names);
}