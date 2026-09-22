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

/**
 * @file main.cpp
 * @brief Basic LLM demo: vart-ml runner + common sampler/tokenizer libs.
 *
 * This is the vart_llm example. It drives generation through the vart-ml
 * public API (vart::LlmRunnerFactory::create_runner + vart::LlmRunner::execute)
 * and adds a selectable prompt template and configurable sampling with
 * anti-repetition filters (repetition penalty + no-repeat-ngram).
 * Tokenization and sampling come from the llm_tokenizer and llm_sampler libraries.
 *
 * Usage:
 *   vart_llm [--app-config <path>] [--log-level <n>]
 *   vart_llm --help
 *
 * The prompt is entered interactively at run time: the app loops reading a
 * prompt from stdin until an empty line or EOF (Ctrl-D).
 *
 * The log level is set from the CLI --log-level option only (see below).
 *
 * For the JSON configuration schema, see json_configs/README.md.
 */

#include <cstdint>
#include <cstdlib>
#include <format>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <boost/program_options.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include "llm_sampler.hpp"
#include "llm_tokenizer.hpp"
#include "common/app_logger.hpp"
#include "vart_llm.hpp"

namespace po = boost::program_options;
namespace pt = boost::property_tree;

using llm_sampler::ISampler;
using llm_sampler::SamplerParams;

namespace {
/* Parsed command-line options for the application. */
struct Options {
  std::string config_path;  // Path to the JSON config file
  int         log_level{};  // Application log level [0, 6]
};

/* Register the supported command-line options (--help, --app-config,
 * --log-level) on the given options description. */
void add_options(po::options_description& desc, Options& opt) {
  desc.add_options()("help,h", "Print help message")(
      "app-config,c", po::value<std::string>(&opt.config_path)->default_value("/etc/vai/vart_llm/json_configs/vart_llm.json"),
      "JSON config file (see /etc/vai/vart_llm/json_configs/vart_llm.json)")(
      "log-level,l", po::value<int>(&opt.log_level)->default_value(static_cast<int>(AppLogLevel::WARNING)),
      "Application log level: 0 NONE, 1 ERROR, 2 WARNING, 3 RESULT, 4 FIXME, 5 INFO, 6 DEBUG");
}

/* Outcome of command-line parsing: proceed, print help, or error out. */
enum class ParseOutcome { Ok, Help, Error };

/* Parse the command line into Options and validate the log level.
 * Returns Help when --help was requested, Error on a parse/validation
 * failure (the caller is responsible for printing usage), and Ok otherwise. */
ParseOutcome parse_options(int argc, char* argv[], Options& opt,
                           po::options_description& desc) {
  try {
    po::variables_map vm;
    po::store(po::command_line_parser(argc, argv).options(desc).run(), vm);
    if (vm.count("help")) return ParseOutcome::Help;
    po::notify(vm);
    if (opt.log_level < 0 || opt.log_level > 6) {
      std::cerr << "Error: --log-level must be in [0, 6] (got " << opt.log_level << ")" << std::endl;
      return ParseOutcome::Error;
    }
    return ParseOutcome::Ok;
  } catch (const po::error& ex) {
    std::cerr << "Error: " << ex.what() << std::endl;
    return ParseOutcome::Error;
  }
}

/* Map a prompt-format string to PromptFormat; defaults to ChatML. */
static PromptFormat parsePromptFormat(const std::string& s) {
  if (s == "qa")       return PromptFormat::QA;
  if (s == "deepseek") return PromptFormat::DeepSeek;
  if (s == "llama3")   return PromptFormat::Llama3;
  if (s == "olmo")     return PromptFormat::Olmo;
  if (s == "raw")      return PromptFormat::Raw;
  return PromptFormat::ChatML;
}

/* Split a "strategy=value" sampler string into its parts. When no '=' is
 * present the whole string is the strategy and value is left empty.
 * Returns false when the strategy part is empty. */
bool parseSamplerOptions(const std::string& samplerStr, std::string& strategy,
                         std::string& value) {
  if (samplerStr.empty()) return false;
  auto eq = samplerStr.find('=');
  if (eq == std::string::npos) {
    strategy = samplerStr;
    value.clear();
  } else {
    strategy = samplerStr.substr(0, eq);
    value    = samplerStr.substr(eq + 1);
  }
  return !strategy.empty();
}

/* Create a sampler for the given strategy ("greedy", "temperature", "top-k",
 * "top-p", "repetition-penalty") and its string value. */
std::unique_ptr<ISampler> createSampler(const std::string& strategy, const std::string& value,
                                        uint32_t seed, AppLogLevel log_level) {
  try {
    /* Start from explicit defaults (all filters disabled), then override the
       one field the selected strategy needs. */
    SamplerParams params;
    params.temperature       = 1.0f;  // 1.0 = no scaling
    params.topK              = 1;     // 0 = disabled
    params.topP              = 0.9f;  // 0.0 = disabled
    params.repetitionPenalty = 1.1f;  // 1.0 = disabled

    std::unique_ptr<ISampler> sampler;
    if (strategy == "greedy") {
      sampler = llm_sampler::CreateSampler("greedy", params);
    } else if (strategy == "temperature") {
      float t = std::stof(value);
      if (t <= 0.0f || t > 100.0f) {
        APP_LOG(AppLogLevel::WARNING, log_level,
                "Invalid temperature: %.2f (0 < temp <= 100); using default %.2f", t,
                params.temperature);
      } else {
        params.temperature = t;
      }
      sampler = llm_sampler::CreateSampler("temperature", params);
    } else if (strategy == "top-k") {
      int k = std::stoi(value);
      if (k <= 0 || k > 100000) {
        APP_LOG(AppLogLevel::WARNING, log_level,
                "Invalid top-k: %d (0 < k <= 100000); using default %d", k, params.topK);
      } else {
        params.topK = k;
      }
      sampler = llm_sampler::CreateSampler("top-k", params);
    } else if (strategy == "top-p") {
      float p = std::stof(value);
      if (p <= 0.0f || p > 1.0f) {
        APP_LOG(AppLogLevel::WARNING, log_level,
                "Invalid top-p: %.2f (0 < p <= 1.0); using default %.2f", p, params.topP);
      } else {
        params.topP = p;
      }
      sampler = llm_sampler::CreateSampler("top-p", params);
    } else if (strategy == "repetition-penalty") {
      float penalty = std::stof(value);
      if (penalty < 0.0f || penalty > 10.0f) {
        APP_LOG(AppLogLevel::WARNING, log_level,
                "Invalid repetition penalty: %.2f (0 <= penalty <= 10.0); using default %.2f",
                penalty, params.repetitionPenalty);
      } else {
        params.repetitionPenalty = penalty;
      }
      sampler = llm_sampler::CreateSampler("repetition-penalty", params);
    } else {
      APP_LOG(AppLogLevel::ERROR, log_level, "Unknown sampler strategy: %s", strategy.c_str());
      return nullptr;
    }

    if (sampler && seed != 0) sampler->SetSeed(seed);
    return sampler;
  } catch (const std::exception& e) {
    APP_LOG(AppLogLevel::ERROR, log_level, "Failed to parse sampler value '%s' for '%s': %s",
            value.c_str(), strategy.c_str(), e.what());
    return nullptr;
  }
}

/* Create the embedded tokenizer with the JSON tokenizer-config directory.
Returns nullptr if the tokenizer cannot be created. */
std::unique_ptr<llm_tokenizer::Tokenizer> createTokenizer(const std::string& tokenizer_path,
                                                          AppLogLevel log_level) {
  APP_LOG(AppLogLevel::INFO, log_level, "Creating embedded tokenizer from: %s",
          tokenizer_path.c_str());
  try {
    return llm_tokenizer::Tokenizer::FromDirectory(tokenizer_path);
  } catch (const std::exception& e) {
    APP_LOG(AppLogLevel::ERROR, log_level, "Failed to create embedded tokenizer: %s", e.what());
    return nullptr;
  }
}

/* Build the AppContext from the JSON config: reads the inference/tokenizer/
 * sampler sub-trees, creates the tokenizer, the vart-ml LLM runner, and the
 * sampler, and wires them into ctx. Returns false (after logging) on any
 * missing-config or creation failure. */
bool createContext(AppContext& ctx, const std::string& config_path, AppLogLevel log_level) {
  pt::ptree tree;
  try {
    pt::read_json(config_path, tree);
  } catch (const std::exception& e) {
    APP_LOG(AppLogLevel::ERROR, log_level, "Could not read config '%s': %s", config_path.c_str(),
            e.what());
    return false;
  }

  /* inference-config sub-tree: model-file is required. */
  const std::string modelFile = tree.get<std::string>("inference-config.model-file", "");
  if (modelFile.empty()) {
    APP_LOG(AppLogLevel::ERROR, log_level,
            "Config '%s' must set 'model-file'", config_path.c_str());
    return false;
  }

  ctx.SetLogLevel(log_level);
  ctx.SetAiAnalyzerProfiling(tree.get<bool>("inference-config.runner-options.ai-analyzer-profiling", false));
  ctx.SetInputIdsName(tree.get<std::string>("inference-config.runner-options.input-ids-name", "input_ids"));
  ctx.SetAttentionMaskName(
      tree.get<std::string>("inference-config.runner-options.attention-mask-name", "attention_mask"));
  ctx.SetLogitsName(tree.get<std::string>("inference-config.runner-options.logits-name", "logits"));
  ctx.SetInputTensorType(
      tree.get<std::string>("inference-config.runner-options.input-tensor-type", "CPU"));
  ctx.SetOutputTensorType(
      tree.get<std::string>("inference-config.runner-options.output-tensor-type", "CPU"));
  std::map<std::string, std::string> tensorTypeOverrides;
  if (auto overrides =
          tree.get_child_optional("inference-config.runner-options.tensor-type-overrides")) {
    for (const auto& kv : *overrides) {
      tensorTypeOverrides.emplace(kv.first, kv.second.get_value<std::string>());
    }
  }
  ctx.SetTensorTypeOverrides(std::move(tensorTypeOverrides));

  /* tokenizer-config sub-tree: prompt formatting and tokenizer location. */
  ctx.SetPromptFormat(parsePromptFormat(tree.get<std::string>("tokenizer-config.prompt-format", "chatml")));
  ctx.SetSystemPrompt(
      tree.get<std::string>("tokenizer-config.system-prompt", "You are a helpful assistant."));

  /* sampler-config sub-tree: anti-repetition filters applied on the runner. */
  ctx.SetRepetitionPenalty(tree.get<float>("sampler-config.rep-penalty", 1.0f));
  ctx.SetNoRepeatNgram(tree.get<int>("sampler-config.no-repeat-ngram", 0));

  /* Create the embedded tokenizer from the tokenizer directory (defaults to
     the model directory when tokenizer-path is absent). */
  const std::string tokenizerPath =
      tree.get<std::string>("tokenizer-config.tokenizer-path", modelFile);
  std::unique_ptr<llm_tokenizer::Tokenizer> tokenizer = createTokenizer(tokenizerPath, log_level);
  if (!tokenizer) {
    return false;
  }
  if (!ctx.SetTokenizer(std::move(tokenizer))) {
    APP_LOG(AppLogLevel::ERROR, log_level, "Failed to set tokenizer on runner");
    return false;
  }

  /* The compiled VAIML artifacts that vart-ml loads via create_runner live in
     the model directory. */
  const std::string runnerPath = modelFile;
  std::cout << "Loading LLM model (this may take 1-3 minutes on first run). Please wait...\n"
            << std::flush;
  if (!ctx.CreateLlmRunner(runnerPath)) {
    APP_LOG(AppLogLevel::ERROR, log_level, "Failed to initialize runner");
    return false;
  }

  /* Create and set the sampler. */
  const std::string samplerStr = tree.get<std::string>("sampler-config.sampler", "greedy");
  std::string strategy, value;
  if (!parseSamplerOptions(samplerStr, strategy, value)) {
    APP_LOG(AppLogLevel::ERROR, log_level, "Failed to parse sampler configuration: %s",
            samplerStr.c_str());
    return false;
  }
  const uint32_t seed = tree.get<uint32_t>("sampler-config.seed", 0);
  std::unique_ptr<ISampler> sampler = createSampler(strategy, value, seed, log_level);
  if (!sampler) {
    APP_LOG(AppLogLevel::ERROR, log_level, "Failed to create sampler for strategy '%s'",
            strategy.c_str());
    return false;
  }
  if (!ctx.SetSampler(std::move(sampler))) {
    APP_LOG(AppLogLevel::ERROR, log_level, "Failed to set sampler on runner");
    return false;
  }

  return true;
}
}  /* namespace */

/* ===== Main ===== */
/*
 * vart_llm entry point.
 *
 * Flow:
 * - Parse the command line (--app-config, --log-level; --help prints usage).
 * - Build the AppContext from the JSON config (tokenizer + runner + sampler).
 * - Run an interactive REPL: read a prompt from stdin, generate a response,
 *   and print it, looping until an empty line or EOF (Ctrl-D).
 *
 * Returns 0 on success, 1 on a configuration or generation failure.
 */
int main(int argc, char* argv[]) {
  Options opt{};
  po::options_description desc(
      "\nvart_llm — interactive LLM text generation on the vart-ml runner, with "
      "HuggingFace tokenization and configurable sampling."
      "\n\nAllowed options");
  add_options(desc, opt);

  const ParseOutcome parsed = parse_options(argc, argv, opt, desc);
  if (parsed == ParseOutcome::Help) {
    std::cout << desc << std::endl;
    return 0;
  }
  if (parsed == ParseOutcome::Error) {
    std::cerr << desc << std::endl;
    return 1;
  }

  AppLogLevel log_level = static_cast<AppLogLevel>(opt.log_level);

  APP_LOG(AppLogLevel::INFO, log_level, "Loading config from: %s", opt.config_path.c_str());
  AppContext ctx;
  if (!createContext(ctx, opt.config_path, log_level)) {
    return 1;
  }

  /* Interactive session: read a prompt from stdin and generate, looping until
     the user enters an empty line or sends EOF (Ctrl-D). */
  std::cout << "\nInteractive LLM session. Enter a prompt (empty line or Ctrl-D to quit).\n";
  std::string prompt;
  while (true) {
    std::cout << "\n> " << std::flush;
    if (!std::getline(std::cin, prompt)) {
      std::cout << std::endl;  // EOF (Ctrl-D)
      break;
    }
    if (prompt.empty()) break;
    std::cout << std::format("[User Prompt] {}\n", prompt);
    std::string result = ctx.Run(prompt);
    if (result.empty()) {
      APP_LOG(AppLogLevel::ERROR, log_level, "Generation failed for the given prompt");
      return 1;
    }
    std::cout << std::format("[Response] {}\n", result);

    /* Always surface prefill/decode performance, regardless of the configured log level. */
    const auto& stats = ctx.GetLastStats();
    std::cout << "\n";
    std::cout << std::format(
        "Prefill: {} tok in {:.1f} ms ({:.1f} tok/s) | Decode: {} tok in {:.1f} ms ({:.1f} tok/s)\n",
        stats.prefillTokens, stats.prefillTimeMs, stats.GetPrefillTokensPerSecond(),
        stats.decodeTokens, stats.decodeTimeMs, stats.GetDecodeTokensPerSecond());
    std::cout.flush();
  }

  return 0;
}
