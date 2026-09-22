// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include "llm_tokenizer.hpp"
#include "tokenizer_impl.h"

// NOTE: This file will not compile until missing onnxruntime-extensions
// dependencies are resolved. Missing headers include:
// - ext_status.h (for OrtxStatus error handling)
// - minja.hpp (for chat template processing)
// - ustring.h, string_utils.h, string_tensor.h (for string operations)
// - And others - see MISSING_DEPENDENCIES.md for complete list

namespace llm_tokenizer {

// PIMPL implementation class
class Tokenizer::Impl {
public:
    // The actual tokenizer implementation from onnxruntime-extensions
    llm_tokenizer::internal::TokenizerImpl tokenizer_impl;
};

// Constructor
Tokenizer::Tokenizer() : impl_(std::make_unique<Impl>()) {}

// Destructor (must be in .cc file for unique_ptr<Impl>)
Tokenizer::~Tokenizer() = default;

// Factory method: Create from directory
std::unique_ptr<Tokenizer> Tokenizer::FromDirectory(const std::string& path) {
    auto tokenizer = std::unique_ptr<Tokenizer>(new Tokenizer());

    // Load the tokenizer from the directory
    auto status = tokenizer->impl_->tokenizer_impl.Load(path);

    if (!status.IsOk()) {
        throw TokenizerException("Failed to load tokenizer from '" + path +
                                 "': " + status.Message());
    }

    return tokenizer;
}

// Factory method: Create from directory with options
std::unique_ptr<Tokenizer> Tokenizer::FromDirectory(
    const std::string& path,
    const std::unordered_map<std::string, std::string>& options) {

    auto tokenizer = FromDirectory(path);

    // Update with custom options
    auto status = tokenizer->impl_->tokenizer_impl.UpdateOptions(options);

    if (!status.IsOk()) {
        throw TokenizerException(std::string("Failed to set tokenizer options: ") +
                                 status.Message());
    }

    return tokenizer;
}

// Encode single text
std::vector<TokenId> Tokenizer::Encode(std::string_view text,
                                        bool add_special_tokens) {
    std::vector<std::string_view> input = {text};
    std::vector<std::vector<TokenId>> result;

    auto status = impl_->tokenizer_impl.Tokenize(input, result, add_special_tokens);

    if (!status.IsOk()) {
        throw TokenizerException(std::string("Failed to encode text: ") + status.Message());
    }

    if (result.empty()) {
        return {};
    }

    return std::move(result[0]);
}

// Encode batch
std::vector<std::vector<TokenId>> Tokenizer::EncodeBatch(
    const std::vector<std::string_view>& texts,
    bool add_special_tokens) {

    std::vector<std::vector<TokenId>> result;

    auto status = impl_->tokenizer_impl.Tokenize(texts, result, add_special_tokens);

    if (!status.IsOk()) {
        throw TokenizerException(std::string("Failed to encode batch: ") + status.Message());
    }

    return result;
}

// Decode single sequence
std::string Tokenizer::Decode(const std::vector<TokenId>& ids,
                               bool skip_special_tokens) {
    // Create a span-like view for the internal API
    std::vector<span<TokenId const>> id_spans;
    id_spans.push_back(span<TokenId const>(ids.data(), ids.size()));

    std::vector<std::string> result;

    auto status = impl_->tokenizer_impl.Detokenize(id_spans, result, skip_special_tokens);

    if (!status.IsOk()) {
        throw TokenizerException(std::string("Failed to decode tokens: ") + status.Message());
    }

    if (result.empty()) {
        return "";
    }

    return std::move(result[0]);
}

// Decode batch
std::vector<std::string> Tokenizer::DecodeBatch(
    const std::vector<std::vector<TokenId>>& ids,
    bool skip_special_tokens) {

    // Convert to span views
    std::vector<span<TokenId const>> id_spans;
    id_spans.reserve(ids.size());

    for (const auto& id_vec : ids) {
        id_spans.push_back(span<TokenId const>(id_vec.data(), id_vec.size()));
    }

    std::vector<std::string> result;

    auto status = impl_->tokenizer_impl.Detokenize(id_spans, result, skip_special_tokens);

    if (!status.IsOk()) {
        throw TokenizerException(std::string("Failed to decode batch: ") + status.Message());
    }

    return result;
}

// Token to ID conversion
TokenId Tokenizer::TokenToId(const std::string& token) {
    TokenId id;

    auto status = impl_->tokenizer_impl.Token2Id(token, id);

    if (!status.IsOk()) {
        throw TokenizerException("Failed to convert token '" + token +
                                 "' to ID: " + status.Message());
    }

    return id;
}

// ID to token conversion
std::string Tokenizer::IdToToken(TokenId id) {
    std::string token;
    std::unique_ptr<llm_tokenizer::internal::TokenizerDecodingState> cache;

    auto status = impl_->tokenizer_impl.Id2Token(id, token, cache, false);

    if (!status.IsOk()) {
        throw TokenizerException("Failed to convert ID " + std::to_string(id) +
                                 " to token: " + status.Message());
    }

    return token;
}

// Set options
void Tokenizer::SetOptions(const std::unordered_map<std::string, std::string>& options) {
    auto status = impl_->tokenizer_impl.UpdateOptions(options);

    if (!status.IsOk()) {
        throw TokenizerException(std::string("Failed to update options: ") + status.Message());
    }
}

// Get vocabulary size
size_t Tokenizer::VocabSize() const {
    // This would need to be implemented in TokenizerImpl
    // For now, throw as not implemented
    throw TokenizerException("VocabSize() not yet implemented");
}

// Get BOS token
std::string Tokenizer::GetBosToken() const {
    return impl_->tokenizer_impl.bos_token;
}

// Get EOS token
std::string Tokenizer::GetEosToken() const {
    return impl_->tokenizer_impl.eos_token;
}

// Get PAD token
std::string Tokenizer::GetPadToken() const {
    // Would need to be added to TokenizerImpl
    throw TokenizerException("GetPadToken() not yet implemented");
}

// Get UNK token
std::string Tokenizer::GetUnkToken() const {
    // Would need to be added to TokenizerImpl
    throw TokenizerException("GetUnkToken() not yet implemented");
}

} // namespace llm_tokenizer
