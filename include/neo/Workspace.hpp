#pragma once

#include "neo/Unicode.hpp"
#include "neo/detail/SegmentScratch.hpp"

#include <vector>

namespace neo_cppjieba {

// Caller-owned decoding and segmentation storage. One active call owns the workspace;
// results never borrow its buffers, and capacity is retained until release or destruction.
class Workspace {
public:
    Workspace() = default;
    Workspace(Workspace &&) noexcept = default;
    auto operator=(Workspace &&) noexcept -> Workspace & = default;
    Workspace(const Workspace &) = delete;
    auto operator=(const Workspace &) -> Workspace & = delete;

    auto release() noexcept -> void {
        *this = Workspace{};
    }

private:
    friend class Jieba;

    UnicodeWithOffset decoded_;
    detail::SegmentScratch scratch_;
    std::vector<WordRange> ranges_;
};

} // namespace neo_cppjieba
