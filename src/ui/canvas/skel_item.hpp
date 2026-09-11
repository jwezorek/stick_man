#pragma once
#include "canvas_item.hpp"

namespace ui::canvas::item {
    // Shared topology-bounds frame; labels do not contribute to the rig bounds.
    class aggregate_frame : public base, public has_treeview_item, public QGraphicsRectItem {
        QGraphicsSimpleTextItem* label_ = nullptr;
        QGraphicsRectItem* tag_ = nullptr;
        void sync_item_to_model() override;
        void sync_sel_frame_to_model() override {}
        QGraphicsItem* create_selection_frame() const override { return nullptr; }
        bool is_selection_frame_only() const override { return true; }
        QGraphicsItem* item_body() override { return this; }
    protected:
        aggregate_frame(QColor color, bool labeled);
        virtual std::vector<sm::const_skel_ref> components() const = 0;
        virtual std::string label() const { return {}; }
    };
    class skeleton : public aggregate_frame {
        sm::skeleton& model_;
        std::vector<sm::const_skel_ref> components() const override { return {model_}; }
        mdl::const_skel_piece to_skeleton_piece() const override { return sm::const_skel_ref(model_); }
    public:
        using model_type = sm::skeleton;
        skeleton(sm::skeleton& skel, double scale);
        sm::skeleton& model() { return model_; }
        const sm::skeleton& model() const { return model_; }
    };
    class character : public aggregate_frame {
        const sm::project& project_;
        sm::object_id id_;
        std::vector<sm::const_skel_ref> components() const override { return model().rig().skeletons(); }
        std::string label() const override { return model().name(); }
        mdl::const_skel_piece to_skeleton_piece() const override;
    public:
        character(const sm::character& character);
        const sm::character& model() const { return project_.character(id_)->get(); }
        const sm::object_id& id() const { return id_; }
        mdl::selection_object to_selection_object() const override { return sm::const_character_ref(model()); }
    };
}
