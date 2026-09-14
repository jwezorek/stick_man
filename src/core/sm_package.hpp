#pragma once
// Private memory-only ZIP lifetime and resource handling.
#include "sm_image.hpp"
#include "miniz.h"
#include <cstring>
#include <stdexcept>
#include <string>

namespace sm::detail {
    class package_writer {
        mz_zip_archive zip_{};
    public:
        package_writer() { if (!mz_zip_writer_init_heap(&zip_, 0, 0)) throw std::runtime_error("ZIP writer initialization failed"); }
        ~package_writer() { mz_zip_writer_end(&zip_); }
        package_writer(const package_writer&) = delete;
        void add(const std::string& name, std::span<const std::uint8_t> bytes) {
            if (!mz_zip_writer_add_mem(&zip_, name.c_str(), bytes.data(), bytes.size(), MZ_DEFAULT_COMPRESSION))
                throw std::runtime_error("ZIP resource write failed");
        }
        image_buffer finish() {
            void* data = nullptr; size_t size = 0;
            if (!mz_zip_writer_finalize_heap_archive(&zip_, &data, &size)) throw std::runtime_error("ZIP finalization failed");
            std::unique_ptr<void, decltype(&mz_free)> owned(data, mz_free);
            return image_buffer(static_cast<std::uint8_t*>(data), static_cast<std::uint8_t*>(data) + size);
        }
    };
    class package_reader {
        mz_zip_archive zip_{};
    public:
        explicit package_reader(std::span<const std::uint8_t> bytes) {
            if (bytes.empty() || !mz_zip_reader_init_mem(&zip_, bytes.data(), bytes.size(), 0))
                throw std::invalid_argument("Invalid ZIP archive");
        }
        ~package_reader() { mz_zip_reader_end(&zip_); }
        package_reader(const package_reader&) = delete;
        bool contains(const std::string& name) { return mz_zip_reader_locate_file(&zip_, name.c_str(), nullptr, MZ_ZIP_FLAG_CASE_SENSITIVE) >= 0; }
        image_buffer read(const std::string& name) {
            int index = -1;
            for (mz_uint i = 0; i < mz_zip_reader_get_num_files(&zip_); ++i) {
                mz_zip_archive_file_stat stat{};
                if (!mz_zip_reader_file_stat(&zip_, i, &stat)) throw std::invalid_argument("Invalid ZIP entry");
                if (name == stat.m_filename) {
                    if (index >= 0 || stat.m_uncomp_size > 512ull * 1024 * 1024) throw std::invalid_argument("Duplicate or oversized ZIP resource");
                    index = int(i);
                }
            }
            if (index < 0) throw std::invalid_argument("Missing ZIP resource");
            size_t size = 0;
            std::unique_ptr<void, decltype(&mz_free)> data(mz_zip_reader_extract_to_heap(&zip_, mz_uint(index), &size, 0), mz_free);
            if (!data) throw std::invalid_argument("Damaged ZIP resource");
            return image_buffer(static_cast<std::uint8_t*>(data.get()), static_cast<std::uint8_t*>(data.get()) + size);
        }
    };
}
