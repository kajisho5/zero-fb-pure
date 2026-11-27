// テスト用：指定した区間の中で起きたヒープ確保の回数を数える。
#pragma once
#include <cstddef>

namespace zfptest
{
struct AllocScope
{
    AllocScope() noexcept;
    ~AllocScope() noexcept;
    static std::size_t count() noexcept; // 直近の区間で数えた回数
};
} // namespace zfptest
