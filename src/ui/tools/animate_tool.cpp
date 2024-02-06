#include "animate_tool.h"
#include "../canvas/node_item.h"
#include "../canvas/bone_item.h"
#include "../canvas/skel_item.h"
#include "../panes/tool_settings_pane.h"
#include "../util.h"
#include "../../core/sm_skeleton.h"
#include "../../core/sm_fabrik.h"
#include "../../core/sm_traverse.h"
#include <unordered_map>
#include <array>
#include <numbers>

/*------------------------------------------------------------------------------------------------*/

namespace {

    using move_state = ui::tool::animate::move_state;

    enum class move_mode {
        none = -1,
        ik_translate,
        fk_translate,
        fk_rotate
    };

	std::string fabrik_result_to_string(sm::result result) {
		switch (result) {
			case sm::result::fabrik_target_reached:
				return "target reached";

			case sm::result::fabrik_converged:
				return "converged";

			case sm::result::fabrik_mixed:
				return "mixed";

			case sm::result::fabrik_no_solution_found:
				return "no solution found";
		}
	}

	std::vector<sm::node_ref> pinned_nodes(sm::node& node) {
		std::vector<sm::node_ref> pinned_nodes;
		auto visit_node = [&pinned_nodes](sm::node& j)->sm::visit_result {
			ui::canvas::item::node& ni = ui::canvas::item_from_model<ui::canvas::item::node>(j);
			if (ni.is_pinned()) {
				pinned_nodes.emplace_back(j);
			}
			return sm::visit_result::continue_traversal;
			};
		sm::dfs(node, visit_node, {}, false);
		return pinned_nodes;
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

    ui::canvas::item::bone* parent_bone(ui::canvas::item::node* j) {
        auto maybe_parent_bone = j->model().parent_bone();
        if (!maybe_parent_bone) {
            return nullptr;
        }
        return &ui::canvas::item_from_model<ui::canvas::item::bone>(maybe_parent_bone->get());
    }

    ui::canvas::item::node* parent_node(ui::canvas::item::node* j) {
        auto maybe_parent_bone= j->model().parent_bone();
        if (!maybe_parent_bone) {
            return nullptr;
        }
        auto& parent_bone = maybe_parent_bone->get();
        auto& parent_node = ui::canvas::item_from_model<ui::canvas::item::node>
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

            ms.bone_->model().rotate_by(theta);
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
                QPointF pos_scene = ms.canvas_->from_global_to_canvas(QCursor::pos());

                auto node = ms.canvas_->top_node(pos_scene);
                if (!node) {
                    return;
                }
                bool current_state = node->is_pinned();
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
				std::vector<std::tuple<sm::node_ref, sm::point>> target = {
					{ms.anchor_->model(), ui::from_qt_pt(ms.event_->scenePos())}
				};
                auto result = sm::perform_fabrik(
					target,
					pinned_nodes(ms.anchor_->model())
				);
                ms.canvas_->sync_to_model();
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

ui::tool::animate::move_state::move_state() :
    canvas_(nullptr), 
    anchor_(nullptr),
    bone_(nullptr),
    event_(nullptr),
	key_event_(nullptr)
{}

void ui::tool::animate::move_state::set(canvas::scene& c, QGraphicsSceneMouseEvent* evnt) {
    canvas_ = &c;
    event_ = evnt;
}

void  ui::tool::animate::move_state::set(canvas::scene& c, QKeyEvent* evnt) {
    canvas_ = &c;
    key_event_ = evnt;
}

/*------------------------------------------------------------------------------------------------*/

ui::tool::animate::animate() :
        settings_(nullptr),
		btns_(nullptr),
        base("animate", "move_icon.png", ui::tool::id::animate) {
}

void ui::tool::animate::keyPressEvent(canvas::scene& c, QKeyEvent* event) {
    auto mode = static_cast<move_mode>(btns_->checkedId());
    if (mode == move_mode::none) {
        return;
    }
    state_.set(c, event);
    tool_for_mode(mode)->keyPressEvent(state_);
}

void ui::tool::animate::mousePressEvent(canvas::scene& c, QGraphicsSceneMouseEvent* event) {
    auto mode = static_cast<move_mode>(btns_->checkedId());
    if (mode == move_mode::none) {
        return;
    }
    state_.set(c, event);
    tool_for_mode(mode)->mousePressEvent(state_);
}

void ui::tool::animate::mouseMoveEvent(canvas::scene& c, QGraphicsSceneMouseEvent* event) {
    auto mode = static_cast<move_mode>(btns_->checkedId());
    if (mode == move_mode::none) {
        return;
    }
    state_.set(c, event);
    tool_for_mode(mode)->mouseMoveEvent(state_);
}

void ui::tool::animate::mouseReleaseEvent(canvas::scene& c, QGraphicsSceneMouseEvent* event) {
    auto mode = static_cast<move_mode>(btns_->checkedId());
    if (mode == move_mode::none) {
        return;
    }
    state_.set(c, event);
    tool_for_mode(mode)->mouseReleaseEvent(state_);
}

QWidget* ui::tool::animate::settings_widget() {
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
