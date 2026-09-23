/**
 * @file common.h
 *
 * @copyright Copyright 2024 Advanced Micro Devices Inc.
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
 */

#include <fstream>
#include <getopt.h>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <unistd.h>
#include <vector>

#include <vart_ml_runner/vart_runner_factory.hpp>

const std::string red("\033[1;31m");
const std::string green("\033[1;32m");
const std::string yellow("\033[1;33m");
const std::string reset("\033[0m");

/**
 * @brief Integer identifiers for every recognised command-line option.
 *
 * Used as the val field of struct option entries so read_options() can
 * dispatch in a switch without re-examining argv strings.
 */
enum OptionVal
{
	// Universal options — present in every demo's long_options table.
	OPT_BATCH_SIZE = 1,
	OPT_CHANNEL_ORDER,
	OPT_GOLD_FILE,
	OPT_NO_GOLD_OUTPUT,
	OPT_IMG_PATH,
	OPT_LABELS,
	OPT_MEAN,
	OPT_NB_IMAGES,
	OPT_NETWORK,
	OPT_RESIZE_TYPE,
	OPT_SNAPSHOT,
	OPT_STD,
	// Demo-specific options — add only to the demos that support them.
	OPT_NB_THREADS,
	OPT_DATA_FORMAT,
	OPT_FORCE_IN_DDR,
	OPT_FORCE_IN_OUT_DDR,
	OPT_FORCE_OUT_DDR,
	OPT_FPGA_ARCH,
	OPT_REPEAT,
	OPT_NO_FPS_PROGRESS,
	OPT_USE_EXTERNAL_QUANT,
	OPT_USE_ONNX_SUBGRAPHS,
	OPT_USE_SNAPSHOT_GOLD,
};

/**
 * @brief Returns the set of long options common to all demos.
 *
 * Each demo should call this, append its own demo-specific entries, then
 * terminate the vector with {nullptr, 0, nullptr, 0} before passing
 * .data() to read_options().
 */
inline std::vector<struct option> base_opts()
{
	return {
		{ "batchSize", required_argument, nullptr, OPT_BATCH_SIZE },
		{ "channelOrder", required_argument, nullptr, OPT_CHANNEL_ORDER },
		{ "goldFile", required_argument, nullptr, OPT_GOLD_FILE },
		{ "noGoldOutput", no_argument, nullptr, OPT_NO_GOLD_OUTPUT },
		{ "imgPath", required_argument, nullptr, OPT_IMG_PATH },
		{ "labels", required_argument, nullptr, OPT_LABELS },
		{ "mean", required_argument, nullptr, OPT_MEAN },
		{ "nbImages", required_argument, nullptr, OPT_NB_IMAGES },
		{ "network", required_argument, nullptr, OPT_NETWORK },
		{ "resizeType", required_argument, nullptr, OPT_RESIZE_TYPE },
		{ "snapshot", required_argument, nullptr, OPT_SNAPSHOT },
		{ "std", required_argument, nullptr, OPT_STD },
	};
}

struct options
{
	// --- Set by read_options() ---
	std::vector<std::string>           snapshots;
	std::string                        imgPath;
	std::vector<std::string>           images_paths;
	std::map<std::string, std::string> gold;
	std::vector<std::string>           categories;
	bool                               noGoldOutput     = false;
	bool                               showFpsProgress  = true;
	bool                               in_isNative      = false;
	bool                               out_isNative     = false;
	bool                               useExternalQuant = false;
	bool                               useOnnxSubgraphs = false;
	bool                               useSnapshotGold  = false;
	bool                               isMultiThread    = false;
	size_t                             nbThreads        = 1;
	size_t                             repeatCnt        = 1;
	std::string                        fpga_arch        = "aieml";
	std::vector<std::vector<uint8_t>>  ddr_in_list;
	std::vector<std::vector<uint8_t>>  ddr_out_list;
	// Preprocessing parameters (--channelOrder / --mean / --std / --resizeType)
	std::vector<size_t>                      channelOrder = { 0, 1, 2 };
	std::vector<float>                       mean         = { 0.0f, 0.0f, 0.0f };
	std::vector<float>                       stddev       = { 255.0f, 255.0f, 255.0f };
	std::pair<std::string, std::vector<int>> resizeType   = { "PanScan", { 224, 224 } };

	// --- Set by read_images_options() (call only after the runner is created) ---
	size_t                                                       nbImages  = 0;
	size_t                                                       batchSize = 0;
	std::vector<std::map<std::string, std::string>>              data_types;
	std::vector<std::map<std::string, std::vector<std::string>>> goldFiles;
};

/**
 * @brief Check if the file is a directory
 *
 * @param dir Path to the file to check
 * @return bool True if dir points to a directory, false otherwise
 */
bool isDirectory(const char* dir);

/**
 * @brief Check if the file is a regular file
 *
 * @param path Path to the file to check
 * @param mode Mode to open the file for
 * @return bool True if path points to a regular file, false otherwise
 */
bool isFile(const char* path, const char* mode);

/**
 * @brief Display help for the program and exit
 *
 * @param cmd        Full argv[0] of the running demo
 * @param reason     Non-empty string printed before the help text, or ""
 * @param long_opts  The same long_options table passed to read_options()
 * @param multi_snapshot  True if the demo accepts PATH[+PATH]... snapshots
 */
void usage(const std::string&   cmd,
           const std::string&   reason,
           const struct option* long_opts,
           bool                 multi_snapshot = false);

/**
 * @brief Parse command-line options using getopt_long and populate options
 *
 * @param argc          Argument count
 * @param argv          Argument vector
 * @param options       Struct that will be populated with parsed values
 * @param long_opts     The demo's long_options table (built from base_opts() +
 *                      demo-specific entries, terminated with {nullptr,0,nullptr,0})
 * @param multi_snapshot  True if the demo accepts PATH[+PATH]... for --snapshot
 * @return int The Vart ML error (vart_ml_error_id) code of the operation.
 */
int read_options(int                  argc,
                 char*                argv[],
                 struct options&      options,
                 const struct option* long_opts,
                 bool                 multi_snapshot = false);

/**
 * @brief Resolve image paths and batch/image counts from the runner batch size
 *
 * Must be called after read_options() and after the runner has been created,
 * because the runner's batch size is needed to apply defaults and validate the
 * user-supplied --batchSize.  The expected call sequence is:
 *
 *   read_options(argc, argv, options, long_opts);   // parse CLI
 *   runner = RunnerFactory::create_runner(..., options.snapshots[0], ...);
 *   read_images_options(options, runner->get_batch_size());
 *
 * @param options struct populated by read_options(); further fields are set here
 * @param default_batchSize batch size reported by the snapshot runner
 * @return int The Vart ML error (vart_ml_error_id) code of the operation.
 */
int read_images_options(struct options& options, size_t default_batchSize);

/**
 * @brief Get the k results with the highest probability
 *
 * @param buf Vector of results for each label
 * @param k Number of top value requested
 * @return std::vector<std::pair<size_t, float>> k pair of label number and probability value
 */
std::vector<std::pair<size_t, float>> topk(const std::vector<float>& buf, size_t k);

/**
 * @brief Get all labels
 *
 * @param labels_file Labels file path
 * @param categories Returned vector of labels
 * @return int The Vart ML error (vart_ml_error_id) code of the operation.
 */
int read_categories(const std::string& labels_file, std::vector<std::string>& categories);

/**
 * @brief Get all correct labels form the gold file
 *
 * @param gold_file Gold file path
 * @param gold Returned map with file names as key and gold labels as value
 * @return int The Vart ML error (vart_ml_error_id) code of the operation.
 */
int read_gold(const std::string& gold_file, std::map<std::string, std::string>& gold);

/**
 * @brief Configure DDR lists if empty or check if forced DDRs are correct
 *
 * @param runner The VART runner
 * @param ddr_in_list Input's DDRs list
 * @param ddr_out_list Output's DDRs list
 * @return int The Vart ML error (vart_ml_error_id) code of the operation.
 */
int configure_ddr_lists(std::shared_ptr<vart::Runner> runner,
                        std::vector<uint8_t>&         ddr_in_list,
                        std::vector<uint8_t>&         ddr_out_list);

/**
 * @brief Apply the sofmax function to all elements
 *
 * @param buf Vector of element on which to apply the softmax function
 */
void softmax(std::vector<float>& buf);

/**
 * @brief Allocate contiguous batch tensor buffers for ultrascale
 *
 * @param options Program options
 * @param runner The VART runner
 * @param tensor_info Info of the tensor to allocate buffers for
 * @param batchSize_curr Size of the batch to allocate (takes into account incomplete batches)
 * @param batch_tensors Vector of tensors to put the allocated tensors in
 * @param buffer_type Type of the buffers to allocate (for error log message)
 * @return int The Vart ML error (vart_ml_error_id) code of the operation.
 */
int allocate_ultrascale_batch_tensors(const struct options&                      options,
                                      std::shared_ptr<vart::Runner>              runner,
                                      const vart::NpuTensorInfo&                 tensor_info,
                                      size_t                                     batchSize_curr,
                                      std::vector<std::vector<vart::NpuTensor>>& batch_tensors,
                                      const char*                                buffer_type);

/**
 * @brief Apply the preprocessing to a set of images
 *
 * Preprocess all input images. If quantization option is enabled, proceed with quantization of images as
 * well.
 * Note: depending on whether or not input data is assumed in native format of not, re-organize data in
 * a way that does not require further byte-level manipulation (force_native_input).
 *
 * @param options Program options
 * @param inbuf Input tensors to match preprocessing of images to
 * @param img_idx Index of first image
 * @param coeff Quantization parameters for a each tensor
 * @return int The Vart ML error (vart_ml_error_id) code of the operation.
 */
int preprocess_batch(const struct options&                      options,
                     std::vector<std::vector<vart::NpuTensor>>& inbuf,
                     size_t                                     img_idx,
                     const std::vector<float>&                  coeff);

/**
 * @brief Compare the results to gold results and display results
 *
 * @param options Program options
 * @param first_image_index Index of the first image in the batch
 * @param results Inference results
 * @param nbComparedImages Number of images that got compared to gold results
 * @param accuracy[2] Array of accuracies to fill
 * @param network Network name to display
 */
void compare_gold(const struct options&                  options,
                  size_t                                 first_image_index,
                  const std::vector<std::vector<float>>& results,
                  size_t&                                nbComparedImages,
                  float                                  accuracy[2],
                  const std::string&                     network);

/**
 * @brief Prints the accuracy summary without header
 *
 * @param network Network name to be displayed
 * @param accuracy Accuracy computed by demo
 * @param nbComparedImages Number of images compared to a gold file
 */
void print_accuracy_summary_headless(const std::string& network, float accuracy[2], size_t nbComparedImages);

/**
 * @brief Prints the accuracy summary
 *
 * @param network Network name as given to program
 * @param accuracy Accuracy computed by demo
 * @param nbComparedImages Number of images compared to a gold file
 */
void print_accuracy_summary(const std::string& network, float accuracy[2], size_t nbComparedImages);

/**
 * @brief Get minimum or maximum batch size from multiple runners
 *
 * @param is_max If true, returns maximum; if false, returns minimum
 * @param runners Vector of runners
 * @return size_t The min or max batch size
 */
size_t get_min_or_max_batch_size(bool is_max, std::vector<std::shared_ptr<vart::Runner>>& runners);

/**
 * @brief Allocate NPU tensor buffers for a runner
 *
 * @param runner The VART runner
 * @param direction INPUT or OUTPUT
 * @param tensor_type CPU or HW
 * @param batch_size Number of batches to allocate
 * @return std::vector<std::vector<vart::NpuTensor>> Allocated tensors [batch][tensor_idx]
 */
std::vector<std::vector<vart::NpuTensor>> allocate_tensor_buffers(std::shared_ptr<vart::Runner> runner,
                                                                  vart::TensorDirection         direction,
                                                                  size_t                        batch_size);

template <typename T>
struct is_vector : std::false_type
{
};

template <typename T, typename A>
struct is_vector<std::vector<T, A>> : std::true_type
{
};

/**
 * @brief Recursively free the buffers of a (possibly nested) tensor collection.
 *
 * Base case: calls std::free() on the buffer of a vart::NpuTensor.
 * Recursive case: iterates over a std::vector and recurses into each element.
 *
 * @tparam T  vart::NpuTensor or std::vector<...> of any nesting depth whose
 *            innermost type is vart::NpuTensor.
 * @param tensors  The tensor or collection whose buffers must be freed.
 */
template <typename T>
void free_tensor_buffers(T& tensors)
{
	if constexpr (std::is_same_v<T, vart::NpuTensor>)
		std::free(tensors.get_buffer());
	else
	{
		static_assert(is_vector<T>::value,
		              "free_tensor_buffers: T must be vart::NpuTensor or std::vector thereof");
		for (auto& inner : tensors)
			free_tensor_buffers(inner);
	}
}

/**
 * @brief Fill input buffers with random dummy data
 *
 * @param tensors Input tensors to fill [batch][tensor_idx]
 */
void fill_dummy_buffers(std::vector<std::vector<vart::NpuTensor>>& tensors);

/**
 * @brief Extract float results from output tensors
 *
 * @param runner The VART runner
 * @param output_tensors Output tensors [batch][tensor_idx]
 * @param batch_size Current batch size
 * @return std::vector<std::vector<float>> Results [batch*outputs][values]
 */
std::vector<std::vector<float>>
extract_float_results(std::shared_ptr<vart::Runner>                    runner,
                      const std::vector<std::vector<vart::NpuTensor>>& output_tensors,
                      size_t                                           batch_size);

/**
 * @brief Unquantize given tensors and extract results
 *
 * @param runner The VART runner
 * @param output_tensors Output tensors [batch][tensor_idx]
 * @param tensor_infos The info of the given output tensors
 * @param apply_softmax Whether softmax should be applied
 * @param results The extracted results
 * @return int The Vart ML error (vart_ml_error_id) code of the operation.
 */
int unquantize_tensors_buffers(std::shared_ptr<vart::Runner>                    runner,
                               const std::vector<std::vector<vart::NpuTensor>>& output_tensors,
                               const std::vector<vart::NpuTensorInfo>&          tensor_infos,
                               bool                                             apply_softmax,
                               std::vector<std::vector<float>>&                 results);

/**
 * @brief Display FPS progress during execution
 *
 * @param current_images Current number of images processed
 * @param total_images Total number of images to process
 * @param elapsed_seconds Elapsed time in seconds
 */
void display_fps_progress(size_t current_images, size_t total_images, double elapsed_seconds);

/**
 * @brief Return the number of heap bytes available for new allocations.
 *
 * Reads /proc/meminfo and subtracts CmaFree (CMA pages are included in
 * MemAvailable but cannot be used by malloc). Returns SIZE_MAX if the
 * value cannot be read.
 */
size_t get_mem_available_bytes();

/**
 * @brief Return the kernel commit headroom in bytes.
 *
 * How much more virtual memory the kernel will allow to be committed
 * before refusing new anonymous mappings (CommitLimit - Committed_AS).
 * Returns SIZE_MAX if the value cannot be read.
 */
size_t get_commit_headroom_bytes();

/**
 * @brief Simulate the round-robin DDR allocation and return bytes needed per bank.
 *
 * Mirrors the ddr_list[(b + i) % ddr_list.size()] allocation loop so the
 * caller can check per-bank headroom before actually allocating.
 *
 * @param tensors    Tensor descriptors whose size_in_bytes will be summed.
 * @param ddr_list   Round-robin list of DDR bank indices.
 * @param nbBatches  Number of batches to simulate.
 * @param batchSize  Number of samples per batch.
 * @param nbImages   Total number of images (last batch may be smaller).
 * @return Map of bank_index -> bytes needed on that bank.
 */
std::map<uint8_t, size_t> compute_npu_ddr_needed_per_bank(const std::vector<vart::NpuTensorInfo>& tensors,
                                                          const std::vector<uint8_t>&             ddr_list,
                                                          size_t                                  nbBatches,
                                                          size_t                                  batchSize,
                                                          size_t                                  nbImages);

enum class MemBudgetResult
{
	OK,
	FALLBACK_ON_THE_FLY,
	FATAL
};

/**
 * @brief Check whether enough heap memory is available before allocating input buffers.
 *
 * @param needed_all      Total bytes needed to pre-allocate all batches across all runners.
 * @param needed_fly      Bytes needed for a single batch (on-the-fly fallback).
 * @param onnx_scratch    Bytes reserved for ONNX runtime scratch allocations.
 * @param nbBatches       Total number of batches (used in warning messages).
 * @param inbuf_alloc_fail Set to true if the on-the-fly fallback should be used.
 * @return int vart_ml_error code; non-zero means not even a single batch fits.
 */
int check_mem_budget(size_t needed_all,
                     size_t needed_fly,
                     size_t onnx_scratch,
                     size_t nbBatches,
                     int&   inbuf_alloc_fail);

/**
 * @brief Check whether enough NPU DDR is available before allocating native buffers.
 *
 * @param needed_all      Per-bank bytes needed to pre-allocate all batches across all runners.
 * @param needed_fly      Per-bank bytes needed for a single batch (on-the-fly fallback).
 * @param runner          Any runner (used to query get_ddr_free_bytes per bank).
 * @param nbBatches       Total number of batches (used in warning messages).
 * @param inbuf_alloc_fail Set to true if the on-the-fly fallback should be used.
 * @return int vart_ml_error code; non-zero means not even a single batch fits.
 */
int check_npu_ddr_budget(const std::map<uint8_t, size_t>& needed_all,
                         const std::map<uint8_t, size_t>& needed_fly,
                         std::shared_ptr<vart::Runner>    runner,
                         size_t                           nbBatches,
                         int&                             inbuf_alloc_fail);
