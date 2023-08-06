#include "move_tool.h"
#include "canvas_item.h"
#include "tool_settings_pane.h"
#include "util.h"
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

	std::string fabrik_result_to_string(sm::fabrik_result result) {
		switch (result) {
			case sm::fabrik_result::target_reached:
				return "target reached";

			case sm::fabrik_result::converged:
				return "converged";

			case sm::fabrik_result::mixed:
				return "mixed";

			case sm::fabrik_result::no_solution_found:
				return "no solution found";
		}
	}

    class abstract_move_tool {
        move_mode mode_;
        QString name_;
    public:
        abstract_move_tool(move_mode mode, QString name) :
            mode_(mode), name_(name)
        {}

        QString name() const { return name_; }
        move_mode mode() const { return mode_; }

        virtual void keyPressEvent(move_state& ms) = 0;
        virtual void mousePressEvent(move_state& ms) = 0;
        virtual void mouseMoveEvent(move_state& ms) = 0;
        virtual void mouseReleaseEvent(move_state& ms) = 0;
    };

    ui::bone_item* parent_bone(ui::node_item* j) {
        auto maybe_parent_bone = j->model().parent_bone();
        if (!maybe_parent_bone) {
            return nullptr;
        }
        return &ui::item_from_model<ui::bone_item>(maybe_parent_bone->get());
    }

    ui::node_item* parent_node(ui::node_item* j) {
        auto maybe_parent_bone= j->model().parent_bone();
        if (!maybe_parent_bone) {
            return nullptr;
        }
        auto& parent_bone = maybe_parent_bone->get();
        auto& parent_node = ui::item_from_model<ui::node_item>
            (parent_bone.parent_node()
        );
        return &parent_node;
    }

    class fk_rotate_tool : public abstract_move_tool {
    public:
        fk_rotate_tool() :
            abstract_move_tool( move_mode::fk_rotate, "rotate (fk)" )
        {}

        void keyPressEvent(move_state& ms) override {
        };

        void mousePressEvent(move_state& ms) override {
            auto node = ms.canvas_->top_node(ms.event_->scenePos());
            if (!node) {
				ms.anchor_ = nullptr;
                return;
            }

            ms.anchor_ = parent_node(node);
            if (!ms.anchor_) {
                return;
            }

            ms.bone_ = parent_bone(node);
        };

        void mouseMoveEvent(move_state& ms) override {
			if (!ms.anchor_) {
				return;
			}
            auto theta = ui::angle_through_points(ms.anchor_->scenePos(), ms.event_->scenePos()) -
                ms.bone_->model().world_rotation();

            ms.bone_->model().rotate(theta);
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

        void keyPressEvent(move_state& ms) override {
            if (ms.key_event_->key() == Qt::Key::Key_Space) {
                QPoint pos = QCursor::pos();
                auto& view = ms.canvas_->view();
                pos = view.mapFromGlobal(pos);
                QPointF pos_scene = view.mapToScene(pos);

                auto node = ms.canvas_->top_node(pos_scene);
                if (!node) {
                    return;
                }
                bool current_state = node->model().is_pinned();
                node->set_pinned(!current_state);
            }
        }

        void mousePressEvent(move_state& ms) override {
            auto node = ms.canvas_->top_node(ms.event_->scenePos());
            if (!node) {
                return;
            }

            ms.anchor_ = node;
        };

        void mouseMoveEvent(move_state& ms) override {
            if (ms.anchor_) {
                auto result = sm::perform_fabrik(ms.anchor_->model(), ui::from_qt_pt(ms.event_->scenePos()));
                ms.canvas_->sync_to_model();
				//qDebug() << fabrik_result_to_string(result).c_str();
            }
        };

        void mouseReleaseEvent(move_state& ms) override {
            ms.anchor_ = nullptr;
        };
    };

    class fk_translate_tool : public abstract_move_tool {
    public:
        fk_translate_tool() :
            abstract_move_tool(move_mode::fk_translate, "translate (fk)")
        {}

        void keyPressEvent(move_state& ms) override {
        };

        void mousePressEvent(move_state& ms) override {
            auto node = ms.canvas_->top_node(ms.event_->scenePos());
            if (!node) {
                return;
            }
            ms.anchor_ = node;
        };

        void mouseMoveEvent(move_state& ms) override {
            if (ms.anchor_) {
                ms.anchor_->model().set_world_pos(
                    ui::from_qt_pt(ms.event_->scenePos())
                );
                ms.canvas_->sync_to_model();
            }
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

void  ui::move_tool::move_state::set(canvas& c, QKeyEvent* evnt) {
    canvas_ = &c;
    key_event_ = evnt;
}

/*------------------------------------------------------------------------------------------------*/

ui::move_tool::move_tool(tool_manager* mgr) :
        settings_(nullptr), 
        abstract_tool(mgr, "move", "move_icon.png", ui::tool_id::move) {

}

void ui::move_tool::keyPressEvent(canvas& c, QKeyEvent* event) {
    auto mode = static_cast<move_mode>(btns_->checkedId());
    if (mode == move_mode::none) {
        return;
    }
    state_.set(c, event);
    tool_for_mode(mode)->keyPressEvent(state_);
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

QWidget* ui::move_tool::settings_widget() {
    if (!settings_) {
        settings_ = new QWidget();
        btns_ = new QButtonGroup(settings_);
        auto* flow = new ui::FlowLayout(settings_);
        for (const auto& mm : k_move_mode_subtools) {
            auto* btn = new QRadioButton(mm->name());
            btns_->addButton(btn, static_cast<int>(mm->mode()));
            flow->addWidget(btn);
        }
    }
    return settings_;
}
