#include "skel_item.hpp"
#include "scene.hpp"
#include <limits>
#include <algorithm>
#include <stdexcept>

ui::canvas::item::aggregate_frame::aggregate_frame(QColor color, bool labeled) {
    setPen(QPen(color, 3, Qt::DotLine));
    setBrush(Qt::NoBrush);
    setZValue(-1);
    setVisible(false);
    if (labeled) {
        tag_ = new QGraphicsRectItem(this);
        tag_->setPen(Qt::NoPen);
        tag_->setBrush(color);
        tag_->setFlag(QGraphicsItem::ItemIgnoresTransformations);
        label_ = new QGraphicsSimpleTextItem(tag_);
        label_->setBrush(Qt::white);
    }
}
void ui::canvas::item::aggregate_frame::sync_item_to_model() {
    double left = std::numeric_limits<double>::max(), bottom = left;
    double right = -left, top = -left;
    for (auto skel : components()) {
        for (auto node : skel->nodes()) {
            auto p = node->world_pos();
            left = std::min(left, p.x); right = std::max(right, p.x);
            bottom = std::min(bottom, p.y); top = std::max(top, p.y);
        }
    }
    if (left > right) { setRect({}); return; }
    const double margin = 11.0 / canvas()->scale();
    setRect(QRectF(left, bottom, right - left, top - bottom).adjusted(-margin, -margin, margin, margin));
    auto p = pen(); p.setCosmetic(true); setPen(p);
    if (tag_) {
        label_->setText(QString::fromStdString(label()));
        auto bounds = label_->boundingRect();
        tag_->setRect(0, -bounds.height() - 6, bounds.width() + 12, bounds.height() + 6);
        tag_->setPos(rect().left(), rect().bottom()); // Y points upward in the canvas.
        label_->setPos(6, -bounds.height() - 3);
    }
}
ui::canvas::item::skeleton::skeleton(sm::skeleton& skel, double) :
    aggregate_frame(Qt::cyan, false), model_(skel) {
    skel.set_user_data(sm::ref(*this));
}
ui::canvas::item::character::character(const sm::character& character) :
    aggregate_frame(QColor(133, 77, 181), true), project_(character.owner()), id_(character.id()) {}
mdl::const_skel_piece ui::canvas::item::character::to_skeleton_piece() const {
    throw std::logic_error("character must be expanded before topology editing");
}
