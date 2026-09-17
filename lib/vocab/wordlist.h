// 内置词表。数据，不含逻辑 —— 加词就往 wordlist.cpp 的原始字符串里粘。
#pragma once

#include <cstddef>

namespace vocab {

extern const char *kRawWords;

// 期望词数。改词表后同步改这个数字，单元测试会对账。
constexpr size_t kExpectedWordCount = 100;

}  // namespace vocab
