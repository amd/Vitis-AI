// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#pragma once

#include <string>
#include <vector>
#include <locale>
#include <codecvt>
#include <set>
#include <map>
#include <unordered_map>
#include <algorithm>
#include <sstream>

#include "ustring.h"
#include "narrow.h"
#include "tokenizer_common.h"

struct KernelBpeDecoder {
 public:
  virtual ~KernelBpeDecoder() = default;


  std::unordered_map<int64_t, std::string> ParseId2String(const std::string& s_attr) {
    std::unordered_map<int64_t, std::string> result;
    result.reserve(s_attr.size() / 4);
    std::stringstream ss(s_attr);

    std::string line;
    std::string token;
    while (std::getline(ss, line, '\n')) {
      size_t pos_end = 0;
      int64_t v = std::stoll(line, &pos_end);
      if (pos_end >= line.size() || line[pos_end] != '\t') {
        token.clear();
      } else {
        token = line.substr(pos_end + 1);
      }
      result.emplace(v, token);
    }

    return result;
  }

  void BuildIdVocab(const std::string& vocab) {
    arr_vocab_.reserve(vocab.size() / 2);  // give a rough estimation.

    std::string_view v_vocab(vocab);
    size_t last_pos = 0;

    auto c_count = v_vocab.size();
    for (size_t n = 0; n < c_count; ++n) {
      if (v_vocab[n] == '\n') {
        std::string_view s_tok = v_vocab.substr(last_pos, n - last_pos);
        arr_vocab_.emplace_back(std::string(s_tok));
        last_pos = n + 1;
      } else if (n == c_count - 1) {
        std::string_view s_tok = v_vocab.substr(last_pos, n - last_pos + 1);
        arr_vocab_.emplace_back(std::string(s_tok));
      }
    }

    arr_vocab_.shrink_to_fit();
  }

  static bool IsSpmByteWord(std::string_view word) {
    return word.size() == 6 && word[0] == '<' && word[1] == '0' && word[2] == 'x' && word[5] == '>';
  }

  static std::string ReplaceAll(std::string_view s, const std::string& search, const std::string& replace) {
    std::string result;
    for (size_t pos = 0;; pos += search.length()) {
      auto new_pos = s.find(search, pos);
      if (new_pos == std::string::npos) {
        result += s.substr(pos, s.size() - pos);
        break;
      }
      result += s.substr(pos, new_pos - pos);
      result += replace;
      pos = new_pos;
    }

    return result;
  }

 protected:
  std::vector<std::string> arr_vocab_;
  std::map<char32_t, unsigned char> byte_decoder_;
  std::map<int64_t, std::string> added_tokens_;
  std::set<int64_t> all_special_ids_;

  std::string bos_token_;
  std::string eos_token_;
  std::string unk_token_;
  int skip_special_tokens_{};
  int whitespace_token_{};
  int en_normalization_{};
};
