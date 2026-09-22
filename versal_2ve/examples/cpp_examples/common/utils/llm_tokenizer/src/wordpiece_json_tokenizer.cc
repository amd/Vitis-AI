// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include "wordpiece_json_tokenizer.hpp"
#include "nlohmann/json.hpp"
#include <fstream>
#include <sstream>

namespace llm_tokenizer::internal {

using json = nlohmann::json;

OrtxStatus WordPieceJsonTokenizer::Load(const TokenJsonConfig& config) {
  // Open and parse tokenizer.json
  std::unique_ptr<std::istream> vocab_stream;
  auto status = config.OpenVocabFile(vocab_stream);
  if (!status.IsOk()) {
    return status;
  }

  json tok_json = json::parse(*vocab_stream, nullptr, false, true);
  if (tok_json.is_discarded()) {
    return OrtxStatus(extError_t::kOrtxErrorInvalidArgument, "Failed to parse tokenizer.json");
  }

  // Get model node
  auto model_node = tok_json.find("model");
  if (model_node == tok_json.end()) {
    return OrtxStatus(extError_t::kOrtxErrorInvalidArgument, "Model node not found in tokenizer.json");
  }

  // Get vocab
  auto vocab_node = model_node->find("vocab");
  if (vocab_node == model_node->end()) {
    return OrtxStatus(extError_t::kOrtxErrorInvalidArgument, "Vocabulary not found in model node");
  }

  // Build vocab string in BERT format (one token per line)
  std::ostringstream vocab_stream_builder;

  // WordPiece vocab is a map {token: id}, need to sort by id
  std::vector<std::pair<std::string, int>> vocab_items;
  for (auto& item : vocab_node->items()) {
    vocab_items.push_back({item.key(), item.value().get<int>()});
  }

  // Sort by ID
  std::sort(vocab_items.begin(), vocab_items.end(),
            [](const auto& a, const auto& b) { return a.second < b.second; });

  // Build vocab string
  for (const auto& [token, id] : vocab_items) {
    vocab_stream_builder << token << "\n";
  }

  vocab_data_ = vocab_stream_builder.str();

  // Get configuration parameters
  std::string unk_token = config.unk_token_.empty() ? "[UNK]" : config.unk_token_;
  std::string sep_token = "[SEP]";
  std::string pad_token = "[PAD]";
  std::string cls_token = "[CLS]";
  std::string mask_token = "[MASK]";

  // Get from added_tokens if available
  auto added_tokens = tok_json.find("added_tokens");
  if (added_tokens != tok_json.end()) {
    for (const auto& token : *added_tokens) {
      auto content = token.value("content", "");
      if (content == "[SEP]") sep_token = content;
      if (content == "[PAD]") pad_token = content;
      if (content == "[CLS]") cls_token = content;
      if (content == "[MASK]") mask_token = content;
    }
  }

  // Get continuing_subword_prefix (default "##")
  std::string suffix_indicator = "##";
  auto continuing_prefix = model_node->find("continuing_subword_prefix");
  if (continuing_prefix != model_node->end()) {
    suffix_indicator = continuing_prefix->get<std::string>();
  }

  // Get do_lower_case from tokenizer_config.json or default to false
  // Note: These options are not currently exposed in TokenJsonConfig
  // Will default to sensible values for BERT
  bool do_lower_case = false;
  bool do_basic_tokenize = true;
  bool tokenize_chinese_chars = true;
  bool strip_accents = false;

  // Max length
  int32_t max_len = 512;  // BERT default
  if (config.model_max_length_ > 0) {
    max_len = config.model_max_length_;
  }

  std::string truncation_strategy = "longest_first";

  // Create BERT tokenizer
  tokenizer_ = std::make_unique<BertTokenizer>(
      vocab_data_,
      do_lower_case,
      do_basic_tokenize,
      ustring(unk_token),
      ustring(sep_token),
      ustring(pad_token),
      ustring(cls_token),
      ustring(mask_token),
      tokenize_chinese_chars,
      strip_accents,
      ustring(suffix_indicator),
      max_len,
      truncation_strategy
  );

  return {};
}

OrtxStatus WordPieceJsonTokenizer::ComputeNoOp(const std::string& input,
                                                std::vector<extTokenId_t>& output,
                                                bool add_special_tokens) const {
  if (!tokenizer_) {
    return OrtxStatus(extError_t::kOrtxErrorInvalidArgument, "Tokenizer not loaded");
  }

  // Tokenize
  std::list<BertTokenizer::OffsetMappingType> offset_map;
  auto tokens = tokenizer_->Tokenize(ustring(input), offset_map, false);

  // Encode
  auto ids = tokenizer_->Encode(tokens);

  // Add special tokens if requested
  if (add_special_tokens) {
    ids = tokenizer_->AddSpecialToken(ids);
  }

  // Convert to output format
  output.clear();
  output.reserve(ids.size());
  for (auto id : ids) {
    output.push_back(static_cast<extTokenId_t>(id));
  }

  return {};
}

extTokenId_t WordPieceJsonTokenizer::GetTokenId([[maybe_unused]] const std::string& token) const {
  // For now, not implemented - would need to expose vocab lookup
  return 0;
}

// Decoder implementation
OrtxStatus WordPieceDecoder::Load(const std::shared_ptr<TokenJsonConfig>& config,
                                   [[maybe_unused]] const WordPieceJsonTokenizer& tokenizer) {
  // Open and parse tokenizer.json
  std::unique_ptr<std::istream> vocab_stream;
  auto status = config->OpenVocabFile(vocab_stream);
  if (!status.IsOk()) {
    return status;
  }

  json tok_json = json::parse(*vocab_stream, nullptr, false, true);
  if (tok_json.is_discarded()) {
    return OrtxStatus(extError_t::kOrtxErrorInvalidArgument, "Failed to parse tokenizer.json");
  }

  // Get vocab
  auto model_node = tok_json.find("model");
  if (model_node == tok_json.end()) {
    return OrtxStatus(extError_t::kOrtxErrorInvalidArgument, "Model node not found");
  }

  auto vocab_node = model_node->find("vocab");
  if (vocab_node == model_node->end()) {
    return OrtxStatus(extError_t::kOrtxErrorInvalidArgument, "Vocabulary not found");
  }

  // Build id->token mapping
  size_t vocab_size = vocab_node->size();
  id_to_token_.resize(vocab_size);

  for (auto& item : vocab_node->items()) {
    std::string token = item.key();
    int id = item.value().get<int>();
    if (id >= 0 && id < static_cast<int>(vocab_size)) {
      id_to_token_[id] = token;
    }
  }

  // Get continuing subword prefix
  auto continuing_prefix = model_node->find("continuing_subword_prefix");
  if (continuing_prefix != model_node->end()) {
    continuing_subword_prefix_ = continuing_prefix->get<std::string>();
  }

  // Get special token IDs
  auto added_tokens = tok_json.find("added_tokens");
  if (added_tokens != tok_json.end()) {
    for (const auto& token : *added_tokens) {
      if (token.contains("id") && token.contains("special")) {
        if (token["special"].get<bool>()) {
          special_token_ids_.insert(token["id"].get<int>());
        }
      }
    }
  }

  return {};
}

OrtxStatus WordPieceDecoder::Id2Token(extTokenId_t id, std::string& token,
                                       TokenizerDecodingState** state,
                                       bool skip_special_tokens) const {
  // Check if we should skip special tokens
  if (skip_special_tokens && special_token_ids_.count(id) > 0) {
    token = "";
    return {};
  }

  // Get token string
  if (id < 0 || id >= static_cast<extTokenId_t>(id_to_token_.size())) {
    return OrtxStatus(extError_t::kOrtxErrorInvalidArgument, "Token ID out of range");
  }

  std::string token_str = id_to_token_[id];

  // Remove continuing subword prefix if present
  if (token_str.find(continuing_subword_prefix_) == 0) {
    token = token_str.substr(continuing_subword_prefix_.length());
  } else {
    // Add space before token unless it's a continuation
    // For the first token, no space needed
    if (state && *state) {
      token = " " + token_str;
    } else {
      token = token_str;
    }
  }

  // Update state (used to track if we're past first token)
  if (state) {
    if (!*state) {
      *state = new TokenizerDecodingState();
    }
  }

  return {};
}

}  // namespace llm_tokenizer::internal
