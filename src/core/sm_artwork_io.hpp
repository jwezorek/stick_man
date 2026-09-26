#pragma once
#include "sm_artwork.hpp"
#include "sm_package.hpp"
#include "json.hpp"

namespace sm::detail {

    nlohmann::json write_artwork(const artwork& art, const std::string& prefix, package_writer& package);
    artwork read_artwork(const nlohmann::json& json, const std::string& prefix, package_reader& package);

}
