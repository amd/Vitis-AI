// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#pragma once

// Export macro for public API symbols.
// All other symbols default to hidden visibility.
#if defined(_WIN32)
  #if defined(LLM_TOKENIZER_BUILD)
    #define TOKENIZER_EXPORT __declspec(dllexport)
  #else
    #define TOKENIZER_EXPORT __declspec(dllimport)
  #endif
#else
  #if defined(LLM_TOKENIZER_BUILD)
    #define TOKENIZER_EXPORT __attribute__((visibility("default")))
  #else
    #define TOKENIZER_EXPORT
  #endif
#endif
