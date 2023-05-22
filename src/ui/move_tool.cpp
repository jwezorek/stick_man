#include "move_tool.h"
#include "tool_settings_pane.h"
#include "../core/ik_sandbox.h"
#include <unordered_map>
#include <array>
#include <numbers>

/*------------------------------------------------------------------------------------------------*/

namespace {

    using move_state = ui::move_tool::move_state;

    enum class move_mode {
        none = -1,
        ik_translate,
        fk_translate,
        fk_rotate
    };

    class abstract_move_tool {
        move_mode mode_;
        QString name_;
    public:
        abstract_move_tool(move_mode mode, QString name) :
            mode_(mode), name_(name)
        {}

        QString name() const { return name_; }
        move_mode mode() const { return mode_; }

        virtual void mousePressEvent(move_state& ms) = 0;
        virtual void mouseMoveEvent(move_state& ms) = 0;
        virtual void mouseReleaseEvent(move_state& ms) = 0;
    };

    class placeholder_move_tool : public abstract_move_tool {
    public:
        placeholder_move_tool(move_mode mode, QString name) :
            abstract_move_tool(mode, name)
        {}
        void mousePressEvent(move_state& ms) override {};
        void mouseMoveEvent(move_state& ms) override {};
        void mouseReleaseEvent(move_state& ms) override {};
    };

    ui::bone_item* parent_bone(ui::joint_item* j) {
        auto maybe_parent_bone = j->joint().parent_bone();
        if (!maybe_parent_bone) {
            return nullptr;
        }
        return &ui::item_from_sandbox<ui::bone_item>(maybe_parent_bone->get());
    }

    ui::joint_item* parent_joint(ui::joint_item* j) {
        auto maybe_parent_bone= j->joint().parent_bone();
        if (!maybe_parent_bone) {
            return nullptr;
        }
        auto& parent_bone = maybe_parent_bone->get();
        auto& parent_joint = ui::item_from_sandbox<ui::joint_item>
            (parent_bone.parent_joint()
        );
        return &parent_joint;
    }

    QRectF rect_from_circle(QPointF center, double radius) {
        auto topleft = center - QPointF(radius, radius);
        return { topleft, QSizeF(2.0 * radius, 2.0 * radius) };
    }

    int to_sixteenth_of_deg(double theta) {
        return static_cast<int>(
            theta * ((180.0 * 16.0) / std::numbers::pi)
        );
    }

    sm::point from_qt_pt(QPointF pt) {
        return { pt.x(), pt.y()};
    }

    double angle_through_points(QPointF origin, QPointF pt) {
        auto diff = pt - origin;
        return std::atan2(diff.y(), diff.x());
    }

    class fk_rotate_tool : public abstract_move_tool {
    public:
        fk_rotate_tool() :
            abstract_move_tool( move_mode::fk_rotate, "rotate (fk)" )
        {}

        void mousePressEvent(move_state& ms) override {
            auto joint = ms.canvas_->top_joint(ms.event_->scenePos());
            if (!joint) {
                return;
            }

            ms.anchor_ = parent_joint(joint);
            if (!ms.anchor_) {
                return;
            }

            ms.bone_ = parent_bone(joint);
        };

        void mouseMoveEvent(move_state& ms) override {
            auto theta = angle_through_points(ms.anchor_->scenePos(), ms.event_->scenePos()) -
                ms.bone_->bone().world_rotation();

            ms.bone_->bone().rotate(theta);
            ms.canvas_->sync_to_model();
        };

        void mouseReleaseEvent(move_state& ms) override {
        };
    };

    class ik_translate_tool : public abstract_move_tool {
    public:
        ik_translate_tool() :
            abstract_move_tool(move_mode::ik_translate, "translate (ik)")
        {}

        void mousePressEvent(move_state& ms) override {
            auto joint = ms.canvas_->top_joint(ms.event_->scenePos());
            if (!joint) {
                return;
            }

            ms.anchor_ = joint;
            ms.bone_ = parent_bone(joint);
        };

        void mouseMoveEvent(move_state& ms) override {
            debug_reach(ms.anchor_->joint(), from_qt_pt( ms.event_->scenePos()));
            ms.canvas_->sync_to_model();
        };

        void mouseReleaseEvent(move_state& ms) override {
        };
    };

    class fk_translate_tool : public abstract_move_tool {
    public:
        fk_translate_tool() :
            abstract_move_tool(move_mode::fk_translate, "translate (fk)")
        {}

        void mousePressEvent(move_state& ms) override {
        };

        void mouseMoveEvent(move_state& ms) override {
        };

        void mouseReleaseEvent(move_state& ms) override {
        };
    };

    auto k_move_mode_subtools = std::to_array<std::unique_ptr<abstract_move_tool>>({
            std::make_unique<fk_rotate_tool>(),
            std::make_unique<fk_translate_tool>(),
            std::make_unique<ik_translate_tool>()
        });

    abstract_move_tool* tool_for_mode(move_mode mode) {
        static std::unordered_map<move_mode, abstract_move_tool*> tbl;
        if (tbl.empty()) {
            for (auto& tool : k_move_mode_subtools) {
                tbl[tool->mode()] = tool.get();
            }
        }
        if (mode == move_mode::none) {
            return nullptr;
        }
        return tbl.at(mode);
    }
}

/*------------------------------------------------------------------------------------------------*/

ui::move_tool::move_state::move_state() : 
    canvas_(nullptr), 
    anchor_(nullptr),
    bone_(nullptr),
    event_(nullptr) 
{}

void ui::move_tool::move_state::set(canvas& c, QGraphicsSceneMouseEvent* evnt) {
    canvas_ = &c;
    event_ = evnt;
}

/*------------------------------------------------------------------------------------------------*/

ui::move_tool::move_tool() : abstract_tool("move", "move_icon.png", ui::tool_id::move) {

}

void ui::move_tool::mousePressEvent(canvas& c, QGraphicsSceneMouseEvent* event) {
    auto mode = static_cast<move_mode>(btns_->checkedId());
    if (mode == move_mode::none) {
        return;
    }
    state_.set(c, event);
    tool_for_mode(mode)->mousePressEvent(state_);
}

void ui::move_tool::mouseMoveEvent(canvas& c, QGraphicsSceneMouseEvent* event) {
    auto mode = static_cast<move_mode>(btns_->checkedId());
    if (mode == move_mode::none) {
        return;
    }
    state_.set(c, event);
    tool_for_mode(mode)->mouseMoveEvent(state_);
}

void ui::move_tool::mouseReleaseEvent(canvas& c, QGraphicsSceneMouseEvent* event) {
    auto mode = static_cast<move_mode>(btns_->checkedId());
    if (mode == move_mode::none) {
        return;
    }
    state_.set(c, event);
    tool_for_mode(mode)->mouseReleaseEvent(state_);
}

void ui::move_tool::populate_settings_aux(tool_settings_pane* pane) {
    btns_ = std::make_unique<QButtonGroup>();
    for (const auto& mm : k_move_mode_subtools) {
        auto* btn = new QRadioButton(mm->name());
        btns_->addButton(btn, static_cast<int>(mm->mode()));
        pane->contents()->addWidget(btn);
    }
}
