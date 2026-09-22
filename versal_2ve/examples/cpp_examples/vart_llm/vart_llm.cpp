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
#include "vart_llm.hpp"

#include <algorithm>
#include <any>
#include <cctype>
#include <chrono>
#include <cstring>
#include <format>
#include <iostream>
#include <regex>
#include <string_view>
#include <unordered_map>

using namespace llm_sampler;

namespace {
/**
 * @brief Convert an fp32 value to its bf16 bit pattern.
 * @param f The fp32 value to convert.
 * @return The 16-bit bf16 representation.
 */
inline uint16_t fp32_to_bf16_bits(float f) {
  uint32_t bits;
  std::memcpy(&bits, &f, sizeof(bits));
  return static_cast<uint16_t>(bits >> 16);
}
/**
 * @brief Convert a bf16 bit pattern back to fp32.
 * @param b The 16-bit bf16 representation.
 * @return The reconstructed fp32 value.
 */
inline float bf16_bits_to_fp32(uint16_t b) {
  uint32_t bits = static_cast<uint32_t>(b) << 16;
  float f;
  std::memcpy(&f, &bits, sizeof(f));
  return f;
}

/* LLMRunner exposes an LLM model's prefill and decode IO as a single merged
 * get_tensors_info() list whose names are disambiguated by a "prefill_" /
 * "decode_" prefix (independent of set_llm_phase()). execute() takes exactly
 * one phase's tensors per call, keeping the prefixed names. */
const std::string kPrefillPrefix = "prefill_";
const std::string kDecodePrefix  = "decode_";

/**
 * @brief Strip the phase prefix so tensors can be matched by their user-facing
 *        base name (e.g. "input_ids", "attention_mask", "logits") in either phase.
 * @param name The prefixed tensor name.
 * @return The base tensor name with any "prefill_"/"decode_" prefix removed.
 */
std::string base_tensor_name(const std::string& name) {
  if (name.rfind(kPrefillPrefix, 0) == 0) return name.substr(kPrefillPrefix.size());
  if (name.rfind(kDecodePrefix, 0) == 0)  return name.substr(kDecodePrefix.size());
  return name;
}

/**
 * @brief Split the merged tensor list, keeping only entries with the given prefix.
 * @param all The merged tensor metadata list from get_tensors_info().
 * @param prefix The phase prefix to filter by ("prefill_" or "decode_").
 * @return The subset of tensors whose names start with @p prefix.
 */
std::vector<vart::NpuTensorInfo> filter_by_prefix(const std::vector<vart::NpuTensorInfo>& all,
                                                  const std::string& prefix) {
  std::vector<vart::NpuTensorInfo> out;
  for (const auto& info : all)
    if (info.name.rfind(prefix, 0) == 0) out.push_back(info);
  return out;
}

/**
 * @brief Copy a sequence of integer token ids into the tensor's raw buffer dtype.
 *
 * The first min(ids, capacity) elements are the real ids; any remaining slots
 * are filled with padId.
 *
 * @param dst Destination host pointer of the tensor buffer.
 * @param info Compiled tensor metadata (dtype, element count, byte size).
 * @param ids Token ids to write.
 * @param padId Pad token id used to fill the unused trailing slots.
 */
void writeIntBuffer(void* dst, const vart::NpuTensorInfo& info,
                    const std::vector<int64_t>& ids, int64_t padId) {
  std::memset(dst, 0, info.size_in_bytes);
  const size_t cap = info.size;
  const size_t n   = std::min<size_t>(cap, ids.size());
  const auto idAt  = [&](size_t i) { return i < n ? ids[i] : padId; };
  switch (info.data_type) {
    case vart::DataType::INT64:
    case vart::DataType::UINT64: {
      auto* p = static_cast<int64_t*>(dst);
      for (size_t i = 0; i < cap; ++i) p[i] = idAt(i);
      break;
    }
    case vart::DataType::INT16:
    case vart::DataType::UINT16: {
      auto* p = static_cast<uint16_t*>(dst);
      for (size_t i = 0; i < cap; ++i) p[i] = static_cast<uint16_t>(idAt(i));
      break;
    }
    case vart::DataType::INT32:
    case vart::DataType::UINT32:
    default: {
      auto* p = static_cast<int32_t*>(dst);
      for (size_t i = 0; i < cap; ++i) p[i] = static_cast<int32_t>(idAt(i));
      break;
    }
  }
}

/**
 * @brief Copy an attention mask (0/1 values) into the tensor's raw buffer
 *        honoring then dtype of the tensor.
 * @param dst Destination host pointer of the tensor buffer.
 * @param info Compiled tensor metadata (dtype, element count, byte size).
 * @param mask Mask values (0/1) to write.
 */
void writeMaskBuffer(void* dst, const vart::NpuTensorInfo& info,
                     const std::vector<int64_t>& mask) {
  std::memset(dst, 0, info.size_in_bytes);
  const size_t n = std::min<size_t>(info.size, mask.size());
  switch (info.data_type) {
    case vart::DataType::BF16: {
      auto* p = static_cast<uint16_t*>(dst);
      for (size_t i = 0; i < n; ++i) p[i] = fp32_to_bf16_bits(static_cast<float>(mask[i]));
      break;
    }
    case vart::DataType::FLOAT32: {
      auto* p = static_cast<float*>(dst);
      for (size_t i = 0; i < n; ++i) p[i] = static_cast<float>(mask[i]);
      break;
    }
    case vart::DataType::INT64:
    case vart::DataType::UINT64: {
      auto* p = static_cast<int64_t*>(dst);
      for (size_t i = 0; i < n; ++i) p[i] = mask[i];
      break;
    }
    case vart::DataType::INT16:
    case vart::DataType::UINT16: {
      auto* p = static_cast<uint16_t*>(dst);
      for (size_t i = 0; i < n; ++i) p[i] = static_cast<uint16_t>(mask[i]);
      break;
    }
    case vart::DataType::INT32:
    case vart::DataType::UINT32:
    default: {
      auto* p = static_cast<int32_t*>(dst);
      for (size_t i = 0; i < n; ++i) p[i] = static_cast<int32_t>(mask[i]);
      break;
    }
  }
}

/**
 * @brief Read the last valid position's vocab logits from the tensor's raw
 *        output buffer.
 * @param src Source host pointer of the logits tensor buffer.
 * @param info Compiled tensor metadata (dtype, element count).
 * @param vocab Vocabulary size (logits row width).
 * @param validLen Number of valid tokens; the row at validLen-1 is read.
 * @param out Output vector, resized to @p vocab and fully overwritten.
 */
void readLogitsBuffer(const void* src, const vart::NpuTensorInfo& info,
                      int64_t vocab, size_t validLen, std::vector<float>& out) {
  const size_t v       = static_cast<size_t>(vocab);
  const size_t total   = info.size;
  const size_t numRows = (v > 0) ? (total / v) : 0;
  size_t row = (validLen > 0) ? (validLen - 1) : 0;
  if (numRows > 0 && row >= numRows) row = numRows - 1;  /* clamp to buffer */
  const size_t off = row * v;
  out.assign(v, 0.0f);
  switch (info.data_type) {
    case vart::DataType::BF16: {
      const auto* p = static_cast<const uint16_t*>(src) + off;
      for (size_t i = 0; i < v; ++i) out[i] = bf16_bits_to_fp32(p[i]);
      break;
    }
    case vart::DataType::FLOAT32:
    default: {
      const auto* p = static_cast<const float*>(src) + off;
      for (size_t i = 0; i < v; ++i) out[i] = p[i];
      break;
    }
  }
}
}  // namespace

/** @brief Construct an empty context. */
AppContext::AppContext()  = default;
/** @brief Destroy the context, releasing the runner and its IO tensors. */
AppContext::~AppContext() = default;

/**
 * @brief Set the tokenizer used to encode prompts and decode generated ids.
 * @param tokenizer Tokenizer to take ownership of.
 * @return true on success, false if @p tokenizer is null.
 */
bool AppContext::SetTokenizer(std::unique_ptr<llm_tokenizer::Tokenizer>&& tokenizer) {
  if (!tokenizer) return false;
  m_tokenizer = std::move(tokenizer);
  return true;
}

/**
 * @brief Set the token sampler; when unset the runner uses greedy argmax.
 * @param sampler Sampler to take ownership of.
 * @return true (always accepted; a null sampler falls back to greedy).
 */
bool AppContext::SetSampler(std::unique_ptr<ISampler>&& sampler) {
  m_sampler = std::move(sampler);
  return true;
}

/**
 * @brief Test whether a token id is an end-of-sequence id.
 * @param token The token id to test.
 * @return true if @p token is in the configured EOS id list.
 */
bool AppContext::isEos(int32_t token) const {
  return std::find(m_eosIds.begin(), m_eosIds.end(), token) != m_eosIds.end();
}

/**
 * @brief Wrap the raw user prompt in the model's configured chat template.
 * @param prompt The raw user prompt text.
 * @return The prompt wrapped with the template's system/user/assistant markers.
 */
std::string AppContext::FormatPrompt(const std::string& prompt) const {
  static const std::string kChatMLPre  = "<|im_start|>system\n";
  static const std::string kChatMLMid  = "<|im_end|>\n<|im_start|>user\n";
  static const std::string kChatMLPost = "<|im_end|>\n<|im_start|>assistant\n";

  static const std::string kQAPre  = "Q: ";
  static const std::string kQAPost = "\nA:";

  /* U+FF5C FULLWIDTH VERTICAL LINE (EF BD 9C) around User/Assistant. The escapes
   * are split with adjacent string literals so a following hex digit (e.g. the
   * 'A' in "Assistant") is not swallowed into the \x9c escape. */
  static const std::string kDeepSeekPre  = "<\xef\xbd\x9c" "User" "\xef\xbd\x9c>";
  static const std::string kDeepSeekPost = "<\xef\xbd\x9c" "Assistant" "\xef\xbd\x9c>";

  static const std::string kLlama3Pre =
      "<|begin_of_text|><|start_header_id|>system<|end_header_id|>\n\n";
  static const std::string kLlama3Mid  = "<|eot_id|><|start_header_id|>user<|end_header_id|>\n\n";
  static const std::string kLlama3Post =
      "<|eot_id|><|start_header_id|>assistant<|end_header_id|>\n\n";

  static const std::string kOlmoPre  = "<|user|>\n";
  static const std::string kOlmoPost = "\n<|assistant|>\n";

  switch (m_promptFormat) {
    case PromptFormat::ChatML:
      return kChatMLPre + m_systemPrompt + kChatMLMid + prompt + kChatMLPost;
    case PromptFormat::QA:
      return kQAPre + prompt + kQAPost;
    case PromptFormat::DeepSeek:
      return kDeepSeekPre + prompt + kDeepSeekPost;
    case PromptFormat::Llama3:
      return kLlama3Pre + m_systemPrompt + kLlama3Mid + prompt + kLlama3Post;
    case PromptFormat::Olmo:
      return kOlmoPre + prompt + kOlmoPost;
    case PromptFormat::Raw:
    default:
      return prompt;
  }
}

/**
 * @brief Dump the compiled input/output tensor metadata (name, dtype, shape,
 *        strides, size). Only emitted at INFO or more verbose log levels.
 */
void AppContext::LogTensorInfo() const {
  auto joinDims = [](const std::vector<uint32_t>& dims) {
    std::string s = "[";
    for (size_t i = 0; i < dims.size(); ++i) s += (i ? "," : "") + std::to_string(dims[i]);
    return s + "]";
  };
  auto logTensors = [&](const char* dir, const std::vector<vart::NpuTensorInfo>& infos) {
    APP_LOG(AppLogLevel::INFO, m_logLevel, "%s tensors: %zu", dir, infos.size());
    for (const auto& t : infos) {
      APP_LOG(AppLogLevel::INFO, m_logLevel,
              "  %s: dtype=%d shape=%s strides=%s size=%zu bytes=%zu", t.name.c_str(),
              static_cast<int>(t.data_type), joinDims(t.shape).c_str(),
              joinDims(t.strides).c_str(), t.size, t.size_in_bytes);
    }
  };
  logTensors("PREFILL INPUT", m_prefill_inputInfos);
  logTensors("PREFILL OUTPUT", m_prefill_outputInfos);
  logTensors("DECODE INPUT", m_decode_inputInfos);
  logTensors("DECODE OUTPUT", m_decode_outputInfos);
}

/**
 * @brief Create the vart::LlmRunner and read LLM metadata via get_llm_config().
 *
 * Creates the runner through vart::LlmRunnerFactory, queries the merged tensor
 * metadata (split into prefill/decode by name prefix), resolves the mask length,
 * vocab size, context length, EOS ids, pad id and batch size, then allocates the
 * per-phase, per-batch IO tensors.
 *
 * @param modelPath Path to the model/tokenizer bundle directory.
 * @return true on success, false on any creation or metadata error.
 */
bool AppContext::CreateLlmRunner(const std::string& modelPath) {
  m_modelPath = modelPath;
  APP_LOG(AppLogLevel::INFO, m_logLevel, "Loading model bundle from: %s", modelPath.c_str());

  try {
    const std::string logLevelStr = (m_logLevel >= AppLogLevel::DEBUG) ? "DEBUG" : "WARNING";
    std::unordered_map<std::string, std::any> options{
        {"log_level", logLevelStr},
        {"input_tensor_type", std::string{TensorTypeString(m_inputTensorType)}},
        {"output_tensor_type", std::string{TensorTypeString(m_outputTensorType)}},
    };
    /* Per-tensor exchange-type overrides forwarded as create_runner options
     * ("input_tensor_type.<t>"/"output_tensor_type.<t>" -> "HW"|"CPU"). */
    for (const auto& [key, val] : m_tensorTypeOverrides) {
      options[key] = std::string{TensorTypeString(ToTensorType(val))};
    }
    if (m_aieColumnsSharing)      options["aie_columns_sharing"] = m_aieColumnsSharing;
    if (m_startColumn > 0)        options["start_column"] = m_startColumn;
    if (m_aiAnalyzerProfiling)    options["ai_analyzer_profiling"] = m_aiAnalyzerProfiling;
    options["input_ids_name"] = m_inputIdsName;
    options["attention_mask_name"] = m_attentionMaskName;
    options["logits_name"] = m_logitsName;

    m_runner = vart::LlmRunnerFactory::create_runner(vart::RunnerType::VAIML, m_modelPath, options);
  } catch (const std::exception& e) {
    APP_LOG(AppLogLevel::ERROR, m_logLevel, "Failed to create vart::LlmRunner: %s", e.what());
    return false;
  }
  if (!m_runner) {
    APP_LOG(AppLogLevel::ERROR, m_logLevel, "vart::LlmRunnerFactory returned a null runner");
    return false;
  }

  /* LLMRunner returns ONE merged list per direction/type containing both graphs'
   * tensors, disambiguated by a "prefill_"/"decode_" name prefix */
  const std::vector<vart::NpuTensorInfo> allInputInfos =
      m_runner->get_tensors_info(vart::TensorDirection::INPUT, m_inputTensorType);
  const std::vector<vart::NpuTensorInfo> allOutputInfos =
      m_runner->get_tensors_info(vart::TensorDirection::OUTPUT, m_outputTensorType);

  if (allInputInfos.empty() || allOutputInfos.empty()) {
    APP_LOG(AppLogLevel::ERROR, m_logLevel,
            "Model metadata returned no tensors. Merged INPUT count=%zu OUTPUT count=%zu",
            allInputInfos.size(), allOutputInfos.size());
    return false;
  }

  m_prefill_inputInfos  = filter_by_prefix(allInputInfos,  kPrefillPrefix);
  m_prefill_outputInfos = filter_by_prefix(allOutputInfos, kPrefillPrefix);
  m_decode_inputInfos   = filter_by_prefix(allInputInfos,  kDecodePrefix);
  m_decode_outputInfos  = filter_by_prefix(allOutputInfos, kDecodePrefix);

  /* Re-fetch metadata for each overridden tensor so its info matches the
   * per-tensor type we passed to create_runner (dtype/shape/strides/size). */
  const std::string kInputOptPrefix  = "input_tensor_type.";
  const std::string kOutputOptPrefix = "output_tensor_type.";
  auto applyOverride = [&](std::vector<vart::NpuTensorInfo>& infos, const std::string& name,
                           vart::TensorType type) {
    for (auto& info : infos) {
      if (info.name == name) {
        info = m_runner->get_tensor_info_by_name(name, type);
        return;
      }
    }
  };
  for (const auto& [key, val] : m_tensorTypeOverrides) {
    const vart::TensorType type = ToTensorType(val);
    if (key.rfind(kInputOptPrefix, 0) == 0) {
      const std::string name = key.substr(kInputOptPrefix.size());
      const bool isPrefill = name.rfind(kPrefillPrefix, 0) == 0;
      applyOverride(isPrefill ? m_prefill_inputInfos : m_decode_inputInfos, name, type);
    } else if (key.rfind(kOutputOptPrefix, 0) == 0) {
      const std::string name = key.substr(kOutputOptPrefix.size());
      const bool isPrefill = name.rfind(kPrefillPrefix, 0) == 0;
      applyOverride(isPrefill ? m_prefill_outputInfos : m_decode_outputInfos, name, type);
    }
  }

  if (m_prefill_inputInfos.empty() || m_prefill_outputInfos.empty() ||
      m_decode_inputInfos.empty() || m_decode_outputInfos.empty()) {
    APP_LOG(AppLogLevel::ERROR, m_logLevel,
            "Model metadata is missing prefill_/decode_ prefixed tensors. "
            "Merged INPUT count=%zu OUTPUT count=%zu",
            allInputInfos.size(), allOutputInfos.size());
    return false;
  }

  if (static_cast<int>(m_logLevel) >= static_cast<int>(AppLogLevel::INFO)) {
    LogTensorInfo();
  }

  /* Resolve, by base (un-prefixed) name against the decode graph for attention_mask and logits
   * for finding the mask length and vocab size. Since prefill and decode share the same tensors,
   * we can use the decode graph to find the mask length and vocab size. */
  const auto maskIt = std::find_if(m_decode_inputInfos.begin(), m_decode_inputInfos.end(),
                                   [&](const auto& t) { return base_tensor_name(t.name) == m_attentionMaskName; });
  const auto logitsIt = std::find_if(m_decode_outputInfos.begin(), m_decode_outputInfos.end(),
                                     [&](const auto& t) { return base_tensor_name(t.name) == m_logitsName; });
  if (maskIt == m_decode_inputInfos.end() || logitsIt == m_decode_outputInfos.end()) {
    APP_LOG(AppLogLevel::ERROR, m_logLevel,
            "Model must expose mask '%s' and output '%s'",
            m_attentionMaskName.c_str(), m_logitsName.c_str());
    return false;
  }

  /* Compiled attention_mask element count: fixed length used every step by the
   * resident-KV trailing-slot mask. */
  m_maskLen = maskIt->size;

  vart::LLMConfigInfo llm;
  try {
    llm = m_runner->get_llm_config();
  } catch (const std::exception& e) {
    APP_LOG(AppLogLevel::ERROR, m_logLevel, "get_llm_config() failed: %s", e.what());
    return false;
  }

  /* Prefer the vocab size reported by get_llm_config(); otherwise fall back to
   * the last dimension of the logits tensor shape which should be the vocabulary size. */
  if (llm.vocab_size > 0) {
    m_vocabSize = llm.vocab_size;
  } else if (!logitsIt->shape.empty()) {
    m_vocabSize = static_cast<int64_t>(logitsIt->shape.back());
  }
  if (m_vocabSize <= 0) {
    APP_LOG(AppLogLevel::ERROR, m_logLevel,
            "Could not determine vocab size from get_llm_config() or the logits tensor shape");
    return false;
  }

  /* Resolve runtime parameters from the model metadata. */
  m_compiledPastSeqLen = llm.compiled_past_seq_len;

  if (llm.max_sequence_length > 0) {
    m_maxSeqLen = static_cast<int>(llm.max_sequence_length);
  } else {
    APP_LOG(AppLogLevel::ERROR, m_logLevel,
            "get_llm_config() did not provide a valid max_sequence_length");
    return false;
  }

  if (!llm.eos_token_ids.empty()) {
    m_eosIds = llm.eos_token_ids;
  } else {
    APP_LOG(AppLogLevel::ERROR, m_logLevel, "get_llm_config() did not provide eos_token_ids");
    return false;
  }

  /* Use the model's pad token id if it is set,
   * it fills the unused slots of the token input. */
  m_padTokenId = llm.pad_token_id;

  APP_LOG(AppLogLevel::INFO, m_logLevel,
          "LLM config: vocab_size=%ld, eos_count=%zu, pad_token_id=%ld, max_seq_len=%ld, "
          "compiled_past_seq_len=%ld",
          static_cast<long>(m_vocabSize), m_eosIds.size(), static_cast<long>(m_padTokenId),
          static_cast<long>(llm.max_sequence_length),
          static_cast<long>(m_compiledPastSeqLen));

  /* execute()'s outer batch dimension must equal the compiled batch size. */
  m_batchSize = std::max<size_t>(1, m_runner->get_batch_size());
  APP_LOG(AppLogLevel::RESULT, m_logLevel, "Compiled batch size: %zu", m_batchSize);

  if (!AllocateIoTensors()) {
    return false;
  }

  APP_LOG(AppLogLevel::RESULT, m_logLevel,
          "LLM runner ready (prefill %zu/%zu, decode %zu/%zu inputs/outputs, batch=%zu)",
          m_prefill_inputInfos.size(), m_prefill_outputInfos.size(),
          m_decode_inputInfos.size(), m_decode_outputInfos.size(), m_batchSize);
  return true;
}

/**
 * @brief Map a case-insensitive "HW"/"CPU" string to the vart::TensorType enum.
 * @param type Tensor-type string from the JSON config.
 * @return vart::TensorType::CPU for "CPU"; vart::TensorType::HW otherwise.
 */
vart::TensorType AppContext::ToTensorType(const std::string& type) {
  std::string t;
  t.reserve(type.size());
  for (char c : type) t.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
  if (t == "CPU") return vart::TensorType::CPU;
  return vart::TensorType::HW;  /* default / "HW" */
}

/**
 * @brief String form of a vart::TensorType, for the create_runner options.
 * @param type The tensor type to stringify.
 * @return "CPU" for vart::TensorType::CPU; "HW" otherwise.
 */
const char* AppContext::TensorTypeString(vart::TensorType type) {
  return (type == vart::TensorType::CPU) ? "CPU" : "HW";
}

/**
 * @brief Allocate one NpuTensor per model input/output, for every batch slot.
 *
 * Allocates independent tensors for each (batch slot, phase tensor) so the
 * execute() outer vector dimension matches the compiled batch size, and caches
 * each tensor's host-visible virtual address for filling/reading.
 *
 * @return true if all prefill/decode input/output tensors were allocated.
 *
 * @todo Avoid copying tokens from the tokenizer (zero copy).
 */
bool AppContext::AllocateIoTensors() {
  auto allocAll = [&](const std::vector<vart::NpuTensorInfo>& infos,
                      std::vector<std::vector<IoTensor>>& out) -> bool {
    out.assign(m_batchSize, {});
    for (size_t b = 0; b < m_batchSize; ++b) {
      out[b].reserve(infos.size());
      for (const auto& info : infos) {
        IoTensor t;
        t.info = info;
        try {
          t.tensor = m_runner->allocate_npu_tensor(info);
        } catch (const std::exception& e) {
          APP_LOG(AppLogLevel::ERROR, m_logLevel, "allocate_npu_tensor failed for '%s': %s",
                  info.name.c_str(), e.what());
          return false;
        }
        t.data = t.tensor.get_virtual_address();
        if (!t.data) {
          APP_LOG(AppLogLevel::ERROR, m_logLevel,
                  "allocate_npu_tensor returned no host address for '%s'", info.name.c_str());
          return false;
        }
        out[b].push_back(std::move(t));
      }
    }
    return true;
  };
  if (!allocAll(m_prefill_inputInfos, m_prefill_inputTensors)) return false;
  if (!allocAll(m_prefill_outputInfos, m_prefill_outputTensors)) return false;
  if (!allocAll(m_decode_inputInfos, m_decode_inputTensors)) return false;
  if (!allocAll(m_decode_outputInfos, m_decode_outputTensors)) return false;
  return true;
}

/**
 * @brief Run one execute() call (prefill or decode) through vart::LlmRunner.
 *
 * Selects the phase graph via set_llm_phase(), fills every batch slot of the
 * matching input tensors, runs execute(), and reads the logits back from
 * batch 0.
 *
 * @param inputIds Token ids for this step.
 * @param attentionMask Attention mask for this step.
 * @param logitsOut Output logits of the last valid position.
 * @param isPrefill true selects the prefill graph, false the decode graph.
 * @return true on a successful execute(), false otherwise.
 */
bool AppContext::RunStep(const std::vector<int64_t>& inputIds,
                            const std::vector<int64_t>& attentionMask,
                            std::vector<float>& logitsOut, bool isPrefill) {
  /* Select the compiled graph and its matching batched HW tensor set for this step. */
  m_runner->set_llm_phase(isPrefill ? vart::LlmPhase::PREFILL : vart::LlmPhase::DECODE);
  std::vector<std::vector<IoTensor>>& inputTensors  = isPrefill ? m_prefill_inputTensors  : m_decode_inputTensors;
  std::vector<std::vector<IoTensor>>& outputTensors = isPrefill ? m_prefill_outputTensors : m_decode_outputTensors;

  for (auto& batch : inputTensors) {
    for (auto& t : batch) {
      const std::string base = base_tensor_name(t.info.name);
      if (base == m_inputIdsName) {
        writeIntBuffer(t.data, t.info, inputIds, m_padTokenId);
      } else if (base == m_attentionMaskName) {
        writeMaskBuffer(t.data, t.info, attentionMask);
      } else {
        /* Any other compiled input (e.g. position ids) is zero-filled */
        std::memset(t.data, 0, t.info.size_in_bytes);
      }
    }
  }

  std::vector<std::vector<vart::NpuTensor>> inputs(inputTensors.size());
  for (size_t b = 0; b < inputTensors.size(); ++b) {
    inputs[b].reserve(inputTensors[b].size());
    for (auto& t : inputTensors[b]) inputs[b].push_back(t.tensor);
  }
  std::vector<std::vector<vart::NpuTensor>> outputs(outputTensors.size());
  for (size_t b = 0; b < outputTensors.size(); ++b) {
    outputs[b].reserve(outputTensors[b].size());
    for (auto& t : outputTensors[b]) outputs[b].push_back(t.tensor);
  }

  /* accumulated per phase (prefill vs decode). */
  const auto executeStart = std::chrono::high_resolution_clock::now();
  const vart::StatusCode status = m_runner->execute(inputs, outputs);
  const auto executeEnd = std::chrono::high_resolution_clock::now();
  const double executeMs =
      std::chrono::duration<double, std::milli>(executeEnd - executeStart).count();
  if (isPrefill) {
    m_lastStats.prefillTimeMs += executeMs;
  } else {
    m_lastStats.decodeTimeMs += executeMs;
  }

  if (status != vart::StatusCode::SUCCESS) {
    APP_LOG(AppLogLevel::ERROR, m_logLevel, "vart::LlmRunner::execute failed (status=%d)",
            static_cast<int>(status));
    return false;
  }

  /* readLogitsBuffer() resizes and fully overwrites logitsOut. Pass the valid
   * token count so prefill reads the last prompt token's row. */
  for (auto& t : outputTensors[0]) {
    if (base_tensor_name(t.info.name) == m_logitsName) {
      readLogitsBuffer(t.data, t.info, m_vocabSize, inputIds.size(), logitsOut);
    }
    /* Other outputs (if any) are ignored. */
  }
  return true;
}

/**
 * @brief Build a leading-ones attention mask of the compiled length (m_maskLen).
 *
 * The first @p maskOnes positions (the valid tokens held in the KV cache) are 1
 * and the rest are 0.
 *
 * @param maskOnes Number of leading ones; clamped to m_maskLen.
 * @return The mask vector of length m_maskLen.
 */
std::vector<int64_t> AppContext::BuildLeadingMask(int maskOnes) const {
  std::vector<int64_t> mask(m_maskLen, 0);
  const int ones = std::min<int>(maskOnes, static_cast<int>(m_maskLen));
  std::fill_n(mask.begin(), ones, 1);
  return mask;
}

/**
 * @brief Run one single-token step (prefill token or decode).
 *
 * Feeds one token plus a leading-ones mask covering all valid tokens so far
 * (past keys + the current token), then advances m_validPast (clamped to
 * m_maskLen-1).
 *
 * @param tokenId The token id to feed this step.
 * @param logitsOut Output logits for the produced position.
 * @param isPrefill true selects the prefill graph, false the decode graph.
 * @return true on success, false if the underlying execute() failed.
 */
bool AppContext::Decode(int64_t tokenId, std::vector<float>& logitsOut, bool isPrefill) {
  std::vector<int64_t> stepIds  = {tokenId};
  std::vector<int64_t> stepMask = BuildLeadingMask(m_validPast + 1);
  if (!RunStep(stepIds, stepMask, logitsOut, isPrefill)) return false;

  const int W = static_cast<int>(m_maskLen) - 1;
  if (m_validPast < W) ++m_validPast;
  return true;
}

/**
 * @brief Prefill the whole prompt in a single [1, chunk] execute().
 *
 * Ingests the prompt (clamped to the model's max sequence length) with a
 * leading-ones mask, then sets m_validPast for the following decode phase.
 *
 * @param promptTokens The tokenized prompt.
 * @param logitsOut Output logits of the last prompt position (resized here).
 * @return true on success, false if the prefill execute() failed.
 */
bool AppContext::BatchPrefill(const std::vector<int32_t>& promptTokens,
                                 std::vector<float>& logitsOut) {
  const int W = static_cast<int>(m_maskLen) - 1;

  const int len = std::min(static_cast<int>(promptTokens.size()), m_maxSeqLen);
  std::vector<int64_t> chunk(promptTokens.begin(), promptTokens.begin() + len);
  std::vector<int64_t> mask = BuildLeadingMask(len);
  if (!RunStep(chunk, mask, logitsOut, /*isPrefill=*/true)) {
    APP_LOG(AppLogLevel::ERROR, m_logLevel, "Batch prefill execute failed");
    return false;
  }
  m_validPast = std::min(len, W);
  return true;
}

// =============================================================================
// sampleNextToken - delegate to the sampler (which applies the filters)
// =============================================================================

int32_t AppContext::sampleNextToken(std::vector<float>& logits, int64_t vocab_size,
                                       const std::vector<int32_t>& generatedTokens) {
  if (m_sampler) {
    // The sampler library applies the repetition-penalty and no-repeat-ngram
    // filters internally (inside Sample()); forward the configured values via
    // SamplerParams and let the sampler own all logits post-processing.
    SamplerParams params;
    params.repetitionPenalty = m_repPenalty;
    params.noRepeatNgram     = m_noRepeatNgram;
    return m_sampler->Sample(logits, params, generatedTokens);
  }

  // Default greedy argmax (no sampler configured).
  int32_t bestId  = 0;
  float   bestVal = logits[0];
  for (int64_t i = 1; i < vocab_size; ++i) {
    if (logits[i] > bestVal) { bestVal = logits[i]; bestId = static_cast<int32_t>(i); }
  }
  return bestId;
}

/**
 * @brief Tokenize the formatted prompt, run prefill and the decode loop.
 *
 * Resets the resident-KV state (every turn is treated as new), formats and
 * tokenizes the prompt, runs the prefill phase, then autoregressively decodes
 * one token per execute() until EOS or the sequence budget is exhausted.
 *
 * @param prompt The raw user prompt.
 * @return The raw generated text, or an empty string on failure.
 */
std::string AppContext::Generate(const std::string& prompt) {
  APP_LOG(AppLogLevel::INFO, m_logLevel, "Generate() called with prompt: %s", prompt.c_str());
  m_lastStats = GenerationStats{};

  if (!m_tokenizer) {
    APP_LOG(AppLogLevel::ERROR, m_logLevel, "No tokenizer set. Call SetTokenizer() first.");
    return "";
  }
  if (!m_runner) {
    APP_LOG(AppLogLevel::ERROR, m_logLevel, "Runner not initialized.");
    return "";
  }

  /* Every turn is treated as new */
  m_validPast = 0;

  const std::string formattedPrompt = FormatPrompt(prompt);

  std::vector<int32_t> promptTokens = m_tokenizer->Encode(formattedPrompt);
  if (promptTokens.empty()) {
    APP_LOG(AppLogLevel::ERROR, m_logLevel, "Tokenizer returned empty sequence.");
    return "";
  }

  const int promptLen = static_cast<int>(promptTokens.size());
  m_lastStats.prefillTokens = promptLen;

  /* Sequence budget for this turn: prompt + generated <= window. */
  int seqBudget = m_maskLen > 1 ? static_cast<int>(m_maskLen) - 1 : m_maxSeqLen;
  int maxNewTokens = seqBudget - promptLen;
  if (maxNewTokens <= 0) {
    APP_LOG(AppLogLevel::WARNING, m_logLevel,
            "Prompt (%d tokens) fills the sequence window (%d); nothing to generate.",
            promptLen, seqBudget);
    return "";
  }

  std::vector<int32_t> generatedTokens;
  generatedTokens.reserve(static_cast<size_t>(maxNewTokens));
  std::vector<float> logitsOut;

  /* ------------------------------------------------------------------------- */
  /* Phase 1: Prefill.                                                         */
  /* ------------------------------------------------------------------------- */
  if (!BatchPrefill(promptTokens, logitsOut)) {
    return "";
  }

  /* logitsOut holds the last prompt position's logits ([.., vocab]). */
  int32_t nextToken = sampleNextToken(logitsOut, m_vocabSize, generatedTokens);
  if (isEos(nextToken)) {
    APP_LOG(AppLogLevel::INFO, m_logLevel, "EOS token produced at prefill");
    return "";
  }
  generatedTokens.push_back(nextToken);
  m_lastStats.decodeTokens++;

  /* ------------------------------------------------------------------------- */
  /* Phase 2: Decode (autoregressive, one token per execute).                  */
  /* ------------------------------------------------------------------------- */
  int step = 1;
  for (; step < maxNewTokens; ++step) {
    if (!Decode(static_cast<int64_t>(nextToken), logitsOut, /*isPrefill=*/false)) {
      break;
    }

    nextToken = sampleNextToken(logitsOut, m_vocabSize, generatedTokens);
    if (isEos(nextToken)) {
      APP_LOG(AppLogLevel::INFO, m_logLevel, "EOS token at step %d", step);
      break;
    }

    generatedTokens.push_back(nextToken);
    m_lastStats.decodeTokens++;
  }

  if (step == maxNewTokens) {
    /* Always surface this to the user, regardless of the configured log level. */
    std::cout << "Reached max tokens limit (" << maxNewTokens << ")" << std::endl;
  }

  /* Byte-level BPE requires decoding the whole id sequence at once. */
  return m_tokenizer->Decode(generatedTokens);
}

/**
 * @brief Run generation for a prompt and return the cleaned generated text.
 *
 * @param prompt The raw user prompt.
 * @return The cleaned generated text, or an empty string on failure.
 */
std::string AppContext::Run(const std::string& prompt) {
  try {
    if (!m_tokenizer) {
      APP_LOG(AppLogLevel::ERROR, m_logLevel, "Runner has no tokenizer set");
      return "";
    }

    std::string result = Generate(prompt);
    if (result.empty()) {
      APP_LOG(AppLogLevel::ERROR, m_logLevel, "Generation failed or produced no text");
      return "";
    }

    /* Strip any residual chat-template special tokens (e.g. <|im_end|>) and trim. */
    result = std::regex_replace(result, std::regex(R"(<\|[^|]*\|>)"), "");
    size_t first = result.find_first_not_of(" \t\n\r");
    result = (first == std::string::npos) ? "" : result.substr(first);
    while (!result.empty() && std::isspace(static_cast<unsigned char>(result.back()))) {
      result.pop_back();
    }

    return result;
  } catch (const std::exception& e) {
    APP_LOG(AppLogLevel::ERROR, m_logLevel, "Error: %s", e.what());
    return "";
  }
}
