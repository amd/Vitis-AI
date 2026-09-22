// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include "bpe_kernels.h"
#include "bpe_tokenizer_model.hpp"
#include "ugm_kernels.hpp"
#include "wordpiece_json_tokenizer.hpp"

#include "tokenizer_impl.h"

namespace llm_tokenizer::internal {

TokenizerImpl::TokenizerImpl() = default;
TokenizerImpl::~TokenizerImpl() = default;

OrtxStatus TokenizerImpl::LoadTokenizer() {

  auto type = TokenJsonConfig::GetTokenType(tok_config_->tokenizer_class_);
  if (type == TokenType::kUnigram) {
    auto tokenizer = std::make_unique<SpmUgmTokenizer>();
    auto status = tokenizer->Load(*tok_config_);
    if (!status.IsOk()) {
      return status;
    }
    auto detok = std::make_unique<SpmUgmDecoder>();

    if (status.IsOk()) {
      status = detok->Load(*tok_config_, *tokenizer);
    }

    if (status.IsOk()) {
      tokenizer_ = std::move(tokenizer);
      detokenizer_ = std::move(detok);
    }
    return status;
  } else if (type == TokenType::kBPE) {
    auto tokenizer = std::make_unique<JsonFastTokenizer>();
    auto fx_load = &JsonFastTokenizer::Load;

    std::filesystem::path vocab_file_path(tok_config_->GetVocabDataFile());
    // vocab file is checked in TokenJsonConfig::Load
    if (vocab_file_path.extension() != ".json") {
      fx_load = &JsonFastTokenizer::LoadTikTokenBase64;
    }

    auto status = (tokenizer.get()->*fx_load)(*tok_config_);
    if (!status.IsOk()) {
      return status;
    }

    auto detok = std::make_unique<BpeStreamingDecoder>();
    status = detok->Load(tok_config_, *tokenizer);
    if (!status.IsOk()) {
      return status;
    }

    status = LoadChatTemplate();
    if (!status.IsOk()) {
      return status;
    }

    if (status.IsOk()) {
      tokenizer_ = std::move(tokenizer);
      detokenizer_ = std::move(detok);
    }

    return status;
  } else if (type == TokenType::kWordPiece) {
    auto tokenizer = std::make_unique<WordPieceJsonTokenizer>();
    auto status = tokenizer->Load(*tok_config_);
    if (!status.IsOk()) {
      return status;
    }

    auto detok = std::make_unique<WordPieceDecoder>();
    status = detok->Load(tok_config_, *tokenizer);
    if (!status.IsOk()) {
      return status;
    }

    if (status.IsOk()) {
      tokenizer_ = std::move(tokenizer);
      detokenizer_ = std::move(detok);
    }

    return status;
  }

  return OrtxStatus(kOrtxErrorNotImplemented, "Unsupported tokenizer class: " + tok_config_->tokenizer_class_);
}

// Blob loading removed - C++ API uses file-based loading only

OrtxStatus TokenizerImpl::Load(const std::string& tok_path) {
  tok_config_ = std::make_shared<llm_tokenizer::internal::TokenJsonConfig>();
  auto status = tok_config_->Load(tok_path);
  if (!status.IsOk()) {
    return status;
  }

  return LoadTokenizer();
}

OrtxStatus TokenizerImpl::UpdateOptions(const std::unordered_map<std::string, std::string>& options) {
  if (options.empty()) {
    return OrtxStatus(kOrtxErrorInvalidArgument, "No options provided.");
  }

  for (const auto& [k, v] : options) {
    if (k.empty()) {
      return OrtxStatus(kOrtxErrorInvalidArgument, "Option key cannot be empty.");
    }

    if (v.empty()) {
      return OrtxStatus(kOrtxErrorInvalidArgument, "Option value cannot be empty for key: " + k);
    }

    // Insert new or update existing KV pair
    options_map[k] = v;
  }

  return OrtxStatus(kOrtxOK, "Tokenizer options updated successfully.");
}

OrtxStatus TokenizerImpl::BatchEncode(const std::vector<std::string_view>& input,
                                      std::vector<std::vector<extTokenId_t>>& t_ids,
                                      bool add_special_tokens) const {
  for (const auto& s : input) {
    std::vector<extTokenId_t> ids;

    OrtxStatus status = std::visit([&](auto& tokenizer) {
      return tokenizer->ComputeNoOp(std::string(s), ids, add_special_tokens);
    }, tokenizer_);

    if (!status.IsOk()) {
      return status;
    }

    t_ids.emplace_back(std::move(ids));
  }

  return {};
}

OrtxStatus TokenizerImpl::BatchDecode(const std::vector<span<extTokenId_t const>>& t_ids,
                                      std::vector<std::string>& t_text, bool skip_special_tokens) const {
  for (const auto& ids_span : t_ids) {
    std::string text;
    TokenizerDecodingState* state = nullptr;

    OrtxStatus status = std::visit([&](auto& detokenizer) {
      for (size_t i = 0; i < ids_span.size(); ++i) {
        std::string token;
        auto s = detokenizer->Id2Token(ids_span[i], token, &state, skip_special_tokens);
        if (!s.IsOk()) return s;
        text += token;
      }
      return OrtxStatus();
    }, detokenizer_);

    if (!status.IsOk()) {
      delete state;
      return status;
    }
    delete state;
    t_text.push_back(std::move(text));
  }
  return {};
}

OrtxStatus TokenizerImpl::Id2Token(extTokenId_t id, std::string& token, TokenizerDecodingState** state, bool skip_special_tokens = true) const {
  return std::visit([&](auto& detokenizer) {
    return detokenizer->Id2Token(id, token, state, skip_special_tokens); }, detokenizer_);
}

static std::map<std::string, std::string> LANGUAGES = {
    {"en", "english"},        {"zh", "chinese"},       {"de", "german"},    {"es", "spanish"},    {"ru", "russian"},
    {"ko", "korean"},         {"fr", "french"},        {"ja", "japanese"},  {"pt", "portuguese"}, {"tr", "turkish"},
    {"pl", "polish"},         {"ca", "catalan"},       {"nl", "dutch"},     {"ar", "arabic"},     {"sv", "swedish"},
    {"it", "italian"},        {"id", "indonesian"},    {"hi", "hindi"},     {"fi", "finnish"},    {"vi", "vietnamese"},
    {"he", "hebrew"},         {"uk", "ukrainian"},     {"el", "greek"},     {"ms", "malay"},      {"cs", "czech"},
    {"ro", "romanian"},       {"da", "danish"},        {"hu", "hungarian"}, {"ta", "tamil"},      {"no", "norwegian"},
    {"th", "thai"},           {"ur", "urdu"},          {"hr", "croatian"},  {"bg", "bulgarian"},  {"lt", "lithuanian"},
    {"la", "latin"},          {"mi", "maori"},         {"ml", "malayalam"}, {"cy", "welsh"},      {"sk", "slovak"},
    {"te", "telugu"},         {"fa", "persian"},       {"lv", "latvian"},   {"bn", "bangla"},     {"sr", "serbian"},
    {"az", "azerbaijani"},    {"sl", "slovenian"},     {"kn", "kannada"},   {"et", "estonian"},   {"mk", "macedonian"},
    {"br", "breton"},         {"eu", "basque"},        {"is", "icelandic"}, {"hy", "armenian"},   {"ne", "nepali"},
    {"mn", "mongolian"},      {"bs", "bosnian"},       {"kk", "kazakh"},    {"sq", "albanian"},   {"sw", "swahili"},
    {"gl", "galician"},       {"mr", "marathi"},       {"pa", "punjabi"},   {"si", "sinhala"},    {"km", "khmer"},
    {"sn", "shona"},          {"yo", "yoruba"},        {"so", "somali"},    {"af", "afrikaans"},  {"oc", "occitan"},
    {"ka", "georgian"},       {"be", "belarusian"},    {"tg", "tajik"},     {"sd", "sindhi"},     {"gu", "gujarati"},
    {"am", "amharic"},        {"yi", "yiddish"},       {"lo", "lao"},       {"uz", "uzbek"},      {"fo", "faroese"},
    {"ht", "haitian creole"}, {"ps", "pashto"},        {"tk", "turkmen"},   {"nn", "nynorsk"},    {"mt", "maltese"},
    {"sa", "sanskrit"},       {"lb", "luxembourgish"}, {"my", "myanmar"},   {"bo", "tibetan"},    {"tl", "tagalog"},
    {"mg", "malagasy"},       {"as", "assamese"},      {"tt", "tatar"},     {"haw", "hawaiian"},  {"ln", "lingala"},
    {"ha", "hausa"},          {"ba", "bashkir"},       {"jw", "javanese"},  {"su", "sundanese"},  {"yue", "cantonese"}};

OrtxStatus TokenizerImpl::GetDecoderPromptIds(size_t batch_size, const char* lang, const char* task, int no_timestamps,
                                              std::vector<std::vector<extTokenId_t>>& t_ids) const {
  // since it was only supported by Whisper model, which is bpe only.
  if (!std::holds_alternative<bpe_tokenizer_t>(tokenizer_)) {
    return OrtxStatus(kOrtxErrorInvalidArgument, "Tokenizer is not loaded");
  }

  auto translate_token_id = std::get<bpe_tokenizer_t>(tokenizer_)->GetTokenId("<|translate|>");
  auto transcribe_token_id = std::get<bpe_tokenizer_t>(tokenizer_)->GetTokenId("<|transcribe|>");
  auto notimestamps_token_id = std::get<bpe_tokenizer_t>(tokenizer_)->GetTokenId("<|notimestamps|>");
  std::vector<extTokenId_t> ids;
  ids.reserve(4);
  if (lang != nullptr) {
    auto lang_str = LANGUAGES.find(lang);
    if (lang_str == LANGUAGES.end()) {
      return OrtxStatus(kOrtxErrorInvalidArgument, "Invalid language");
    }

    std::string lang_token = "<|" + lang_str->first + "|>";
    ids.push_back(std::get<bpe_tokenizer_t>(tokenizer_)->GetTokenId(lang_token));
  }

  if (task != nullptr) {
    if (0 == strcmp(task, "translate")) {
      ids.push_back(translate_token_id);
    } else if (0 == strcmp(task, "transcribe")) {
      ids.push_back(transcribe_token_id);
    } else {
      return OrtxStatus(kOrtxErrorInvalidArgument, "Invalid task");
    }
  }

  if (no_timestamps) {
    ids.push_back(notimestamps_token_id);
  }

  t_ids.resize(batch_size, ids);
  return {};
}

OrtxStatus TokenizerImpl::LoadChatTemplate() {
  // Load the chat template from the tokenizer configuration
  chat_template = (tok_config_->tokenizer_class_ == "WhisperTokenizer") ? R"({{ messages | map(attribute='content') | join('\n') }})" : tok_config_->chat_template_;
  if (chat_template.size()) {
    try {
      chat_template_root_ = minja::Parser::parse(chat_template, {});
    } catch (const std::runtime_error&) {
      return OrtxStatus(kOrtxOK, "Warning: The chat template for this model is not yet supported, trying to apply chat template will cause an error.");
    }
  }

  return OrtxStatus(kOrtxOK, "Loaded chat template.");
}

}  // namespace llm_tokenizer::internal
