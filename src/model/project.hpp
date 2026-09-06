#pragma once
#include <QWidget>
#include <QtWidgets>
#include <string>
#include <vector>
#include <memory>
#include <stack>
#include <cstddef>
#include <tuple>
#include <functional>
#include <optional>
#include <unordered_set>
#include "../core/sm_skeleton.hpp"
#include "handle.hpp"

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

        sm::world world_;
        std::stack<command> redo_stack_;
        std::stack<command> undo_stack_;
        std::size_t next_node_name_ = 1;
        std::size_t next_bone_name_ = 1;
        void clear_redo_stack();
        void execute_command(const command& cmd);
        void rename_aux(skel_piece piece, const std::string& new_name);
        bool can_rename(skel_piece piece, const std::string& new_name);
        void replace_skeletons_aux(
            const std::vector<sm::object_id>& replacees,
            const std::vector<sm::skel_ref>& replacements,
            std::vector<sm::object_id>* new_ids_of_replacements,
            const std::unordered_set<sm::object_id>& regenerate_ids = {});
        void clear();
        std::string next_default_node_name();
        std::string next_default_bone_name();
        void advance_default_name_counters_from_world();
    public:
        project();
        const sm::world& world() const;
        sm::world& world();
        model_object get(const sm::object_id& id);
        const_model_object get(const sm::object_id& id) const;
        bool can_undo() const;
        bool can_redo() const;
        std::string to_json() const;
        bool from_json(const std::string& str);
        void undo();
        void redo();
        void add_bone(const handle& node_u, const handle& node_v);
        void add_new_skeleton_root(sm::point loc);
        bool rename(skel_piece piece, const std::string& new_name);
        void replace_skeletons(
            const std::vector<sm::object_id>& replacees,
            const std::vector<sm::skel_ref>& replacements,
            const std::unordered_set<sm::object_id>& regenerate_ids = {}
        );
        void transform(const std::vector<handle>& nodes,
            const std::function<void(sm::node&)>& fn);
        void transform(const std::vector<handle>& nodes,
            const std::function<void(sm::bone&)>& fn);
        using node_locs = std::vector<std::tuple<handle, sm::point>>;
        void transform_node_positions(
            const node_locs& old_locs, const node_locs& new_locs
        );
    signals:
        void pre_new_bone_added(sm::node& u, sm::node& v);
        void new_bone_added(sm::bone& bone);
        void new_project_opened(project& model);
        void new_skeleton_added(sm::skel_ref skel);
        void refresh_canvas(project& model, bool clear);
        void name_changed(skel_piece piece, const std::string& new_name);
        void refresh_undo_redo_state(bool, bool);
    };
    bool identical_pieces(mdl::skel_piece p1, mdl::skel_piece p2);
}
