#include "bone_properties.h"
#include "../canvas/canvas.h"
#include "../canvas/canvas_item.h"
#include "../canvas/bone_item.h"
#include "../canvas/node_item.h"
#include "../canvas/skel_item.h"
#include "../panes/skeleton_pane.h"
#include <ranges>
#include <functional>
#include <numbers>

using namespace std::placeholders;
namespace r = std::ranges;
namespace rv = std::ranges::views;

namespace {

    constexpr double k_default_rot_constraint_min = -std::numbers::pi / 2.0;
    constexpr double k_default_rot_constraint_span = std::numbers::pi;

    void set_rot_constraints(mdl::project& proj, ui::canvas::canvas& canv,
            bool is_parent_relative, double start, double span) {
        proj.transform(
            mdl::to_handles(ui::canvas::to_model_ptrs(canv.selected_bones())) |
            r::to<std::vector<mdl::handle>>(),
            [&](sm::bone& bone) {
                bone.set_rotation_constraint(start, span, is_parent_relative);
            }
        );
    }

    void remove_rot_constraints(mdl::project& proj, ui::canvas::canvas& canv) {
        proj.transform(
            mdl::to_handles(ui::canvas::to_model_ptrs(canv.selected_bones())) |
            r::to<std::vector<mdl::handle>>(),
            [](sm::bone& bone) {
                bone.remove_rotation_constraint();
            }
        );
    }

    std::vector<sm::bone*> topological_sort_selected_bones(ui::canvas::canvas& canv) {
        auto bone_items = canv.selected_bones();
        std::unordered_set<sm::bone*> selected = ui::canvas::to_model_ptrs(bone_items) |
            r::to< std::unordered_set<sm::bone*>>();
        std::vector<sm::bone*> ordered_bones;
        auto skeletons = canv.skeleton_items();

        for (auto skel_item : skeletons) {
            auto& skel = skel_item->model();
            sm::visit_bones(skel.root_node(),
                [&](auto& bone)->sm::visit_result {
                    if (selected.contains(&bone)) {
                        ordered_bones.push_back(&bone);
                    }
                    return sm::visit_result::continue_traversal;
                }
            );
        }

        return ordered_bones;
    }

    void set_selected_bone_length(mdl::project& proj, ui::canvas::canvas& canv, double new_length) {
        auto ordered = topological_sort_selected_bones(canv);
        proj.transform(
            mdl::to_handles(rv::all(ordered)) | r::to<std::vector<mdl::handle>>(),
            [new_length](sm::bone_ref bone) {
                bone.get().set_length(new_length);
            }
        );
    }

    void set_selected_bone_rotation(mdl::project& proj, ui::canvas::canvas& canv, double theta) {
        auto ordered = topological_sort_selected_bones(canv);
        // sort bones into topological order and then set world rotation...
        proj.transform(
            mdl::to_handles(rv::all(ordered)) | r::to<std::vector<mdl::handle>>(),
            [theta](sm::bone_ref bone) {
                bone.get().set_world_rotation(theta);
            }
        );
    }
}

namespace ui {
    namespace pane {
        namespace props {
            class rot_constraint_box : public QGroupBox {
                QPushButton* btn_;
                ui::labeled_numeric_val* start_angle_;
                ui::labeled_numeric_val* span_angle_;
                QComboBox* mode_;
                ui::canvas::canvas& canv_;
                mdl::project& proj_;

                bool is_constraint_relative_to_parent() const {
                    return mode_->currentIndex() == 0;
                }

                double constraint_start_angle() const {
                    if (!start_angle_->num_edit()->value()) {
                        throw std::runtime_error("attempted to get value from indeterminate value box");
                    }
                    return ui::degrees_to_radians(*start_angle_->num_edit()->value());
                }

                double constraint_span_angle() const {
                    if (!span_angle_->num_edit()->value()) {
                        throw std::runtime_error("attempted to get value from indeterminate value box");
                    }
                    return ui::degrees_to_radians(*span_angle_->num_edit()->value());
                }

                void set_constraint_angle(mdl::project& proj, ui::canvas::canvas& canv, double val, bool is_start_angle) {
                    auto theta = ui::degrees_to_radians(val);
                    auto start_angle = is_start_angle ? theta : constraint_start_angle();
                    auto span_angle = is_start_angle ? constraint_span_angle() : theta;
                    set_rot_constraints(proj, canv,
                        is_constraint_relative_to_parent(), start_angle, span_angle);
                    canv.sync_to_model();
                }

                void set_constraint_mode(mdl::project& proj, ui::canvas::canvas& canv, bool relative_to_parent) {
                    set_rot_constraints(proj, canv,
                        relative_to_parent, constraint_start_angle(), constraint_span_angle()
                    );
                    canv.sync_to_model();
                }

                void showEvent(QShowEvent* event) override
                {
                    QWidget::showEvent(event);
                    btn_->setText("remove rotation constraint");
                }

                void hideEvent(QHideEvent* event) override
                {
                    QWidget::hideEvent(event);
                    btn_->setText("add rotation constraint");
                }

            public:
                rot_constraint_box(QPushButton* btn, ui::canvas::canvas& canv, mdl::project& proj) :
                    btn_(btn),
                    start_angle_(nullptr),
                    span_angle_(nullptr),
                    mode_(nullptr),
                    canv_(canv),
                    proj_(proj) {
                    this->setTitle("rotation constraint");
                    btn_->setText("add rotation constraint");
                    auto* layout = new QVBoxLayout();
                    this->setLayout(layout);
                    layout->addWidget(start_angle_ = new ui::labeled_numeric_val("start angle", 0, -180, 180));
                    layout->addWidget(span_angle_ = new ui::labeled_numeric_val("span angle", 0, 0, 360));
                    layout->addWidget(new QLabel("mode"));
                    layout->addWidget(mode_ = new QComboBox());
                    mode_->addItem("relative to parent");
                    mode_->addItem("relative to world");
                    this->hide();

                    connect(start_angle_->num_edit(), &ui::number_edit::value_changed,
                        [this](double v) {
                            set_constraint_angle(proj_, canv_, v, true);
                        }
                    );

                    connect(span_angle_->num_edit(), &ui::number_edit::value_changed,
                        [this](double v) {
                            set_constraint_angle(proj_, canv_, v, false);
                        }
                    );

                    connect(mode_, &QComboBox::currentIndexChanged,
                        [this](int new_index) {
                            set_constraint_mode(proj_, canv_, new_index == 0);
                        }
                    );
                }

                void set(bool parent_relative, double start, double span) {
                    start_angle_->num_edit()->set_value(ui::radians_to_degrees(start));
                    span_angle_->num_edit()->set_value(ui::radians_to_degrees(span));
                    mode_->setCurrentIndex(parent_relative ? 0 : 1);
                    show();
                }
            };

            std::function<void()> make_select_node_fn(const current_canvas_fn& get_current_canv,
                bool parent_node) {
                return [get_current_canv, parent_node]() {
                    auto& canv = get_current_canv();
                    auto bones = canv.selected_bones();
                    if (bones.size() != 1) {
                        return;
                    }
                    sm::bone& bone = bones.front()->model();
                    auto& node_itm = ui::canvas::item_from_model<ui::canvas::node_item>(
                        (parent_node) ? bone.parent_node() : bone.child_node()
                    );
                    canv.set_selection(&node_itm, true);
                    };
            }

            class rotation_tab : public ui::tabbed_values {
            private:
                current_canvas_fn get_current_canv_;

                double convert_to_or_from_parent_coords(double val, bool to_parent) {
                    auto bone_selection = get_current_canv_().selected_bones();
                    if (bone_selection.size() != 1) {
                        return val;
                    }
                    auto& bone = bone_selection.front()->model();
                    auto parent = bone.parent_bone();
                    if (!parent) {
                        return val;
                    }
                    auto parent_rot = ui::radians_to_degrees(parent->get().world_rotation());
                    return (to_parent) ?
                        val - parent_rot :
                        val + parent_rot;
                }

            public:
                rotation_tab(const current_canvas_fn& get_current_canv) :
                    get_current_canv_(get_current_canv),
                    ui::tabbed_values(nullptr,
                { "world", "parent" }, {
                    {"rotation", 0.0, -180.0, 180.0}
                }, 100
                    )
                {}

                double to_nth_tab(int tab, int, double val) override {
                    if (tab == 0) {
                        return val;
                    }
                    return convert_to_or_from_parent_coords(val, true);
                }

                double from_nth_tab(int tab, int, double val) override {
                    if (tab == 0) {
                        return val;
                    }
                    return convert_to_or_from_parent_coords(val, false);
                }
            };
        }
    }
}

void ui::pane::props::bones::add_or_delete_constraint(mdl::project& proj, ui::canvas::canvas& canv) {
    bool is_adding = !constraint_box_->isVisible();
    if (is_adding) {
        set_rot_constraints(proj, canv, true,
            k_default_rot_constraint_min, k_default_rot_constraint_span);
        constraint_box_->set(
            true, k_default_rot_constraint_min, k_default_rot_constraint_span
        );
    }
    else {
        remove_rot_constraints(proj, canv);
        constraint_box_->hide();
    }
    get_current_canv_().sync_to_model();
}

ui::pane::props::bones::bones(const current_canvas_fn& fn, selection_properties* parent) :
    single_or_multi_props_widget(
        fn,
        parent,
        "selected bones"
    ),
    name_(nullptr),
    length_(nullptr),
    rotation_(nullptr),
    constraint_box_(nullptr),
    constraint_btn_(nullptr),
    u_(nullptr),
    v_(nullptr),
    nodes_(nullptr) {
}

void ui::pane::props::bones::populate(mdl::project& proj) {
    layout_->addWidget(
        name_ = new ui::labeled_field("   name", "")
    );
    name_->set_color(QColor("yellow"));
    name_->value()->set_validator(
        [this](const std::string& new_name)->bool {
            return parent_->skel_pane().validate_props_name_change(new_name);
        }
    );

    nodes_ = new QWidget();
    auto vert_pair = new QVBoxLayout(nodes_);
    vert_pair->addWidget(new QLabel("nodes"));
    vert_pair->addWidget(u_ = new ui::labeled_hyperlink("   u", ""));
    vert_pair->addWidget(v_ = new ui::labeled_hyperlink("   v", ""));
    vert_pair->setAlignment(Qt::AlignTop);
    vert_pair->setSpacing(0);

    layout_->addWidget(nodes_);

    layout_->addWidget(
        length_ = new ui::labeled_numeric_val("length", 0.0, 0.0, 1500.0)
    );

    layout_->addWidget(rotation_ = new rotation_tab(get_current_canv_));


    constraint_btn_ = new QPushButton();
    layout_->addWidget(constraint_box_ = new rot_constraint_box(
        constraint_btn_, get_current_canv_(), proj)
    );
    layout_->addWidget(constraint_btn_);

    connect(constraint_btn_, &QPushButton::clicked,
        [&]() {
            add_or_delete_constraint(proj, get_current_canv_());
        }
    );

    connect(&proj, &mdl::project::name_changed,
        [this](mdl::skel_piece piece, const std::string& new_name) {
            handle_rename(piece, name_->value(), new_name);
        }
    );

    connect(name_->value(), &ui::string_edit::value_changed,
        this, &bones::do_property_name_change
    );

    connect(u_->hyperlink(), &QPushButton::clicked,
        make_select_node_fn(get_current_canv_, true)
    );
    connect(v_->hyperlink(), &QPushButton::clicked,
        make_select_node_fn(get_current_canv_, false)
    );

    connect(length_->num_edit(), &ui::number_edit::value_changed,
        [this, &proj](double val) {
            auto& canv = get_current_canv_();
            set_selected_bone_length(proj, canv, val);
        }
    );
    connect(rotation_, &ui::tabbed_values::value_changed,
        [this, &proj](int) {
            auto rot = rotation_->value(0);
            set_selected_bone_rotation(
                proj,
                get_current_canv_(),
                ui::degrees_to_radians(*rot)
            );
        }
    );
}

bool ui::pane::props::bones::is_multi(const ui::canvas::canvas& canv) {
    return canv.selected_bones().size() > 1;
}

void ui::pane::props::bones::set_selection_common(const ui::canvas::canvas& canv) {
    const auto& sel = canv.selection();
    auto bones = ui::canvas::to_model_ptrs(ui::as_range_view_of_type<ui::canvas::bone_item>(sel));
    auto length = get_unique_val(bones |
        rv::transform([](sm::bone* b) {return b->scaled_length(); }));
    length_->num_edit()->set_value(length);

    auto world_rot = get_unique_val(bones |
        rv::transform([](sm::bone* b) {return b->world_rotation(); }));
    rotation_->set_value(0, world_rot.transform(ui::radians_to_degrees));
}

void ui::pane::props::bones::set_selection_multi(const ui::canvas::canvas& canv) {
    name_->hide();
    nodes_->hide();
    constraint_box_->hide();
    constraint_btn_->hide();
    rotation_->lock_to_primary_tab();
}

void ui::pane::props::bones::set_selection_single(const ui::canvas::canvas& canv) {
    name_->show();
    nodes_->show();
    constraint_btn_->show();
    rotation_->unlock();
    auto& bone = canv.selected_bones().front()->model();
    name_->set_value(bone.name().c_str());
    auto rot_constraint = bone.rotation_constraint();
    if (rot_constraint) {
        constraint_box_->show();
        constraint_box_->set(
            rot_constraint->relative_to_parent,
            rot_constraint->start_angle,
            rot_constraint->span_angle
        );
    }
    else {
        constraint_box_->hide();
    }
    u_->hyperlink()->setText(bone.parent_node().name().c_str());
    v_->hyperlink()->setText(bone.child_node().name().c_str());
}

void ui::pane::props::bones::lose_selection() {
    if (constraint_box_) {
        constraint_box_->hide();
    }
}
