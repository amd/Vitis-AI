/*
 * Copyright (C) 2026 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software
 * is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY
 * KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO
 * EVENT SHALL "AMD" BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT
 * OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE. Except as contained in this notice, the name of the AMD shall
 * not be used in advertising or otherwise to promote the sale, use or other
 * dealings in this Software without prior written authorization from AMD.
 *
 * Generic VART model runner implementation.
 * Based on AsuraModel / GarudaModel / RouteModel from ref/ but unified into
 * a single class parameterised on model name and cache path.
 */

#include "vart_multi_tenancy.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>

// ---------------------------------------------------------------------------
// Construction / Destruction
// ---------------------------------------------------------------------------

/**
 * @brief Construct a VartMultiTenancy
 * @details Stores the model name, cache directory, and tensor types for later
 *          use. No XRT device is opened at this stage.
 * @param model_name         Human-readable model identifier (e.g. "Model_1")
 * @param model_cache_dir    Path to the compiled VAIML model cache directory
 * @param input_tensor_type  Tensor type for IFM allocation (default: HW)
 * @param output_tensor_type Tensor type for OFM allocation (default: HW)
 */
VartMultiTenancy::VartMultiTenancy(const std::string& model_name,
                                   const std::string& model_cache_dir,
                                   vart::TensorType input_tensor_type,
                                   vart::TensorType output_tensor_type)
    : m_model_name(model_name),
      m_model_cache_dir(model_cache_dir),
      m_input_tensor_type(input_tensor_type),
      m_output_tensor_type(output_tensor_type) {}

/**
 * @brief Destructor – automatically deallocates any runner-allocated tensors
 */
VartMultiTenancy::~VartMultiTenancy() {
  deallocate_tensors();
}

// ---------------------------------------------------------------------------
// Initialization – create VART runner and fetch tensor metadata
// ---------------------------------------------------------------------------

/**
 * @brief Create the VART runner and fetch tensor metadata
 * @details Creates the VART runner using the model cache directory and the
 *          options from @p cfg. The per-model NPU column policy is forwarded
 *          to the runner via the std::any options bag:
 *
 *          - "start_column"        : @ref utils::ModelConfig::start_column
 *                                    (uint32_t, multiple of 4 in [0, 32]).
 *                                    Selects the first NPU column the model
 *                                    is placed on.
 *
 *          - "aie_columns_sharing" : @ref utils::ModelConfig::aie_columns_sharing
 *                                    (bool). true = shared/temporal (the
 *                                    column block is time-multiplexed with
 *                                    other models that map to the same
 *                                    columns); false = exclusive/spatial
 *                                    (the column block is owned by this
 *                                    model only).
 *
 *          Either option is only added to the bag when the corresponding
 *          "is_*_provided" flag is true; otherwise the runner uses its
 *          default placement.
 *
 *          After the runner is created, tensor metadata is fetched and
 *          validated.
 * @param cfg Model configuration parsed from the JSON config file
 * @return true on success, false on failure
 */
bool VartMultiTenancy::initialize(const utils::ModelConfig& cfg) {
  if (m_model_cache_dir.empty()) {
    log_error("[ERROR] ", m_model_name, ": model cache directory not set.");
    return false;
  }

  try {
    std::unordered_map<std::string, std::any> options = {
        {"input_tensor_type", std::string(m_input_tensor_type == vart::TensorType::CPU ? "CPU" : "HW")},
        {"output_tensor_type", std::string(m_output_tensor_type == vart::TensorType::CPU ? "CPU" : "HW")},
    };

    // Forward the placement policy to the runner only when explicitly set
    // in the JSON config; otherwise the runner picks its own default.
    if (cfg.is_start_column_provided) {
      options["start_column"] = cfg.start_column;
    }
    if (cfg.is_columns_sharing_provided) {
      options["aie_columns_sharing"] = cfg.aie_columns_sharing;
    }

    // Forward the application-level verbosity to the VART ML runner so
    // that its internal logging matches the user's CLI choice.
    const char* log_levels[] = {"ERROR", "WARNING", "INFO", "DEBUG"};
    int log_idx = std::clamp(g_verbose, 0, static_cast<int>(std::size(log_levels) - 1));
    options["log_level"] = std::string(log_levels[log_idx]);

    log_info("[INFO] ", m_model_name, ": creating VART runner with model cache: ", m_model_cache_dir);

    m_runner = vart::RunnerFactory::create_runner(vart::RunnerType::VAIML, m_model_cache_dir, options);

    if (!m_runner) {
      log_error("[ERROR] ", m_model_name, ": failed to create VART runner.");
      return false;
    }

    log_info("[INFO] ", m_model_name, ": VART runner created successfully.");

    if (!get_tensor_metadata()) {
      return false;
    }

    if (m_num_input_tensors == 0 || m_num_output_tensors == 0) {
      log_error("[ERROR] ", m_model_name, ": invalid tensor count \u2013 input: ", m_num_input_tensors,
                ", output: ", m_num_output_tensors);
      return false;
    }

    return true;
  } catch (const std::exception& ex) {
    log_error("[ERROR] ", m_model_name, " initialization exception: ", ex.what());
    return false;
  }
}

// ---------------------------------------------------------------------------
// Tensor metadata
// ---------------------------------------------------------------------------

/**
 * @brief Query the runner for input/output tensor counts and metadata
 * @details Retrieves NpuTensorInfo vectors for both HW and CPU views
 *          and logs the discovered tensor counts.
 * @return true on success, false if the runner throws
 */
bool VartMultiTenancy::get_tensor_metadata() {
  try {
    m_num_input_tensors = m_runner->get_num_input_tensors();
    m_num_output_tensors = m_runner->get_num_output_tensors();

    m_input_hw_tensors_info = m_runner->get_tensors_info(vart::TensorDirection::INPUT, vart::TensorType::HW);
    m_input_cpu_tensors_info = m_runner->get_tensors_info(vart::TensorDirection::INPUT, vart::TensorType::CPU);
    m_output_hw_tensors_info = m_runner->get_tensors_info(vart::TensorDirection::OUTPUT, vart::TensorType::HW);
    m_output_cpu_tensors_info = m_runner->get_tensors_info(vart::TensorDirection::OUTPUT, vart::TensorType::CPU);

    log_info("[INFO] ", m_model_name, " metadata \u2013 inputs: ", m_num_input_tensors,
             ", outputs: ", m_num_output_tensors);
    return true;
  } catch (const std::exception& ex) {
    log_error("[ERROR] ", m_model_name, ": failed to get tensor metadata: ", ex.what());
    return false;
  }
}

/**
 * @brief Return the number of input tensors discovered during initialization
 * @return Number of input tensors
 */
size_t VartMultiTenancy::get_num_input_tensors() const {
  return m_num_input_tensors;
}

/**
 * @brief Return the number of output tensors discovered during initialization
 * @return Number of output tensors
 */
size_t VartMultiTenancy::get_num_output_tensors() const {
  return m_num_output_tensors;
}

/**
 * @brief Return input tensor metadata for the requested tensor type
 * @param type Tensor type (HW or CPU)
 * @return Reference to the vector of NpuTensorInfo for input tensors
 */
const std::vector<vart::NpuTensorInfo>& VartMultiTenancy::get_input_tensors_info(vart::TensorType type) const {
  return (type == vart::TensorType::CPU) ? m_input_cpu_tensors_info : m_input_hw_tensors_info;
}

/**
 * @brief Return output tensor metadata for the requested tensor type
 * @param type Tensor type (HW or CPU)
 * @return Reference to the vector of NpuTensorInfo for output tensors
 */
const std::vector<vart::NpuTensorInfo>& VartMultiTenancy::get_output_tensors_info(vart::TensorType type) const {
  return (type == vart::TensorType::CPU) ? m_output_cpu_tensors_info : m_output_hw_tensors_info;
}

/**
 * @brief Return the TensorType used to allocate input (IFM) buffers
 * @return TensorType for IFM allocation
 */
vart::TensorType VartMultiTenancy::get_ifm_alloc_tensor_type() const {
  return m_input_tensor_type;
}

/**
 * @brief Return the TensorType used to allocate output (OFM) buffers
 * @return TensorType for OFM allocation
 */
vart::TensorType VartMultiTenancy::get_ofm_alloc_tensor_type() const {
  return m_output_tensor_type;
}

/**
 * @brief Allocate IFM and OFM NpuTensor buffers via the runner
 * @details Uses the runner's allocate_npu_tensor() API. The batch size is
 *          obtained from runner->get_batch_size() so a single IFM file may
 *          carry multiple frames concatenated end-to-end.
 *          Populates m_ifm_tensors and m_ofm_tensors for subsequent
 *          load/infer/save calls.
 * @return true on success, false if any tensor allocation fails
 */
bool VartMultiTenancy::allocate_tensors() {
  try {
    m_batch_size = m_runner->get_batch_size();
    if (m_batch_size == 0) {
      log_error("[ERROR] ", m_model_name, ": runner reported batch_size=0.");
      return false;
    }
    m_ifm_tensors.resize(m_batch_size);
    m_ofm_tensors.resize(m_batch_size);

    const auto& in_info = get_input_tensors_info(m_input_tensor_type);
    const auto& out_info = get_output_tensors_info(m_output_tensor_type);

    for (size_t batch = 0; batch < m_batch_size; ++batch) {
      for (size_t i = 0; i < m_num_input_tensors; ++i) {
        auto tensor = m_runner->allocate_npu_tensor(in_info[i]);
        m_ifm_tensors[batch].push_back(std::move(tensor));
      }

      for (size_t i = 0; i < m_num_output_tensors; ++i) {
        auto tensor = m_runner->allocate_npu_tensor(out_info[i]);
        m_ofm_tensors[batch].push_back(std::move(tensor));
      }
    }

    m_tensors_allocated = true;
    log_info("[INFO] ", m_model_name, ": [Step 1/4 Allocate] batch_size=", m_batch_size, ", allocated ",
             m_num_input_tensors, " IFM and ", m_num_output_tensors, " OFM tensor(s) per batch.");
    return true;
  } catch (const std::exception& ex) {
    log_error("[ERROR] ", m_model_name, ": tensor allocation failed: ", ex.what());
    return false;
  }
}

/**
 * @brief Deallocate all IFM and OFM tensors previously allocated via the runner
 * @details Safe to call multiple times; no-op when no tensors are allocated.
 */
void VartMultiTenancy::deallocate_tensors() {
  if (!m_tensors_allocated || !m_runner)
    return;

  m_ifm_tensors.clear();
  m_ofm_tensors.clear();
  m_tensors_allocated = false;
  log_info("[INFO] ", m_model_name, ": tensors deallocated.");
}

// ---------------------------------------------------------------------------
// IFM loading – read binary files into runner-allocated tensors
// ---------------------------------------------------------------------------

/**
 * @brief Load IFM data from binary files into runner-allocated input tensors
 * @details For each input tensor, the tensor name is looked up in
 *          cfg.ifm_node_file_map. The map value is treated as a full
 *          filesystem path. The IFM binary may contain a single frame or
 *          multiple frames concatenated end-to-end; the runner's batch_size
 *          determines how many frames are read into successive
 *          m_ifm_tensors[batch] slots. Returns false if a tensor name is not
 *          found in the map (node-name mismatch) or if the file size is not
 *          a positive multiple of the per-frame tensor size.
 * @param cfg Model configuration with ifm_node_file_map (full paths)
 * @return true on success, false on failure
 */
bool VartMultiTenancy::load_ifms(const utils::ModelConfig& cfg) {
  try {
    if (cfg.ifm_node_file_map.empty()) {
      log_error("[ERROR] ", m_model_name, ": ifm_node_file_map is empty.");
      return false;
    }

    if (!m_tensors_allocated || m_ifm_tensors.empty()) {
      log_error("[ERROR] ", m_model_name, ": tensors not allocated. Call allocate_tensors() first.");
      return false;
    }

    const auto& info = get_input_tensors_info(m_input_tensor_type);

    for (size_t j = 0; j < m_num_input_tensors; ++j) {
      const std::string& tensor_name = info[j].name;
      auto it = cfg.ifm_node_file_map.find(tensor_name);
      if (it == cfg.ifm_node_file_map.end()) {
        std::ostringstream oss;
        oss << "[ERROR] " << m_model_name << ": node name mismatch for input tensor " << j << ".\n"
            << "  Expected node name : '" << tensor_name << "'\n"
            << "  Provided node names: [";
        bool first = true;
        for (const auto& [key, val] : cfg.ifm_node_file_map) {
          if (!first)
            oss << ", ";
          oss << "'" << key << "'";
          first = false;
        }
        oss << "]";
        log_error(oss.str());
        return false;
      }

      std::filesystem::path file_path = it->second;

      std::ifstream ifs(file_path, std::ios::binary | std::ios::ate);
      if (!ifs.is_open()) {
        log_error("[ERROR] ", m_model_name, ": failed to open IFM file: ", file_path.string());
        return false;
      }

      const auto file_size = static_cast<size_t>(ifs.tellg());
      const size_t per_frame = info[j].size_in_bytes;

      // The IFM file must hold at least one full frame and be a positive
      // multiple of the per-frame tensor size. Partial frame counts (fewer
      // than batch_size) are allowed: the runner handles partial-batch
      // execution; only the first `frames_in_file` batch slots are
      // populated and a warning is logged.
      if (per_frame == 0 || file_size == 0 || file_size % per_frame != 0) {
        log_error("[ERROR] ", m_model_name, ": IFM file size mismatch for tensor '", tensor_name, "' (",
                  file_path.string(), ").\n", "  Expected size : positive multiple of ", per_frame,
                  " bytes (per-frame size)\n", "  Provided size : ", file_size, " bytes");
        return false;
      }

      const size_t frames_in_file = file_size / per_frame;
      const size_t frames_to_read = std::min(frames_in_file, m_batch_size);

      if (frames_in_file < m_batch_size) {
        log_warn("[WARN] ", m_model_name, ": IFM file '", file_path.string(), "' for tensor '", tensor_name,
                 "' contains ", frames_in_file, " frame(s) but model batch_size is ", m_batch_size,
                 ". Proceeding with partial batch \u2013 VART ML will handle the partial-frame execution.");
      } else if (frames_in_file > m_batch_size) {
        log_warn("[WARN] ", m_model_name, ": IFM file '", file_path.string(), "' for tensor '", tensor_name,
                 "' contains ", frames_in_file, " frame(s); only the first ", m_batch_size,
                 " (batch_size) will be loaded.");
      }

      ifs.seekg(0, std::ios::beg);

      // Read one frame per batch slot from consecutive regions of the file.
      // When frames_in_file < m_batch_size (partial batch), only the first
      // frames_to_read batch slots are populated. The application tracks
      // this via m_actual_batch_size so that infer_execute() and save_ofms()
      // only process the valid frames — no dummy/stale data is forwarded
      // to the runner or written to disk.
      for (size_t b = 0; b < frames_to_read; ++b) {
        void* data = m_ifm_tensors[b][j].get_virtual_address();
        ifs.read(static_cast<char*>(data), static_cast<std::streamsize>(per_frame));
      }

      // Track the actual number of valid frames across all input tensors.
      // For the first tensor, initialise m_actual_batch_size; for subsequent
      // tensors, take the minimum (all inputs must agree on frame count).
      if (j == 0) {
        m_actual_batch_size = frames_to_read;
      } else {
        m_actual_batch_size = std::min(m_actual_batch_size, frames_to_read);
      }

      log_info("[INFO] ", m_model_name, ": [Step 2/4 Load IFMs] loaded '", tensor_name, "' from ", file_path.string(),
               " (", frames_to_read, "/", m_batch_size, " frame(s) x ", per_frame, " bytes)");
    }

    // For partial batches, shrink the tensor vectors to the actual number
    // of loaded frames. This frees the unused trailing NPU tensor slots
    // and lets infer_execute() / save_ofms() pass the vectors directly
    // without needing temporary partial views.
    if (m_actual_batch_size < m_batch_size) {
      m_ifm_tensors.resize(m_actual_batch_size);
      m_ofm_tensors.resize(m_actual_batch_size);
      log_warn("[WARN] ", m_model_name, ": executing partial batch (", m_actual_batch_size, "/", m_batch_size,
               " frames).");
    }

    return true;
  } catch (const std::exception& ex) {
    log_error("[ERROR] ", m_model_name, ": load_ifms failed: ", ex.what());
    return false;
  }
}

// ---------------------------------------------------------------------------
// Random IFM fill (used by --dry-run / -d dry-run mode)
// ---------------------------------------------------------------------------

/**
 * @brief Fill all allocated IFM tensors with random uint8_t bytes
 * @details Used by the --dry-run / -d dry-run mode. No IFM files are
 *          opened. Writes random bytes directly into each tensor's virtual
 *          address. Tensors must be allocated first via allocate_tensors().
 * @return true on success, false if tensors are not yet allocated
 */
bool VartMultiTenancy::fill_random_ifms() {
  if (!m_tensors_allocated || m_ifm_tensors.empty()) {
    log_error("[ERROR] ", m_model_name, ": tensors not allocated. Call allocate_tensors() first.");
    return false;
  }

  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<int> dist(0, 255);

  const auto& info = get_input_tensors_info(m_input_tensor_type);

  for (size_t b = 0; b < m_batch_size; ++b) {
    for (size_t j = 0; j < m_num_input_tensors; ++j) {
      uint8_t* data = static_cast<uint8_t*>(m_ifm_tensors[b][j].get_virtual_address());
      const size_t nbytes = info[j].size_in_bytes;
      for (size_t k = 0; k < nbytes; ++k) {
        data[k] = static_cast<uint8_t>(dist(gen));
      }
    }
  }

  // In dry-run mode all batch slots are populated with random data,
  // so the actual batch size equals the full model batch size.
  m_actual_batch_size = m_batch_size;

  log_info("[INFO] ", m_model_name, ": [Step 2/4 Load IFMs] dry-run: filled ", m_num_input_tensors, " IFM tensor(s) x ",
           m_batch_size, " batch(es) with random bytes (no files read).");
  return true;
}

// ---------------------------------------------------------------------------
// OFM saving – read from runner-allocated tensors and write binary files
// ---------------------------------------------------------------------------

/**
 * @brief Save OFM data from runner-allocated output tensors to binary files
 * @details One file per OFM tensor node named
 *          <node_name>_<shape>_<datatype>.bin. When
 *          batch_size > 1 all batch frames are concatenated into the same
 *          file. Creates the output directory if it does not already exist.
 * @param cfg Model configuration with ofm_dir
 * @return true on success, false on failure
 */
bool VartMultiTenancy::save_ofms(const utils::ModelConfig& cfg, size_t model_index) {
  try {
    if (cfg.ofm_dir.empty()) {
      log_error("[ERROR] ", m_model_name, ": ofm_dir is empty.");
      return false;
    }

    if (!m_tensors_allocated || m_ofm_tensors.empty()) {
      log_error("[ERROR] ", m_model_name, ": tensors not allocated.");
      return false;
    }

    // Create per-model subdirectory: ofm_model_1, ofm_model_2, ...
    std::filesystem::path model_ofm_dir =
        std::filesystem::path(cfg.ofm_dir) / ("ofm_model_" + std::to_string(model_index + 1));
    std::filesystem::create_directories(model_ofm_dir);

    const auto& info = get_output_tensors_info(m_output_tensor_type);

    for (size_t j = 0; j < m_num_output_tensors; ++j) {
      std::string filename =
          info[j].name + "_" + shape_to_string(info[j].shape) + "_" + data_type_to_string(info[j].data_type) + ".bin";
      auto ofm_path = model_ofm_dir / filename;
      std::ofstream ofs(ofm_path, std::ios::binary);
      if (!ofs.is_open()) {
        log_error("[ERROR] ", m_model_name, ": failed to open OFM file: ", ofm_path.string());
        return false;
      }

      // Write only the frames that were actually processed during
      // inference (m_actual_batch_size). When the user provides fewer
      // frames than the model's batch size, only those valid output
      // frames are saved — no stale/uninitialised data is written.
      const size_t frames_to_save = (m_actual_batch_size > 0) ? m_actual_batch_size : m_batch_size;
      size_t total_bytes = 0;
      for (size_t b = 0; b < frames_to_save; ++b) {
        const void* data = m_ofm_tensors[b][j].get_virtual_address();
        ofs.write(static_cast<const char*>(data), static_cast<std::streamsize>(info[j].size_in_bytes));
        total_bytes += info[j].size_in_bytes;
      }

      log_info("[INFO] ", m_model_name, ": [Step 4/4 Save OFMs] saved '", info[j].name, "' (", frames_to_save, "/",
               m_batch_size, " batch frame(s)) -> ", ofm_path.string(), " (", total_bytes, " bytes)");
    }
    return true;
  } catch (const std::exception& ex) {
    log_error("[ERROR] ", m_model_name, ": save_ofms failed: ", ex.what());
    return false;
  }
}

// ---------------------------------------------------------------------------
// Inference execution
// ---------------------------------------------------------------------------

/**
 * @brief Execute model inference on the NPU
 * @details Passes the IFM and OFM tensor batches to the runner.
 *          On exception returns vart::StatusCode::FAILURE.
 * @return StatusCode from runner->execute()
 */
vart::StatusCode VartMultiTenancy::infer_execute() {
  try {
    // Guard: this should never be reached because load_ifms() and
    // fill_random_ifms() already validate and set m_actual_batch_size,
    // but check defensively in case the call sequence is wrong.
    if (m_actual_batch_size == 0) {
      log_error("[ERROR] ", m_model_name, ": cannot execute inference \u2013 no IFM frames have been loaded.");
      return vart::StatusCode::FAILURE;
    }

    auto status = m_runner->execute(m_ifm_tensors, m_ofm_tensors);
    return status;
  } catch (const std::exception&) {
    log_error("[ERROR] ", m_model_name, ": exception during inference execution.");
    return vart::StatusCode::FAILURE;
  }
}
