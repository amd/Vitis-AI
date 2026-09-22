// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#pragma once

#include "bert_tokenizer.hpp"
#include "tokenizer_jsconfig.hpp"
#include "ext_status.h"
#include <vector>
#include <string>
#include <memory>
#include <set>

namespace llm_tokenizer::internal {

// Adapter class to load BERT/WordPiece tokenizer from tokenizer.json format
class WordPieceJsonTokenizer {
 public:
  WordPieceJsonTokenizer() = default;

  OrtxStatus Load(const TokenJsonConfig& config);

  OrtxStatus ComputeNoOp(const std::string& input, std::vector<extTokenId_t>& output,
                         bool add_special_tokens = true) const;

  extTokenId_t GetTokenId(const std::string& token) const;

 private:
  std::unique_ptr<BertTokenizer> tokenizer_;
  std::string vocab_data_;  // Keep vocab data alive
};

// Decoder for BERT/WordPiece tokens
class WordPieceDecoder {
 public:
  OrtxStatus Load(const std::shared_ptr<TokenJsonConfig>& config,
                  const WordPieceJsonTokenizer& tokenizer);

  OrtxStatus Id2Token(extTokenId_t id, std::string& token,
                      TokenizerDecodingState** state,
                      bool skip_special_tokens = true) const;

 private:
  std::vector<std::string> id_to_token_;
  std::string continuing_subword_prefix_ = "##";
  std::set<extTokenId_t> special_token_ids_;
};

}  // namespace llm_tokenizer::internal
