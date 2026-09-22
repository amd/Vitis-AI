/******************************************************************************
 * Copyright (C) 2026 Advanced Micro Devices, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 ******************************************************************************/
#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <vart/vart_llm_runner_factory.hpp>
#include <vart/vart_npu_tensor.hpp>
#include "llm_sampler.hpp"
#include "common/app_logger.hpp"
#include "llm_tokenizer.hpp"

using llm_sampler::ISampler;
using llm_sampler::SamplerParams;

/**
 * @brief Prompt template style.
 *
 *   Format   Model / Model family : Template
 *   -------- -------------------- : --------------------------------------
 *   ChatML   Qwen2.5              : <|im_start|>system .. user .. assistant
 *   QA       base LM              : "Q: {p}\nA:"
 *   DeepSeek R1-Distill           : <｜User｜>{p}<｜Assistant｜>
 *   Llama3   Instruct             : <|begin_of_text|> + header blocks
 *   Olmo     OLMo SFT             : "<|user|>\n{p}\n<|assistant|>\n"
 *   Raw      -                    : no templating (prompt fed verbatim)
 */
enum class PromptFormat { ChatML, QA, DeepSeek, Llama3, Olmo, Raw };

/**
 * @brief Performance statistics for a generation run.
 */
struct GenerationStats {
  /* Time spent in the prefill phase, in milliseconds */
  double prefillTimeMs = 0.0;
  /* Number of prompt tokens ingested during prefill */
  int    prefillTokens = 0;
  /* Time spent in the decode loop, in milliseconds */
  double decodeTimeMs  = 0.0;
  /* Number of tokens generated during decode */
  int    decodeTokens  = 0;

  /** @brief Prefill throughput in tokens/second (0 when not timed). */
  double GetPrefillTokensPerSecond() const {
    return prefillTimeMs <= 0 ? 0.0 : (prefillTokens * 1000.0) / prefillTimeMs;
  }
  /** @brief Decode throughput in tokens/second (0 when not timed). */
  double GetDecodeTokensPerSecond() const {
    return decodeTimeMs <= 0 ? 0.0 : (decodeTokens * 1000.0) / decodeTimeMs;
  }
};

/**
 * @brief Application context that drives LLM generation on the vart-ml public
 *        API (vart::LlmRunner).
 *
 * Owns the configuration, the tokenizer, the sampler, and the underlying
 * vart::LlmRunner, and orchestrates prompt formatting, tokenization, the
 * prefill/decode loop, and detokenization.
 */
class AppContext {
 public:
  /** @brief Construct an empty context; call CreateLlmRunner() before Run(). */
  AppContext();
  /** @brief Destroy the context and release the runner and its tensors. */
  ~AppContext();

  /**
   * @brief Load the model bundle from a directory: creates the vart::LlmRunner
   *        via vart::LlmRunnerFactory and reads LLM metadata (vocab size, EOS
   *        ids, context length) from vart::LlmRunner::get_llm_config().
   */
  bool CreateLlmRunner(const std::string& modelPath);

  /** @brief Set the tokenizer */
  bool SetTokenizer(std::unique_ptr<llm_tokenizer::Tokenizer>&& tokenizer);

  /**
   * @brief Set the sampler.
   * When no sampler is set the runner falls back to greedy argmax decoding.
   */
  bool SetSampler(std::unique_ptr<ISampler>&& sampler);

  /** @brief Select the chat prompt template applied to each prompt. */
  void SetPromptFormat(PromptFormat fmt) { m_promptFormat = fmt; }
  /** @brief Set the system prompt used by the chat templates. */
  void SetSystemPrompt(const std::string& sys) { m_systemPrompt = sys; }
  /** @brief Set the repetition-penalty factor forwarded to the sampler (1.0 = disabled). */
  void SetRepetitionPenalty(float penalty) { m_repPenalty = penalty; }
  /** @brief Ban repeating n-grams of this size. */
  void SetNoRepeatNgram(int n) { m_noRepeatNgram = n; }

  /** @brief Enable AI Engine column sharing. */
  void SetAieColumnsSharing(bool on) { m_aieColumnsSharing = on; }
  /** @brief Set the NPU start column. */
  void SetStartColumn(int col) { m_startColumn = col; }
  /** @brief Enable AI Analyzer profiling. */
  void SetAiAnalyzerProfiling(bool on) { m_aiAnalyzerProfiling = on; }

  /**
   * @brief Set the compiled token-id input tensor name. Resolved in
   *        CreateLlmRunner() against the input-ids name the runner exposes.
   */
  void SetInputIdsName(const std::string& name) { m_inputIdsName = name; }
  /** @brief Set the compiled attention-mask input tensor name. */
  void SetAttentionMaskName(const std::string& name) { m_attentionMaskName = name; }
  /** @brief Set the compiled logits output tensor name. */
  void SetLogitsName(const std::string& name) { m_logitsName = name; }

  /**
   * @brief Set the input tensor exchange type: ("HW" | "CPU")
   */
  void SetInputTensorType(const std::string& type) { m_inputTensorType = ToTensorType(type); }
  /**
   * @brief Set the output tensor exchange type ("HW" | "CPU")
   */
  void SetOutputTensorType(const std::string& type) { m_outputTensorType = ToTensorType(type); }

  /**
   * @brief Set per-tensor exchange-type overrides, keyed by the create_runner
   *        option name ("input_tensor_type.<tensor>" / "output_tensor_type.<tensor>")
   *        with an "HW" | "CPU" value. Only tensors that differ from the global
   *        input/output type need to be listed.
   */
  void SetTensorTypeOverrides(std::map<std::string, std::string> overrides) {
    m_tensorTypeOverrides = std::move(overrides);
  }

  /** @brief Set the application log level. */
  void SetLogLevel(AppLogLevel level) { m_logLevel = level; }

  /**
   * @brief Run generation for a prompt and return the cleaned generated text.
   * @return the generated text on success, or an empty string on failure.
   */
  std::string Run(const std::string& prompt);

  /** @brief Performance statistics from the most recent Run()/Generate(). */
  const GenerationStats& GetLastStats() const { return m_lastStats; }

 private:
  /**
   * @brief Tokenize the formatted prompt, run prefill + the decode loop, and
   *        return the raw generated text.
   */
  std::string Generate(const std::string& prompt);

  /** @brief Wrap the raw prompt in the configured template. */
  std::string FormatPrompt(const std::string& prompt) const;

  /**
   * @brief Pick the next token id from the logits.
   *
   * Delegates to the configured sampler, forwarding the repetition-penalty and
   * no-repeat-ngram settings via SamplerParams (the sampler applies these
   * filters internally). Falls back to greedy argmax when no sampler is set.
   */
  int32_t sampleNextToken(std::vector<float>& logits, int64_t vocab_size,
                          const std::vector<int32_t>& generatedTokens);

  /** @brief Return true if the token id is an end-of-sequence id. */
  bool        isEos(int32_t token) const;

  /**
   * @brief Run one execute() step. @p isPrefill selects the prefill vs decode
   *        compiled graph (via set_llm_phase) and its matching tensor set.
   */
  bool RunStep(const std::vector<int64_t>& inputIds, const std::vector<int64_t>& attentionMask,
               std::vector<float>& logitsOut, bool isPrefill);

  /**
   * @brief Single-token forward for the resident-KV path.
   * @param isPrefill selects the prefill vs decode graph.
   */
  bool Decode(int64_t tokenId, std::vector<float>& logitsOut, bool isPrefill);

  /**
   * @brief Batch prefill: process the prompt in [1, chunk] executes (chunked by
   *        m_maxSeqLen) and set m_validPast for the decode phase.
   */
  bool BatchPrefill(const std::vector<int32_t>& promptTokens, std::vector<float>& logitsOut);

  /**
   * @brief Build a leading-ones attention mask of the compiled attention_mask
   *        length: the first @p maskOnes positions (the valid tokens in the KV
   *        cache) are 1, the rest are 0. @p maskOnes is clamped to the mask
   *        length.
   */
  std::vector<int64_t> BuildLeadingMask(int maskOnes) const;

  /**
   * @brief One runner IO tensor: the compiled tensor metadata plus the
   *        runner-allocated NpuTensor and its host-visible virtual address.
   *
   * Holds either a HW (XRT-BO) or CPU tensor depending on the configured tensor
   * type; both are filled/read through the host pointer, and the write/read
   * helpers honour info.data_type.
   */
  struct IoTensor {
    /* Compiled tensor metadata (name, dtype, shape, strides, size) */
    vart::NpuTensorInfo info;
    /* Runner-allocated NpuTensor (HW XRT-BO or CPU buffer) */
    vart::NpuTensor     tensor;
    /* Host-visible virtual address of the tensor, used to fill/read it */
    void*               data = nullptr;
  };

  /** @brief Allocate the per-phase, per-batch runner IO tensors. */
  bool AllocateIoTensors();

  /**
   * @brief Map a case-insensitive "HW"/"CPU" string to the vart::TensorType
   *        enum, falling back to HW on an unrecognised value.
   */
  static vart::TensorType ToTensorType(const std::string& type);
  /** @brief String form ("HW"/"CPU") of a vart::TensorType, for create_runner. */
  static const char* TensorTypeString(vart::TensorType type);

  /**
   * @brief Log the compiled input/output tensor metadata (name, dtype, shape,
   *        strides, size). Only emitted at INFO or more verbose log levels.
   */
  void LogTensorInfo() const;

  /* vart-ml LLM runner (RunnerType::VAIML); created via vart::LlmRunnerFactory */
  std::shared_ptr<vart::LlmRunner> m_runner;

  /* Compiled tensor metadata, one set per graph. A single get_tensors_info()
   * query returns both graphs' tensors, split here by the "prefill_"/"decode_"
   * name prefix; each set keeps its own NpuTensorInfo list. */
  /* Prefill-graph input tensor metadata */
  std::vector<vart::NpuTensorInfo> m_prefill_inputInfos;
  /* Prefill-graph output tensor metadata */
  std::vector<vart::NpuTensorInfo> m_prefill_outputInfos;
  /* Decode-graph input tensor metadata */
  std::vector<vart::NpuTensorInfo> m_decode_inputInfos;
  /* Decode-graph output tensor metadata */
  std::vector<vart::NpuTensorInfo> m_decode_outputInfos;

  /* Prefill-graph input buffers */
  std::vector<std::vector<IoTensor>> m_prefill_inputTensors;
  /* Prefill-graph output buffers */
  std::vector<std::vector<IoTensor>> m_prefill_outputTensors;
  /* Decode-graph input buffers */
  std::vector<std::vector<IoTensor>> m_decode_inputTensors;
  /* Decode-graph output buffers */
  std::vector<std::vector<IoTensor>> m_decode_outputTensors;
  /* Compiled batch size (from get_batch_size()); execute()'s outer dimension */
  size_t m_batchSize = 1;

  /* Compiled tensor names (from model-config in the JSON; defaults shown here),
   * validated against the runner's tensor info in CreateLlmRunner(). */
  /* Token-id input tensor name (base name, prefix-stripped) */
  std::string          m_inputIdsName      = "input_ids";
  /* Attention-mask input tensor name */
  std::string          m_attentionMaskName = "attention_mask";
  /* Logits output tensor name */
  std::string          m_logitsName        = "logits";

  /* Metadata from vart::LlmRunner::get_llm_config(), resolved in CreateLlmRunner() */
  /* Vocabulary size (logits width) */
  int64_t              m_vocabSize          = 0;
  /* Model context length (max sequence length) */
  int                  m_maxSeqLen          = 256;
  /* Batch-prefill ingest width (compiled past-sequence length) */
  int64_t              m_compiledPastSeqLen = 1;
  /* Pad token id used to fill unused token-input slots */
  int64_t              m_padTokenId         = 1;
  /* End-of-sequence token ids that stop generation */
  std::vector<int64_t> m_eosIds;

  /* Resident-KV decode state (trailing-slot convention) */
  /* Compiled attention_mask element count (fixed each step) */
  size_t               m_maskLen   = 0;
  /* Host-side count of real past keys, advanced per forward and clamped to m_maskLen-1 */
  int                  m_validPast = 0;

  /* Embedded tokenizer (encode prompts / decode generated ids) */
  std::unique_ptr<llm_tokenizer::Tokenizer> m_tokenizer;
  /* Token sampler; when null the runner falls back to greedy argmax */
  std::unique_ptr<ISampler>   m_sampler;

  /* Runtime configuration (populated from the JSON config) */
  /* Path to the model/tokenizer bundle directory */
  std::string  m_modelPath;
  /* System prompt injected by the chat templates */
  std::string  m_systemPrompt        = "You are a helpful assistant.";
  /* Chat prompt template applied to each prompt */
  PromptFormat m_promptFormat        = PromptFormat::ChatML;
  /* Repetition-penalty pre-pass factor (1.0 = disabled) */
  float        m_repPenalty          = 1.0f;
  /* No-repeat n-gram size (0/1 = disabled) */
  int          m_noRepeatNgram       = 0;
  /* NPU start column forwarded to create_runner (0 = auto) */
  int          m_startColumn         = 0;
  /* AI Engine column sharing forwarded to create_runner */
  bool         m_aieColumnsSharing   = true;
  /* AI Analyzer profiling forwarded to create_runner */
  bool         m_aiAnalyzerProfiling = false;
  /* Runner input tensor exchange type (HW = XRT-BO; CPU = runner-side translation) */
  vart::TensorType m_inputTensorType  = vart::TensorType::CPU;
  /* Runner output tensor exchange type (HW | CPU) */
  vart::TensorType m_outputTensorType = vart::TensorType::CPU;
  /* Per-tensor exchange-type overrides ("input_tensor_type.<t>"/"output_tensor_type.<t>" -> HW|CPU) */
  std::map<std::string, std::string> m_tensorTypeOverrides;
  /* Application log level (see common/app_logger.hpp) */
  AppLogLevel  m_logLevel            = AppLogLevel::WARNING;
  /* Performance statistics from the most recent generation run */
  GenerationStats m_lastStats;
};
