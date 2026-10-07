#include "artwork_browser.hpp"
#include "artwork_browser_detail.hpp"
#include "../canvas/canvas_manager.hpp"
#include "../canvas/artwork_layer.hpp"
#include <algorithm>
#include <ranges>
#include <span>
#include <stdexcept>
#include <utility>

using namespace ui::pane::artwork_browser_detail;

std::optional<sm::object_id> ui::pane::artwork_browser::selected_background_id() const {
	if (!backgrounds_ || !backgrounds_->currentItem()) {
		return {};
	}
	auto parsed = sm::object_id::from_string(
	    backgrounds_->currentItem()->data(Qt::UserRole).toString().toStdString());
	return parsed ? std::optional(*parsed) : std::nullopt;
}

void ui::pane::artwork_browser::sync_target_to_canvas() {
	character_ = artwork_character(canvases_.active_canvas().selected_objects());
	backgrounds_target_ = !character_.has_value();
}

void ui::pane::artwork_browser::move_selected_background(int direction) {
	if (refreshing_) {
		return;
	}
	auto id = selected_background_id();
	if (!id) {
		return;
	}
	const auto& backgrounds = project_.core().backgrounds();
	auto found = std::ranges::find(backgrounds, *id, &sm::background_image::id);
	if (found == backgrounds.end()) {
		return;
	}
	const auto from = int(found - backgrounds.begin());
	const auto to = direction == -2 ? 0
	    : direction == 2           ? int(backgrounds.size()) - 1
	                               : std::clamp(from + direction, 0, int(backgrounds.size()) - 1);
	if (from == to) {
		return;
	}
	project_.edit_backgrounds([&](auto& changed) {
		auto moved = std::move(changed[std::size_t(from)]);
		changed.erase(changed.begin() + from);
		changed.insert(changed.begin() + to, std::move(moved));
	});
	canvases_.active_canvas().artwork().set_selected_background(*id);
}

void ui::pane::artwork_browser::import_backgrounds() {
	auto files = QFileDialog::getOpenFileNames(this, "Import background images", {},
	    "Images (*.png *.jpg *.jpeg *.bmp *.tga *.gif *.psd *.pic *.pnm)");
	if (files.empty()) {
		return;
	}

	struct imported_background {
		std::string base_name;
		sm::image_resource image;
	};
	std::vector<imported_background> imports;
	imports.reserve(files.size());
	for (const auto& path : files) {
		QFile file(path);
		if (!file.open(QIODevice::ReadOnly)) {
			throw std::runtime_error("Could not read image file.");
		}
		auto bytes = file.readAll();
		if (file.error() != QFileDevice::NoError) {
			throw std::runtime_error("Could not read complete image file.");
		}
		const auto* data = reinterpret_cast<const std::uint8_t*>(bytes.constData());
		imports.push_back({ QFileInfo(path).completeBaseName().toStdString(),
		    sm::image_resource::decode(std::span<const std::uint8_t>(data, bytes.size())) });
	}

	std::optional<sm::object_id> last;
	project_.edit_backgrounds([&](auto& backgrounds) {
		for (auto& imported : imports) {
			auto name = imported.base_name;
			auto name_used = [&](const std::string& candidate) {
				return std::ranges::any_of(
				    backgrounds, [&](const auto& background) { return background.name == candidate; });
			};
			for (int suffix = 2; name_used(name); ++suffix) {
				name = imported.base_name + "_" + std::to_string(suffix);
			}

			auto id = sm::object_id::generate();
			auto id_used = [&](sm::object_id candidate) {
				if (std::ranges::any_of(backgrounds,
				        [&](const auto& background) { return background.id == candidate; })) {
					return true;
				}
				try {
					(void)std::as_const(project_.core()).get(candidate);
					return true;
				} catch (const std::runtime_error&) {
					return false;
				}
			};
			while (id_used(id)) {
				id = sm::object_id::generate();
			}
			last = id;
			backgrounds.push_back(
			    sm::background_image{ id, std::move(name), std::move(imported.image), {} });
		}
	});
	if (last) {
		canvases_.active_canvas().artwork().set_selected_background(*last);
	}
}

