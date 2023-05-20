#include "move_tool.h"
#include "tool_settings_pane.h"
#include <unordered_map>
#include <array>

/*------------------------------------------------------------------------------------------------*/

namespace {

    using move_state = ui::move_tool::move_state;

    enum class move_mode {
        none = -1,
        translate = 0,
        rotate,
        pinned_ik,
        ik_free,
        end
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

    auto k_move_mode = std::to_array<std::unique_ptr<abstract_move_tool>>({

            std::make_unique<placeholder_move_tool>(
                move_mode::translate ,
                "translate (free)"
            ),
            std::make_unique<placeholder_move_tool>(
                move_mode::rotate ,
                "rotate (forward kinematics)"
            ),
            std::make_unique<placeholder_move_tool>(
                 move_mode::pinned_ik ,
                "translate pinned (ik)"
            ),
            std::make_unique<placeholder_move_tool>(
                move_mode::ik_free ,
                "translate unpinned (ik)"
            )
        });

    abstract_move_tool* tool_for_mode(move_mode mode) {
        static std::unordered_map<move_mode, abstract_move_tool*> tbl;
        if (tbl.empty()) {
            auto end = static_cast<int>(move_mode::end);
            for (int i = 0; i < end; ++i) {
                tbl[static_cast<move_mode>(i)] = k_move_mode[i].get();
            }
        }
        if (mode == move_mode::none || mode == move_mode::end) {
            return nullptr;
        }
        return tbl.at(mode);
    }
}

/*------------------------------------------------------------------------------------------------*/

ui::move_tool::move_state::move_state() : 
    canvas_(nullptr), 
    anchor_(0,0), 
    rubber_band_(nullptr), 
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
    state_.set(c, event);
    tool_for_mode(mode)->mousePressEvent(state_);
}

void ui::move_tool::mouseMoveEvent(canvas& c, QGraphicsSceneMouseEvent* event) {
    auto mode = static_cast<move_mode>(btns_->checkedId());
    state_.set(c, event);
    tool_for_mode(mode)->mouseMoveEvent(state_);
}

void ui::move_tool::mouseReleaseEvent(canvas& c, QGraphicsSceneMouseEvent* event) {
    auto mode = static_cast<move_mode>(btns_->checkedId());
    state_.set(c, event);
    tool_for_mode(mode)->mouseReleaseEvent(state_);
}

void ui::move_tool::populate_settings_aux(tool_settings_pane* pane) {
    btns_ = std::make_unique<QButtonGroup>();
    for (const auto& mm : k_move_mode) {
        auto* btn = new QRadioButton(mm->name());
        btns_->addButton(btn, static_cast<int>(mm->mode()));
        pane->contents()->addWidget(btn);
    }
}
