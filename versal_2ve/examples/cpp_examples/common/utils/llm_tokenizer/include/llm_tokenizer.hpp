// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

/// @file llm_tokenizer.hpp
/// @brief Modern C++ API for tokenization
///
/// This header provides a clean, modern C++ interface for tokenization
/// without dependencies on the C API or ONNX Runtime.

#pragma once

#include <string>
#include <vector>
#include <memory>
#include <string_view>
#include <unordered_map>
#include <stdexcept>

#include "export.h"

namespace llm_tokenizer {

/// Token ID type
using TokenId = int32_t;

/// Exception thrown by tokenizer operations
class TOKENIZER_EXPORT TokenizerException : public std::runtime_error {
public:
    explicit TokenizerException(const std::string& message)
        : std::runtime_error(message) {}
};

/// Options for tokenizer behavior
struct TokenizerOptions {
    /// Whether to add special tokens (BOS/EOS) during encoding
    bool add_special_tokens = true;

    /// Whether to skip special tokens during decoding
    bool skip_special_tokens = true;

    /// Additional custom options as key-value pairs
    std::unordered_map<std::string, std::string> custom_options;
};

/// Main Tokenizer class
///
/// Usage:
/// @code
///   auto tokenizer = Tokenizer::FromDirectory("/path/to/tokenizer");
///   auto ids = tokenizer->Encode("Hello, world!");
///   auto text = tokenizer->Decode(ids);
/// @endcode
class TOKENIZER_EXPORT Tokenizer {
public:
    /// Create a tokenizer from a directory containing tokenizer files
    /// @param path Path to the tokenizer directory
    /// @return Unique pointer to the created tokenizer
    /// @throws TokenizerException if tokenizer cannot be loaded
    static std::unique_ptr<Tokenizer> FromDirectory(const std::string& path);

    /// Create a tokenizer from a directory with custom options
    /// @param path Path to the tokenizer directory
    /// @param options Options for tokenizer behavior
    /// @return Unique pointer to the created tokenizer
    /// @throws TokenizerException if tokenizer cannot be loaded
    static std::unique_ptr<Tokenizer> FromDirectory(
        const std::string& path,
        const std::unordered_map<std::string, std::string>& options);

    /// Tokenizers cannot be copied (move-only type)
    Tokenizer(const Tokenizer&) = delete;
    Tokenizer& operator=(const Tokenizer&) = delete;

    /// Tokenizers can be moved
    Tokenizer(Tokenizer&&) noexcept = default;
    Tokenizer& operator=(Tokenizer&&) noexcept = default;

    /// Virtual destructor
    virtual ~Tokenizer();

    // ===== Encoding API =====

    /// Encode a single text string to token IDs
    /// @param text Input text to encode
    /// @param add_special_tokens Whether to add special tokens (default: true)
    /// @return Vector of token IDs
    /// @throws TokenizerException on encoding error
    std::vector<TokenId> Encode(
        std::string_view text,
        bool add_special_tokens = true);

    /// Encode multiple text strings to token IDs (batch operation)
    /// @param texts Vector of input texts
    /// @param add_special_tokens Whether to add special tokens (default: true)
    /// @return Vector of token ID vectors (one per input text)
    /// @throws TokenizerException on encoding error
    std::vector<std::vector<TokenId>> EncodeBatch(
        const std::vector<std::string_view>& texts,
        bool add_special_tokens = true);

    // ===== Decoding API =====

    /// Decode token IDs back to text
    /// @param ids Vector of token IDs
    /// @param skip_special_tokens Whether to skip special tokens (default: true)
    /// @return Decoded text string
    /// @throws TokenizerException on decoding error
    std::string Decode(
        const std::vector<TokenId>& ids,
        bool skip_special_tokens = true);

    /// Decode multiple sequences of token IDs to text (batch operation)
    /// @param ids Vector of token ID vectors
    /// @param skip_special_tokens Whether to skip special tokens (default: true)
    /// @return Vector of decoded text strings
    /// @throws TokenizerException on decoding error
    std::vector<std::string> DecodeBatch(
        const std::vector<std::vector<TokenId>>& ids,
        bool skip_special_tokens = true);

    // ===== Token/ID Conversion =====

    /// Convert a token string to its token ID
    /// @param token Token string
    /// @return Token ID
    /// @throws TokenizerException if token not found
    TokenId TokenToId(const std::string& token);

    /// Convert a token ID to its string representation
    /// @param id Token ID
    /// @return Token string
    /// @throws TokenizerException if ID is invalid
    std::string IdToToken(TokenId id);

    // ===== Configuration =====

    /// Update tokenizer options
    /// @param options Map of option key-value pairs
    /// @throws TokenizerException if options are invalid
    void SetOptions(const std::unordered_map<std::string, std::string>& options);

    // ===== Metadata =====

    /// Get the vocabulary size
    /// @return Number of tokens in vocabulary
    size_t VocabSize() const;

    /// Get the BOS (beginning of sequence) token
    /// @return BOS token string (empty if not defined)
    std::string GetBosToken() const;

    /// Get the EOS (end of sequence) token
    /// @return EOS token string (empty if not defined)
    std::string GetEosToken() const;

    /// Get the PAD token
    /// @return PAD token string (empty if not defined)
    std::string GetPadToken() const;

    /// Get the UNK (unknown) token
    /// @return UNK token string (empty if not defined)
    std::string GetUnkToken() const;

private:
    /// PIMPL pattern to hide implementation details
    class Impl;
    std::unique_ptr<Impl> impl_;

    /// Private constructor (use factory methods)
    Tokenizer();
};

/// Version information
namespace version {
    constexpr int MAJOR = 0;
    constexpr int MINOR = 1;
    constexpr int PATCH = 0;

    /// Get version string
    /// @return Version string in format "major.minor.patch"
    inline std::string GetVersionString() {
        return std::to_string(MAJOR) + "." +
               std::to_string(MINOR) + "." +
               std::to_string(PATCH);
    }
}

} // namespace llm_tokenizer
