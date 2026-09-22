// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#pragma once

#include <string>
#include <string_view>
#include <unordered_map>

#include "ext_status.h"
#include "ustring.h"

// Token ID type - should match the public API
using extTokenId_t = int32_t;

// Simple span implementation (like std::span from C++20)
template<typename T>
class span {
public:
  span(const T* data, size_t size) : data_(data), size_(size) {}
  const T* data() const { return data_; }
  size_t size() const { return size_; }
  const T& operator[](size_t i) const { return data_[i]; }
private:
  const T* data_;
  size_t size_;
};

namespace llm_tokenizer::internal {
class BpeModel;

struct AddedToken final {
  uint32_t id_{};
  std::string token_type_;
  std::string content_;
  bool lstrip_{};
  bool normalized_{};
  bool rstrip_{};
  bool single_word_{};
  bool special_{};
};

class TokenJsonConfig;  // forward declaration

struct TokenizerDecodingState {
  std::string incomplete_utf8_;
  bool f_special_last_{};
  char signature_{};
};

using AddedTokenMap = std::unordered_map<std::u32string, AddedToken>;

constexpr std::string_view spm_escaped_space = "\xE2\x96\x81";
}  // namespace llm_tokenizer::internal
