// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.
#pragma once
#include "narrow.h"

#include <vector>
#include <set>
#include <map>
#include <string>
#include <memory>
#include <sstream>
#include <charconv>
#include <optional>

#include "unescape.hpp"
#include "trietree.hpp"

// This Trie Tree is C++ implementation of
// https://github.com/BlinkDL/ChatRWKV/blob/main/rwkv_pip_package/src/rwkv/rwkv_tokenizer.py
// Perf optimized by leveraging C++ features, but the algorithm is the same.
class RWKVTrieTree : public llm_tokenizer::internal::TrieTree<char> {
 public:
  static constexpr int kMaxTokenLength_ = 128;

  RWKVTrieTree(char ch = 0) : TrieTree(ch) {}

  // keep the same function for source code understanding.
  void add(const std::string& key, int idx = 0,
           std::optional<int> value = std::optional<int>()) {
    Add(key, idx, value);
  }

  int find_longest(const std::string& key, size_t& idx) {
    return FindLongest(key, idx);
  }
};

class TrieTokenizer {
 private:
  std::map<int, std::string> idx2token;
  RWKVTrieTree root;
  using UnescapeUtils = llm_tokenizer::internal::UnescapeUtils;

 public:
  TrieTokenizer(const std::string& text_tokens) {
    std::istringstream file(text_tokens);
    std::string line;

    while (std::getline(file, line)) {
      auto l_ws = line.find(' ');
      auto r_ws = line.rfind(' ');
      if (l_ws == std::string::npos || r_ws == std::string::npos || l_ws == r_ws) {
        ORTX_CXX_API_THROW(MakeString("[TrieTokenizer] vocab line: ", line), ORT_RUNTIME_EXCEPTION);
      }

      int idx = 0;
      std::from_chars(line.data(), line.data() + line.size(), idx);
      if (idx == 0) {
        ORTX_CXX_API_THROW(MakeString("[TrieTokenizer] bad index in vocab line: ", line), ORT_RUNTIME_EXCEPTION);
      }

      std::string raw = line.substr(line.find(' ') + 1, line.rfind(' ') - line.find(' ') - 1);
      std::string x;
      int key_length = 0;
      if (UnescapeUtils::UnquoteString(raw, x)) {
        std::from_chars(line.data() + r_ws + 1, line.data() + line.size(), key_length);
      }
      if (x.length() != key_length) {
        ORTX_CXX_API_THROW(MakeString("[TrieTokenizer] bad len in vocab line: ", line), ORT_RUNTIME_EXCEPTION);
      }

      idx2token[idx] = x;
    }

    for (const auto& kv : idx2token) {
      root.add(kv.second, 0, kv.first);
    }
  }

  std::vector<int> encodeBytes(const std::string& src) {
    size_t idx = 0;
    std::vector<int> tokens;
    while (idx < src.length()) {
      auto result = root.find_longest(src, idx);
      tokens.push_back(result);
    }

    return tokens;
  }

  std::string decodeBytes(const std::vector<int>& tokens) {
    std::string result;
    for (const auto& i : tokens) {
      result += idx2token[i];
    }
    return result;
  }
};
