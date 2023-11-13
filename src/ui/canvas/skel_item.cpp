#include "scene.h"
#include "canvas_item.h"
#include "skel_item.h"
#include "../util.h"
#include "../../core/sm_skeleton.h"

/*------------------------------------------------------------------------------------------------*/

namespace r = std::ranges;
namespace rv = std::ranges::views;

namespace {

    constexpr auto k_skel_marg = 11.0;

    QRectF scale_rect(double scale, const QRectF r) {
        return {
            scale * r.topLeft().x(),
            scale * r.topLeft().y(),
            scale * r.width(),
            scale * r.height()
        };
    }

    QRectF inflate_rect(const QRectF& originalRect, qreal amnt)
    {
        qreal x = originalRect.x() - amnt;
        qreal y = originalRect.y() - amnt;
        qreal width = originalRect.width() + 2 * amnt;
        qreal height = originalRect.height() + 2 * amnt;

        return QRectF(x, y, width, height);
    }

    QRectF skeleton_bounds(const sm::skeleton& skel) {
        auto pts = skel.nodes() | rv::transform(
            [](const sm::node& node)->sm::point {
                return node.world_pos();
            }
        ) | r::to<std::vector<sm::point>>();
        auto [min_x, max_x] = r::minmax(
            pts | rv::transform([](const auto& pt) {return pt.x; })
        );
        auto [min_y, max_y] = r::minmax(
            pts | rv::transform([](const auto& pt) {return pt.y; })
        );
        return {
            min_x, min_y,
            max_x - min_x,
            max_y - min_y
        };
    }

}

/*------------------------------------------------------------------------------------------------*/

void ui::canvas::skeleton_item::sync_item_to_model() {
    QRectF rect = inflate_rect(skeleton_bounds(model_), k_skel_marg);
    auto& canv = *canvas();
    double inv_scale = 1.0 / canv.scale();
    setRect(
        scale_rect(inv_scale, rect)
    );
    setVisible(is_selected());
}

void ui::canvas::skeleton_item::sync_sel_frame_to_model() {
}

QGraphicsItem* ui::canvas::skeleton_item::create_selection_frame() const {
    return nullptr;
}

bool ui::canvas::skeleton_item::is_selection_frame_only() const {
    return true;
}

QGraphicsItem* ui::canvas::skeleton_item::item_body() {
    return this;
}

mdl::const_skel_piece ui::canvas::skeleton_item::to_skeleton_piece() const {
    auto& skel = model();
    return std::ref(skel);
}

ui::canvas::skeleton_item::skeleton_item(sm::skeleton& skel, double scale) :
    has_stick_man_model<ui::canvas::skeleton_item, sm::skeleton&>(skel) {
    setPen(QPen(Qt::cyan, 3, Qt::DotLine));
    setBrush(Qt::NoBrush);
    setVisible(false);
}
