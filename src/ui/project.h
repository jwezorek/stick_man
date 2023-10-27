#pragma once

#include <QWidget>
#include <QtWidgets>
#include <string>
#include <vector>
#include <unordered_map>
#include <span>
#include <string_view>
#include <memory>
#include <stack>
#include "../core/sm_skeleton.h"
/*------------------------------------------------------------------------------------------------*/

namespace ui {

    class canvas_manager;

    using const_skel_piece =
        std::variant<sm::const_node_ref, sm::const_bone_ref, sm::const_skel_ref>;
    using skel_piece = std::variant<sm::node_ref, sm::bone_ref, sm::skel_ref>;

    using tab_table = std::unordered_map<std::string, std::vector<std::string>>;
    
    class project;
    struct command {
        std::function<void(project&)> redo;
        std::function<void(project&)> undo;
    };

    class project : public QObject {

        friend class commands;

        Q_OBJECT

        tab_table tabs_;
        sm::world world_;

        std::stack<command> redo_stack_;
        std::stack<command> undo_stack_;

        void delete_skeleton_name_from_canvas_table(const std::string& tab, const std::string& skel);
        void clear_redo_stack();
        void execute_command(const command& cmd);
        bool rename_aux(skel_piece piece, const std::string& new_name);

    public:
        project();

        const sm::world& world() const;
        sm::world& world();
        void clear();

        void undo();
        void redo();
        bool can_undo() const;
        bool can_redo() const;

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
        bool rename(skel_piece piece, const std::string& new_name);


        void replace_skeletons(const std::string& canvas_name, 
            const std::vector<std::string>& replacees,
            const std::vector<sm::skel_ref>& replacements,
            bool rename = true);
        std::string canvas_name_from_skeleton(const std::string& skel) const;
        void transform(const std::vector<sm::node_ref>& nodes, 
            const std::function<void(sm::node&)>& fn);
        void transform(const std::vector<sm::bone_ref>& nodes,
            const std::function<void(sm::bone&)>& fn);
    signals:
        void new_tab_added(const std::string& name);
        void pre_new_bone_added(sm::node& u, sm::node& v);
        void new_bone_added(sm::bone& bone);
        void new_project_opened(project& model);
        void new_skeleton_added(sm::skel_ref skel);
        void refresh_canvas(project& model, const std::string& canvas, bool clear);
        void name_changed(skel_piece piece, const std::string& new_name);
        void refresh_undo_redo_state(bool, bool);
    };

    std::string unique_skeleton_name(const std::string& old_name, const std::vector<std::string>& used_names);
    bool identical_pieces(skel_piece p1, skel_piece p2);
}