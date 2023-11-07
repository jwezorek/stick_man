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
#include "handle.h"

/*------------------------------------------------------------------------------------------------*/

namespace mdl {

    class project;

    struct command {
        std::function<void(project&)> redo;
        std::function<void(project&)> undo;
    };

    class project : public QObject {

        friend class commands;

        Q_OBJECT

        std::unordered_map<std::string, std::vector<std::string>> tabs_;
        sm::world world_;

        std::stack<command> redo_stack_;
        std::stack<command> undo_stack_;

        void delete_skeleton_name_from_canvas_table(const std::string& tab, const std::string& skel);
        void clear_redo_stack();
        void execute_command(const command& cmd);
        void rename_aux(skel_piece piece, const std::string& new_name);
        bool can_rename(skel_piece piece, const std::string& new_name);
        void replace_skeletons_aux(const std::string& canvas_name,
            const std::vector<std::string>& replacees,
            const std::vector<sm::skel_ref>& replacements,
            std::vector<std::string>* new_names_of_replacements);
        void clear();

    public:
        project();

        const sm::world& world() const;        
        bool can_undo() const;
        bool can_redo() const;
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
        bool has_tab(const std::string& str) const;
        std::string to_json() const;
        std::string canvas_name_from_skeleton(const std::string& skel) const;

        void undo();
        void redo();
        sm::world& world();
        bool add_new_tab(const std::string& name);
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
        bool from_json(const std::string& str);
        void add_bone(const std::string& tab, 
            const handle& node_u, const handle& node_v);
        void add_new_skeleton_root(const std::string& tab, sm::point loc);
        bool rename(skel_piece piece, const std::string& new_name);
        void replace_skeletons(const std::string& canvas_name,
            const std::vector<std::string>& replacees,
            const std::vector<sm::skel_ref>& replacements
        );
        void transform(const std::vector<handle>& nodes, 
            const std::function<void(sm::node&)>& fn);
        void transform(const std::vector<handle>& nodes,
            const std::function<void(sm::bone&)>& fn);

    signals:
        void tab_created_or_deleted(const std::string& name, bool created);
        void pre_new_bone_added(sm::node& u, sm::node& v);
        void new_bone_added(sm::bone& bone);
        void new_project_opened(project& model);
        void new_skeleton_added(sm::skel_ref skel);
        void refresh_canvas(project& model, const std::string& canvas, bool clear);
        void name_changed(skel_piece piece, const std::string& new_name);
        void refresh_undo_redo_state(bool, bool);
    };

    std::string unique_skeleton_name(const std::string& old_name, const std::vector<std::string>& used_names);
    bool identical_pieces(mdl::skel_piece p1, mdl::skel_piece p2);
}