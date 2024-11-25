#pragma once

#include <string>

namespace sm {

    class animation {
        friend class skeleton;

        std::string name_;

    public:

        animation(const std::string& name);

    };

}