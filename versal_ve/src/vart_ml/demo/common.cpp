/**
 * @file common.cpp
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

#include <algorithm>
#include <ctime>
#include <dirent.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>

#include <cmath>
#include <unistd.h>

#include <opencv2/opencv.hpp>

#include "common.h"
#include "utils/log.h"
#include "vart_ml_runner/vart_runner_factory.hpp"

#define DEFAULT_NB_BATCH 10

static std::pair<std::string, std::string> parse_gold(const std::string& path, const std::string& dir_prefix)
{
	std::string filename  = path.substr(dir_prefix.size());
	filename              = filename.substr(0, filename.rfind('.'));
	size_t      type_sep  = filename.rfind('_');
	std::string type      = filename.substr(type_sep + 1);
	size_t      batch_sep = filename.rfind('_', type_sep - 1);
	std::string name      = filename.substr(0, batch_sep);

	return std::make_pair(name, type);
}

bool isDirectory(const char* dir)
{
	DIR* d = nullptr;

	d = opendir(dir);
	if (!d)
		return false;

	closedir(d);
	return true;
}

bool isFile(const char* path, const char* mode)
{
	FILE* f = nullptr;

	f = fopen(path, mode);
	if (!f)
		return false;

	fclose(f);
	return true;
}

static int channelIdx(char channel)
{
	static const char channels[] = { 'B', 'G', 'R' };

	auto ptr = std::find(channels, channels + 3, channel);

	// Getting index from pointer
	return ptr - channels;
}

static void panScan(const cv::Mat& inputImage, cv::Mat& outputImage, int height, int width, int resize)
{
	// Get image size
	int img_h, img_w;
	img_h = static_cast<int>(inputImage.rows);
	img_w = static_cast<int>(inputImage.cols);

	// Compute new size
	int h, w, newHeight, newWidth;
	h         = (resize == 0) ? height : resize;
	w         = (resize == 0) ? width : resize;
	newHeight = std::max(img_h * w / img_w, img_h * h / img_h);
	newWidth  = std::max(img_w * w / img_w, img_w * h / img_h);

	cv::Mat resizedImage;
	/* Resize shortest, this line triggers error messages about Xilinx XRT, disregard them */
	cv::resize(inputImage, resizedImage, cv::Size(newWidth, newHeight));

	// Crop longest
	int      x = (resizedImage.cols - width) / 2;
	int      y = (resizedImage.rows - height) / 2;
	cv::Rect cropRegion(x, y, width, height);

	outputImage = resizedImage(cropRegion).clone();
}

static void listDdr(std::string& list_str, std::vector<std::vector<uint8_t>>& list)
{
	if (!list_str.empty())
	{
		while (list_str.find('+') != std::string::npos)
		{
			std::string          sub_list_str = list_str.substr(0, list_str.find('+'));
			std::vector<uint8_t> sub_list;
			while (sub_list_str.find(':') != std::string::npos)
			{
				sub_list.push_back(
				    static_cast<uint8_t>(std::stoi(sub_list_str.substr(0, sub_list_str.find(':')))));
				sub_list_str.erase(0, sub_list_str.find(':') + 1);
			}

			if (!sub_list_str.empty())
				sub_list.push_back(static_cast<uint8_t>(std::stoi(sub_list_str)));

			list.push_back(sub_list);
			list_str.erase(0, list_str.find('+') + 1);
		}

		if (!list_str.empty())
		{
			std::vector<uint8_t> sub_list;
			while (list_str.find(':') != std::string::npos)
			{
				sub_list.push_back(static_cast<uint8_t>(std::stoi(list_str.substr(0, list_str.find(':')))));
				list_str.erase(0, list_str.find(':') + 1);
			}

			if (!list_str.empty())
				sub_list.push_back(static_cast<uint8_t>(std::stoi(list_str)));

			list.push_back(sub_list);
		}
	}
}

static bool has_option(const struct option* opts, int val)
{
	for (; opts->name != nullptr; ++opts)
		if (opts->val == val)
			return true;
	return false;
}

void
usage(const std::string& cmd, const std::string& reason, const struct option* long_opts, bool multi_snapshot)
{
	std::string demo = cmd.substr(cmd.find_last_of('/') + 1);

	if (!reason.empty())
		std::cout << cmd << ": " << reason << std::endl;

	if (multi_snapshot)
		std::cout << "Usage: " << demo << " --snapshot PATH[+PATH]... [OPTION]..." << std::endl;
	else
		std::cout << "Usage: " << demo << " --snapshot PATH [OPTION]..." << std::endl;
	std::cout << std::endl;

	std::cout << "Mandatory arguments:" << std::endl;
	if (multi_snapshot)
		std::cout << "  --snapshot PATH[+PATH]... Paths to the snapshot directories" << std::endl;
	else
		std::cout << "  --snapshot PATH           Path to the snapshot directory" << std::endl;
	std::cout << std::endl;

	std::cout << "Options:" << std::endl;
	std::cout << "  --batchSize BATCHSIZE   Size of a batch of images to process, defaults to the snapshot\n"
	             "                          batch size"
	          << std::endl;
	std::cout << "  --channelOrder ORDER    Expected channel order (e.g. BGR), defaults to BGR" << std::endl;
	std::cout << "  --imgPath PATH          A directory or an image file (repeatable):\n"
	          << "                            directory: run on the first --nbImages images\n"
	          << "                            file:      run on the listed images (overrides --nbImages)\n"
	          << "                            omitted:   random data is used" << std::endl;
	std::cout << "  --goldFile PATH         Path to the gold results file; omit to skip comparison"
	          << std::endl;
	std::cout << "  --noGoldOutput          Suppress per-image gold detail output" << std::endl;
	std::cout << "  --labels PATH           Path to the labels file, defaults to './labels'" << std::endl;
	std::cout << "  --mean MEAN             Per-channel pixel mean (space-separated), defaults to 0"
	          << std::endl;
	std::cout << "  --nbImages N            Number of images to process, defaults to " << DEFAULT_NB_BATCH
	          << " x batch size" << std::endl;

	if (multi_snapshot)
		std::cout << "  --network NETWORK[+NETWORK]...  Model name(s) to display" << std::endl;
	else
		std::cout << "  --network NETWORK       Model name to display" << std::endl;

	std::cout << "  --resizeType TYPE [N]   Resize algorithm and optional size, defaults to 'PanScan 224 224'"
	          << std::endl;
	std::cout << "  --std STD               Per-channel pixel std-dev (space-separated), defaults to 255"
	          << std::endl;

	if (has_option(long_opts, OPT_NB_THREADS))
		std::cout << "  --nbThreads N           Number of threads to use" << std::endl;

	if (has_option(long_opts, OPT_DATA_FORMAT))
		std::cout << "  --dataFormat FORMAT     Force native-format data transfer. FORMAT is one of:\n"
		          << "                            'native' (input and output), 'inNative', 'outNative'"
		          << std::endl;

	if (has_option(long_opts, OPT_FORCE_IN_OUT_DDR))
	{
		std::cout << "  --forceInOutDdr IDs     Ordered colon-separated DDR IDs for input and output buffers"
		          << std::endl;
		std::cout << "  --forceInDdr IDs        Ordered colon-separated DDR IDs for input buffers"
		          << std::endl;
		std::cout << "  --forceOutDdr IDs       Ordered colon-separated DDR IDs for output buffers"
		          << std::endl;
	}

	if (has_option(long_opts, OPT_FPGA_ARCH))
		std::cout
		    << "  --fpgaArch ARCH         FPGA architecture for native-format layout, defaults to 'aieml'"
		    << std::endl;

	if (has_option(long_opts, OPT_REPEAT))
		std::cout << "  --repeat N              Repeat the image set N times (for profiling), defaults to 1"
		          << std::endl;

	if (has_option(long_opts, OPT_NO_FPS_PROGRESS))
		std::cout << "  --noFpsProgress         Suppress FPS progress display during inference" << std::endl;

	if (has_option(long_opts, OPT_USE_EXTERNAL_QUANT))
		std::cout << "  --useExternalQuant      Apply app-level (un)quantization around VART ML calls"
		          << std::endl;

	if (has_option(long_opts, OPT_USE_ONNX_SUBGRAPHS))
		std::cout << "  --useOnnxSubgraphs      Execute ONNX nodes of the model (default: NPU only)"
		          << std::endl;

	if (has_option(long_opts, OPT_USE_SNAPSHOT_GOLD))
		std::cout << "  --useSnapshotGold       Use the snapshot's built-in gold files for validation"
		          << std::endl;
}

int read_options(int                  argc,
                 char*                argv[],
                 struct options&      options,
                 const struct option* long_opts,
                 bool                 multi_snapshot)
{
	// Suppress getopt's own error messages; we print usage() ourselves.
	opterr = 0;

	// DDR strings are kept as locals because mutual-exclusion between
	// forceInOutDdr / forceInDdr / forceOutDdr can only be checked once
	// all three options have been scanned.
	std::string ddr_in_str, ddr_out_str;
	bool        force_in_out_ddr = false;
	bool        force_in_ddr     = false;
	bool        force_out_ddr    = false;

	std::string labels_file;

	int err, opt, idx;
	while ((opt = getopt_long(argc, argv, "", long_opts, &idx)) != -1)
	{
		if (opt == '?')
		{
			std::string unknown = (optind > 1 && optind <= argc) ? argv[optind - 1] : "?";
			usage(argv[0], "unknown option: " + unknown, long_opts, multi_snapshot);
			return vart_ml_log_err_msg(
			    vart_ml_error::CONFIG_BAD_ARGUMENT, "Option %s doesn't exist\n", unknown.c_str());
		}

		switch (opt)
		{
		case OPT_BATCH_SIZE:
			options.batchSize = strtoul(optarg, nullptr, 0);
			break;

		case OPT_CHANNEL_ORDER:
			options.channelOrder.clear();
			for (char c : std::string(optarg))
				options.channelOrder.push_back(channelIdx(c));
			break;

		case OPT_GOLD_FILE:
			err = read_gold(optarg, options.gold);
			if (err)
				return err;
			break;

		case OPT_NO_GOLD_OUTPUT:
			options.noGoldOutput = true;
			break;

		case OPT_IMG_PATH:
			if (isDirectory(optarg))
			{
				if (!options.imgPath.empty())
					return vart_ml_log_err_msg(vart_ml_error::CONFIG_BAD_ARGUMENT,
					                           "--imgPath directory specified more than once\n");
				options.imgPath = optarg;
			}
			else if (isFile(optarg, "r"))
				options.images_paths.push_back(optarg);
			else
				return vart_ml_log_err_msg(
				    vart_ml_error::CONFIG_BAD_ARGUMENT, "Unsupported file mode %s.\n", optarg);
			break;

		case OPT_LABELS:
			labels_file = optarg;
			break;

		case OPT_MEAN:
		{
			options.mean.clear();
			std::string s(optarg);
			s.erase(std::remove(s.begin(), s.end(), '\''), s.end());
			std::istringstream iss(s);
			float              v;
			while (iss >> v)
				options.mean.push_back(v);
			break;
		}

		case OPT_NB_IMAGES:
			options.nbImages = strtoul(optarg, nullptr, 0);
			break;

		case OPT_NETWORK:
			// Kept for compatibility; value not used.
			break;

		case OPT_RESIZE_TYPE:
		{
			options.resizeType.first.clear();
			options.resizeType.second.clear();
			std::istringstream iss(optarg);
			iss >> options.resizeType.first;
			int v;
			while (iss >> v)
				options.resizeType.second.push_back(v);
			break;
		}

		case OPT_SNAPSHOT:
		{
			std::string s(optarg);
			if (s.find('+') != std::string::npos)
			{
				if (!multi_snapshot)
					return vart_ml_log_err_msg(vart_ml_error::TOOLS_BAD_USAGE,
					                           "Multi-model execution is not supported by %s. Please use "
					                           "multi_models_demo, multi_threads_demo or vart_ml_demo.\n",
					                           argv[0]);

				while (s.find('+') != std::string::npos)
				{
					options.snapshots.push_back(s.substr(0, s.find('+')));
					s.erase(0, s.find('+') + 1);
				}
			}
			options.snapshots.push_back(s);
			break;
		}

		case OPT_STD:
		{
			options.stddev.clear();
			std::string s(optarg);
			s.erase(std::remove(s.begin(), s.end(), '\''), s.end());
			std::istringstream iss(s);
			float              v;
			while (iss >> v)
				options.stddev.push_back(v);
			for (auto& x : options.stddev)
				if (x == 0)
					x = 1;
			break;
		}

		case OPT_NB_THREADS:
			options.nbThreads     = strtoul(optarg, nullptr, 0);
			options.isMultiThread = true;
			break;

		case OPT_DATA_FORMAT:
		{
			std::string fmt(optarg);
			if (fmt != "native" && fmt != "inNative" && fmt != "outNative")
				return vart_ml_log_err_msg(
				    vart_ml_error::TOOLS_BAD_USAGE,
				    "unknown dataformat argument '%s'. Valid arguments are: 'native' (in "
				    "and out), 'inNative', 'outNative'.\n",
				    optarg);

			if (fmt == "native")
			{
				options.in_isNative  = true;
				options.out_isNative = true;
			}
			else if (fmt == "inNative")
				options.in_isNative = true;
			else
				options.out_isNative = true;
			break;
		}

		case OPT_FORCE_IN_DDR:
			force_in_ddr = true;
			ddr_in_str   = optarg;
			break;

		case OPT_FORCE_OUT_DDR:
			force_out_ddr = true;
			ddr_out_str   = optarg;
			break;

		case OPT_FORCE_IN_OUT_DDR:
			force_in_out_ddr = true;
			ddr_in_str = ddr_out_str = optarg;
			break;

		case OPT_FPGA_ARCH:
			options.fpga_arch = optarg;
			break;

		case OPT_REPEAT:
			options.repeatCnt = strtoul(optarg, nullptr, 0);
			break;

		case OPT_NO_FPS_PROGRESS:
			options.showFpsProgress = false;
			break;

		case OPT_USE_EXTERNAL_QUANT:
			options.useExternalQuant = true;
			break;

		case OPT_USE_ONNX_SUBGRAPHS:
			options.useOnnxSubgraphs = true;
			break;

		case OPT_USE_SNAPSHOT_GOLD:
			options.useSnapshotGold  = true;
			options.useOnnxSubgraphs = true;
			break;
		}
	}

	// Mandatory: --snapshot
	if (options.snapshots.empty())
	{
		usage(argv[0], "Missing argument snapshot.\n", long_opts, multi_snapshot);
		return vart_ml_log_err_msg(vart_ml_error::TOOLS_BAD_USAGE, "Missing argument snapshot.\n");
	}

	// --forceIn/OutDdr mutual-exclusion check (requires all three options to be scanned first).
	if (force_in_out_ddr && (force_in_ddr || force_out_ddr))
		return vart_ml_log_err_msg(
		    vart_ml_error::TOOLS_BAD_USAGE,
		    "forceInOutDdr option cannot be used simultaneously with forceInDdr or forceOutDdr.\n");

	if (force_in_out_ddr || force_in_ddr || force_out_ddr)
	{
		listDdr(ddr_in_str, options.ddr_in_list);
		listDdr(ddr_out_str, options.ddr_out_list);
	}

	options.ddr_in_list.resize(options.snapshots.size());
	options.ddr_out_list.resize(options.snapshots.size());

	err = read_categories(labels_file, options.categories);
	if (err)
		return err;

	return vart_ml_error::SUCCESS;
}

int read_images_options(struct options& options, size_t default_batchSize)
{
	// Parse size of batchs, defaults to snapshot batch size if none given.
	if (options.batchSize == 0)
		options.batchSize = default_batchSize;

	// Parse number of images, defaults to DEFAULT_NB_BATCH * batchSize size if none given.
	if (!options.images_paths.empty())
		options.nbImages = options.images_paths.size();
	else if (options.nbImages == 0)
		options.nbImages = options.batchSize * DEFAULT_NB_BATCH;

	// Not enough images to fill a full batch
	if (options.batchSize > options.nbImages)
		options.batchSize = options.nbImages;

	if (options.imgPath.empty() && options.images_paths.empty() && !options.useSnapshotGold)
		vart_ml_log(
		    LOG_WARN,
		    "No images provided. This run will use random buffers and will not have an accuracy score\n");
	else if (options.gold.empty() && !options.useSnapshotGold)
		vart_ml_log(LOG_WARN, "No Gold file provided. This run will not have an accuracy score.\n");
	else if (options.categories.empty() && !options.useSnapshotGold)
		vart_ml_log(LOG_WARN, "No labels file provided. This run will not have an accuracy score.\n");

	// Get paths of all required images if a directory was given.
	if (!options.imgPath.empty() && options.images_paths.empty() && !options.useSnapshotGold)
		for (size_t n = 0; n < options.nbImages; n += options.batchSize)
			for (size_t b = 0; b < options.batchSize; b++)
			{
				std::stringstream num;
				num << std::setfill('0') << std::setw(8) << std::to_string(n + b + 1);
				options.images_paths.push_back(options.imgPath + "/ILSVRC2012_val_" + num.str() + ".JPEG");
			}
	else if (options.useSnapshotGold)
	{
		if (!options.imgPath.empty() || !options.categories.empty() || !options.gold.empty())
			vart_ml_log(LOG_WARN,
			            "useSnapshotGold is enable, imgPath, labels and goldFile will be ignored\n");

		options.goldFiles.resize(options.snapshots.size());
		options.data_types.resize(options.snapshots.size());
		for (size_t i = 0; i < options.snapshots.size(); i++)
		{
			std::string snapshot_path = options.snapshots[i] + "/embedded_export/";

			std::vector<std::string> inputs_path;
			for (const auto& entry : std::filesystem::directory_iterator(snapshot_path + "golds/inputs"))
				inputs_path.push_back(entry.path());

			std::sort(inputs_path.begin(), inputs_path.end());

			for (auto& input_path : inputs_path)
			{
				auto [name, type]           = parse_gold(input_path, snapshot_path + "golds/inputs/");
				options.data_types[i][name] = type;
				options.goldFiles[i][name].push_back(input_path);
			}

			std::vector<std::string> outputs_path;
			for (const auto& entry : std::filesystem::directory_iterator(snapshot_path + "golds/outputs"))
				outputs_path.push_back(entry.path());

			std::sort(outputs_path.begin(), outputs_path.end());

			for (auto& output_path : outputs_path)
			{
				auto [name, type]           = parse_gold(output_path, snapshot_path + "golds/outputs/");
				options.data_types[i][name] = type;
				options.goldFiles[i][name].push_back(output_path);
			}

			if (options.goldFiles[i].begin()->second.size() * options.batchSize < options.nbImages)
				options.nbImages = options.goldFiles[i].begin()->second.size() * options.batchSize;
		}
	}

	return vart_ml_error::SUCCESS;
}

std::vector<std::pair<size_t, float>> topk(const std::vector<float>& buf, size_t k)
{
	std::vector<std::pair<size_t, float>> tmp;

	size_t i = 0;
	for (auto x : buf)
		tmp.push_back(std::make_pair(i++, x));

	std::sort(tmp.begin(), tmp.end(), [](auto& l, auto r) { return l.second > r.second; });

	tmp.resize(k);
	return tmp;
}

int read_categories(const std::string& labels_file, std::vector<std::string>& categories)
{
	std::ifstream catfile(labels_file.empty() ? "labels" : labels_file);
	std::string   str;

	if (!catfile)
		return vart_ml_error::SUCCESS;

	while (std::getline(catfile, str))
		categories.push_back(str);

	catfile.close();
	return vart_ml_error::SUCCESS;
}

int read_gold(const std::string& gold_file, std::map<std::string, std::string>& gold)
{
	// If there is no gold file, return an empty map.
	if (gold_file.empty())
		return vart_ml_error::SUCCESS;

	// Open the gold file.
	std::ifstream catfile(gold_file);
	if (!catfile)
		return vart_ml_log_err_msg(vart_ml_error::FILE_ACCESS_OPEN_FAILURE,
		                           "Failed to open the gold file.\n");

	// Read gold file and insert elements in gold map.
	std::string str;
	while (std::getline(catfile, str))
		gold[str.substr(0, str.find(" "))] = str.substr(str.find(" ") + 1);

	catfile.close();
	return vart_ml_error::SUCCESS;
}

int configure_ddr_lists(std::shared_ptr<vart::Runner> runner,
                        std::vector<uint8_t>&         ddr_in_list,
                        std::vector<uint8_t>&         ddr_out_list)
{
	uint8_t nb_ddrs = std::any_cast<uint8_t>(runner->get_property("nb_ddrs"));

	if (ddr_in_list.empty())
		for (size_t i = 0; i < nb_ddrs; i++)
			ddr_in_list.push_back(i);
	else if (*std::max_element(ddr_in_list.begin(), ddr_in_list.end()) >= nb_ddrs)
		return vart_ml_log_err_msg(vart_ml_error::TOOLS_BAD_USAGE,
		                           "DDR ID (%d) exceeds actual DDR ID range (%d) for input buffers\n",
		                           *std::max_element(ddr_in_list.begin(), ddr_in_list.end()),
		                           nb_ddrs - 1);

	if (ddr_out_list.empty())
		for (size_t i = 0; i < nb_ddrs; i++)
			ddr_out_list.push_back(i);
	else if (*std::max_element(ddr_out_list.begin(), ddr_out_list.end()) >= nb_ddrs)
		return vart_ml_log_err_msg(vart_ml_error::TOOLS_BAD_USAGE,
		                           "DDR ID (%d) exceeds actual DDR ID range (%d) for output buffers\n",
		                           *std::max_element(ddr_out_list.begin(), ddr_out_list.end()),
		                           nb_ddrs - 1);

	return vart_ml_error::SUCCESS;
}

void softmax(std::vector<float>& buf)
{
	double sum = 0.0f;

	for (auto& x : buf)
	{
		x = exp(x);
		sum += x;
	}

	for (auto& x : buf)
		x /= sum;
}

int allocate_ultrascale_batch_tensors(const struct options&                      options,
                                      std::shared_ptr<vart::Runner>              runner,
                                      const vart::NpuTensorInfo&                 tensor_info,
                                      size_t                                     batchSize_curr,
                                      std::vector<std::vector<vart::NpuTensor>>& batch_tensors,
                                      const char*                                buffer_type)
{
	auto parent_info = tensor_info;
	/* Compute memory size per system */
	size_t batch_size_per_core   = std::any_cast<size_t>(runner->get_property("batch_size_per_core"));
	size_t nb_cores_per_system   = std::any_cast<size_t>(runner->get_property("nb_cores_per_system"));
	size_t nb_system             = std::any_cast<size_t>(runner->get_property("nb_systems"));
	size_t batch_size_per_system = batch_size_per_core * nb_cores_per_system;

	parent_info.size_in_bytes *= batch_size_per_system;

	size_t filled = 0;
	while (filled < batchSize_curr)
	{
		for (size_t sys = 0; sys < nb_system && filled < batchSize_curr; sys++)
		{
			auto parent = runner->allocate_npu_tensor(parent_info, sys);

			for (size_t s = 0; s < batch_size_per_system && filled < batchSize_curr; s++)
			{
				auto sub = runner->allocate_sub_tensor(parent, tensor_info, s * tensor_info.size_in_bytes);
				if (sub.get_virtual_address() == NULL)
					return vart_ml_log_err_msg(
					    vart_ml_error::DEVICE_DDR_MALLOC_FAILURE,
					    "Failed to allocate %s buffers for all %zu images.\n"
					    "Pre-process-on-the-fly mode not supported by Ultrascale. Abort.\n",
					    buffer_type,
					    options.nbImages);
				batch_tensors[filled].push_back(std::move(sub));
				filled++;
			}
		}
	}

	return vart_ml_error::SUCCESS;
}

template <typename T>
static int preprocess(const struct options& options,
                      T* const              buf,
                      const std::string&    image_path,
                      size_t                height,
                      size_t                width,
                      size_t                channel)
{
	cv::Mat preprocessed = cv::Mat(height, width, CV_8UC3);
	cv::Mat image        = cv::imread(image_path);

	if (options.resizeType.first == "PanScan")
	{
		if (options.resizeType.second.size() >= 3)
			panScan(image, preprocessed, height, width, options.resizeType.second[2]);
		else
			panScan(image, preprocessed, height, width, 0);
	}
	else
		return vart_ml_log_err_msg(vart_ml_error::TOOLS_BAD_ARG,
		                           "Resize type %s is not supported.\n",
		                           options.resizeType.first.c_str());

	for (size_t h = 0; h < height; h++)
		for (size_t w = 0; w < width; w++)
			for (size_t c = 0; c < channel; c++)
				if (c < options.channelOrder.size())
				{
					uint8_t pixel = preprocessed.at<cv::Vec3b>(h, w)[options.channelOrder[c]];
					if constexpr (std::is_same_v<T, float>)
					{
						if (c < options.mean.size() && c < options.stddev.size())
							buf[width * channel * h + channel * w + c] =
							    (pixel - options.mean[c]) / options.stddev[c];
					}
					else
						buf[width * channel * h + channel * w + c] = pixel;
				}

	return vart_ml_error::SUCCESS;
}

static int clip(int x, int min, int max)
{
	if (x >= max)
		return max;
	if (x <= min)
		return min;
	return x;
}

// Returns the [H, W, C] shape to use for preprocessing a native-format tensor,
// accounting for the tiled memory layouts used by the NPU.
static std::array<uint32_t, 3> native_hwc_shape(const vart::NpuTensorInfo& info)
{
	const auto& s = info.shape;
	switch (info.memory_layout)
	{
	case vart::MemoryLayout::NHW16C4WC:
		return { s[1], s[2] * s[4], s[3] * s[5] };
	case vart::MemoryLayout::NH2HWC4C:
		return { s[1] * s[2], s[3], s[4] * s[5] };
	case vart::MemoryLayout::NH2C4HWC:
		return { s[1] * s[3], s[4], s[2] * s[5] };
	default:
		return { s[1], s[2], s[3] };
	}
}

int preprocess_batch(const struct options&                      options,
                     std::vector<std::vector<vart::NpuTensor>>& in_tensors,
                     size_t                                     img_idx,
                     const std::vector<float>&                  coeff)
{
	int err;
	std::srand(std::time({})); // use current time as seed for random generator

	static std::vector<uint8_t> dummy_image;

	for (size_t b = 0; b < in_tensors.size(); b++)
		for (size_t i = 0; i < in_tensors[b].size(); i++)
		{
			if (options.images_paths.empty())
			{
				if (dummy_image.size() < in_tensors[b][i].get_info().size_in_bytes)
				{
					size_t curr_size = dummy_image.size();
					for (size_t d = curr_size; d < in_tensors[b][i].get_info().size_in_bytes; d++)
						dummy_image.push_back(rand() % 256);
				}

				memcpy(in_tensors[b][i].get_virtual_address(),
				       dummy_image.data(),
				       in_tensors[b][i].get_info().size_in_bytes);
			}
			else if (options.in_isNative)
			{
				std::vector<float> input(in_tensors[b][i].get_info().size);

				auto shape = native_hwc_shape(in_tensors[b][i].get_info());

				if (in_tensors[b][i].get_info().data_type == vart::DataType::FLOAT32)
				{
					err = preprocess(options,
					                 input.data(),
					                 options.images_paths[img_idx + b],
					                 shape[0],
					                 shape[1],
					                 shape[2]);
					if (err)
						return err;
				}
				else if (in_tensors[b][i].get_info().data_type == vart::DataType::INT8)
				{
					err = preprocess(options,
					                 input.data(),
					                 options.images_paths[img_idx + b],
					                 shape[0],
					                 shape[1],
					                 shape[2]);
					if (err)
						return err;

					for (size_t j = 0; j < input.size(); j++)
						((int8_t*)in_tensors[b][i].get_virtual_address())[j] =
						    clip(nearbyintf(input[j] * coeff[i]), -128, 127);
				}
				else
				{
					err = preprocess(options,
					                 (uint8_t*)in_tensors[b][i].get_virtual_address(),
					                 options.images_paths[img_idx + b],
					                 shape[0],
					                 shape[1],
					                 shape[2]);
					if (err)
						return err;
				}
			}
			else if (options.useExternalQuant)
			{
				if (coeff[i] <= 0)
					return vart_ml_log_err_msg(vart_ml_error::CONFIG_INVALID_QUANTIZATION_TYPE,
					                           "No external quantization available for input data type.\n");

				std::vector<float> input(in_tensors[b][i].get_info().size);

				err = preprocess(options,
				                 input.data(),
				                 options.images_paths[img_idx + b],
				                 in_tensors[b][i].get_info().shape[1],
				                 in_tensors[b][i].get_info().shape[2],
				                 in_tensors[b][i].get_info().shape[3]);
				if (err)
					return err;

				for (size_t j = 0; j < in_tensors[b][i].get_info().size; j++)
					((int8_t*)in_tensors[b][i].get_virtual_address())[j] =
					    clip(nearbyintf(input[j] * coeff[i]), -128, 127);
			}
			else
			{
				if (in_tensors[b][i].get_info().data_type == vart::DataType::FLOAT32)
					err = preprocess(options,
					                 (float*)in_tensors[b][i].get_virtual_address(),
					                 options.images_paths[img_idx + b],
					                 in_tensors[b][i].get_info().shape[1],
					                 in_tensors[b][i].get_info().shape[2],
					                 in_tensors[b][i].get_info().shape[3]);
				else
					err = preprocess(options,
					                 (uint8_t*)in_tensors[b][i].get_virtual_address(),
					                 options.images_paths[img_idx + b],
					                 in_tensors[b][i].get_info().shape[1],
					                 in_tensors[b][i].get_info().shape[2],
					                 in_tensors[b][i].get_info().shape[3]);
				if (err)
					return err;
			}
		}

	return vart_ml_error::SUCCESS;
}

void compare_gold(const struct options&                  options,
                  size_t                                 first_image_index,
                  const std::vector<std::vector<float>>& results,
                  size_t&                                nbComparedImages,
                  float                                  accuracy[2],
                  const std::string&                     network)
{
	// Without the categories or the gold, the accuracy can not be calculated.
	if (options.images_paths.empty() || options.categories.empty() || options.gold.empty())
	{
		nbComparedImages = 0;
		return;
	}

	for (size_t b = 0; b < results.size(); b++)
	{
		size_t result_index = first_image_index + b;

		// Get the name of the current image.
		std::string currentImg = options.images_paths[result_index].substr(
		    options.images_paths[result_index].find_last_of('/') + 1);

		std::vector<std::pair<size_t, float>> top5 = topk(results[b], 5);

		std::string color[5]   = { "" };
		auto        goldResult = options.gold.find(currentImg);
		// If gold file has no goldResult for this image, decrement compared images counter.
		if (goldResult == options.gold.end())
			--nbComparedImages;
		else if (goldResult->second == options.categories[top5[0].first])
		{
			accuracy[0]++;
			accuracy[1]++;
			color[0] = green;
		}
		else
		{
			for (size_t i = 1; i < 5; i++)
			{
				if (goldResult->second == options.categories[top5[i].first])
				{
					accuracy[1]++;
					if (top5[0].second == top5[i].second)
					{
						accuracy[0]++;
						color[i] = green;
					}
					else
						color[i] = yellow;
					break;
				}
			}
		}

		if (options.noGoldOutput)
		{
			std::cout << "Computing accuracy " << std::setw(8) << std::setfill(' ') << std::fixed
			          << first_image_index << " imgs.\r" << std::flush;
			continue;
		}

		std::cout << network << " Image " << result_index << " (" << result_index << ":0) " << currentImg
		          << std::endl;

		std::cout << network << "    GOLD - ";
		if (goldResult == options.gold.end())
			std::cout << "no gold results for this image, omitted for accuracy summary";
		else
			std::cout << goldResult->second << " - 1.00000000";
		std::cout << std::endl;

		const auto default_precision{ std::cout.precision() };
		std::cout << std::setprecision(8);
		for (size_t i = 0; i < 5; i++)
			std::cout << network << "    PRED - " << options.categories[top5[i].first] << " - " << color[i]
			          << top5[i].second << reset << std::endl;
		std::cout << network << std::setprecision(default_precision) << std::endl;
	}
}

void print_accuracy_summary_headless(const std::string& network, float accuracy[2], size_t nbComparedImages)
{
	std::cout << std::fixed << std::setprecision(2);
	std::cout << "[AMD] [" << network << "TEST top1] " << 100 * accuracy[0] / nbComparedImages << "% passed."
	          << std::endl;
	std::cout << "[AMD] [" << network << "TEST top5] " << 100 * accuracy[1] / nbComparedImages << "% passed."
	          << std::endl;
	std::cout << "[AMD] [" << network << "ALL TESTS] " << 100 * accuracy[0] / nbComparedImages << "% passed."
	          << std::endl;
}

void print_accuracy_summary(const std::string& network, float accuracy[2], size_t nbComparedImages)
{
	// Set network name used for accuracy section. Use tabulation as default if none given.
	std::string network_accur = (network.empty()) ? "" : (network + ' ');

	std::cout << std::endl;
	std::cout << "============================================================" << std::endl;
	std::cout << "Accuracy Summary:" << std::endl;
	print_accuracy_summary_headless(network_accur, accuracy, nbComparedImages);
}

size_t get_min_or_max_batch_size(bool is_max, std::vector<std::shared_ptr<vart::Runner>>& runners)
{
	std::vector<size_t> batch_size(runners.size());
	std::transform(
	    runners.begin(), runners.end(), batch_size.begin(), [](auto& x) { return x->get_batch_size(); });
	if (is_max)
		return *std::max_element(batch_size.begin(), batch_size.end());
	else
		return *std::min_element(batch_size.begin(), batch_size.end());
}

std::vector<std::vector<vart::NpuTensor>> allocate_tensor_buffers(std::shared_ptr<vart::Runner> runner,
                                                                  vart::TensorDirection         direction,
                                                                  size_t                        batch_size)
{
	std::vector<std::vector<vart::NpuTensor>> tensors(batch_size);
	size_t num_tensors = (direction == vart::TensorDirection::INPUT) ? runner->get_num_input_tensors()
	                                                                 : runner->get_num_output_tensors();

	for (size_t b = 0; b < batch_size; b++)
		for (size_t i = 0; i < num_tensors; i++)
		{
			auto  info = runner->get_tensors_info(direction, vart::TensorType::CPU)[i];
			auto* buf  = std::malloc(info.size_in_bytes);
			tensors[b].push_back(vart::NpuTensor(info, buf, vart::MemoryType::USER_POINTER_CMA));
		}

	return tensors;
}

void fill_dummy_buffers(std::vector<std::vector<vart::NpuTensor>>& tensors)
{
	for (size_t b = 0; b < tensors.size(); b++)
		for (size_t i = 0; i < tensors[b].size(); i++)
			for (size_t d = 0; d < tensors[b][i].get_info().size_in_bytes; d++)
				((uint8_t*)tensors[b][i].get_virtual_address())[d] = rand() % 256;
}

std::vector<std::vector<float>>
extract_float_results(std::shared_ptr<vart::Runner>                    runner,
                      const std::vector<std::vector<vart::NpuTensor>>& output_tensors,
                      size_t                                           batch_size)
{
	std::vector<std::vector<float>> results;

	for (size_t i = 0; i < runner->get_num_output_tensors(); i++)
	{
		for (size_t b = 0; b < batch_size; b++)
		{
			std::vector<float> unquant_buf;

			for (size_t j = 0; j < output_tensors[b][i].get_info().size; j++)
				unquant_buf.push_back(((float*)output_tensors[b][i].get_virtual_address())[j]);

			results.push_back(unquant_buf);
		}
	}

	return results;
}

int unquantize_tensors_buffers(std::shared_ptr<vart::Runner>                    runner,
                               const std::vector<std::vector<vart::NpuTensor>>& output_tensors,
                               const std::vector<vart::NpuTensorInfo>&          tensor_infos,
                               bool                                             apply_softmax,
                               std::vector<std::vector<float>>&                 results)
{
	for (size_t i = 0; i < tensor_infos.size(); i++)
	{
		for (size_t b = 0; b < output_tensors.size(); b++)
		{
			std::vector<float> unquant_buf;

			size_t         tensor_size      = output_tensors[b][i].get_info().size;
			vart::DataType tensor_data_type = output_tensors[b][i].get_info().data_type;
			const void*    tensor_buffer    = output_tensors[b][i].get_virtual_address();

			float tensor_coeff = runner->get_quant_parameters(tensor_infos[i].name).scale;

			if (tensor_data_type == vart::DataType::FLOAT32)
				for (size_t j = 0; j < tensor_size; j++)
					unquant_buf.push_back(((float*)tensor_buffer)[j]);
			else if (tensor_data_type == vart::DataType::INT8)
				for (size_t j = 0; j < tensor_size; j++)
					unquant_buf.push_back(((int8_t*)tensor_buffer)[j] / tensor_coeff);
			else if (tensor_data_type == vart::DataType::UINT8)
				for (size_t j = 0; j < tensor_size; j++)
					unquant_buf.push_back(((uint8_t*)tensor_buffer)[j] / tensor_coeff);
			else
				return vart_ml_log_err_msg(
				    vart_ml_error::CONFIG_INVALID_QUANTIZATION_TYPE,
				    "Output %ld data_type is unsupported. Please use a valid data type.\n",
				    i);

			if (apply_softmax)
				softmax(unquant_buf);
			results.push_back(unquant_buf);
		}
	}

	return vart_ml_error::SUCCESS;
}

void display_fps_progress(size_t current_images, size_t total_images, double elapsed_seconds)
{
	std::cout << "\t" << std::setw(8) << std::setprecision(2) << std::setfill(' ') << std::fixed
	          << current_images / elapsed_seconds << " imgs/s. ("
	          << std::setw(std::to_string(total_images).size()) << current_images << " images)\r"
	          << std::flush;
}

struct MemInfo
{
	size_t mem_available_bytes;
	size_t commit_headroom_bytes;
};

/* Parse all four required fields from /proc/meminfo in a single pass. */
static MemInfo read_meminfo()
{
	std::ifstream meminfo("/proc/meminfo");
	std::string   line;
	size_t        mem_available_kb = 0;
	size_t        cma_free_kb      = 0;
	size_t        commit_limit_kb  = 0;
	size_t        committed_as_kb  = 0;
	bool          has_mem          = false;
	bool          has_limit        = false;
	bool          has_committed    = false;
	int           found            = 0;

	while (found < 4 && std::getline(meminfo, line))
	{
		if (line.rfind("MemAvailable:", 0) == 0)
		{
			std::istringstream(line.substr(13)) >> mem_available_kb;
			has_mem = true;
			found++;
		}
		else if (line.rfind("CmaFree:", 0) == 0)
		{
			std::istringstream(line.substr(8)) >> cma_free_kb;
			found++;
		}
		else if (line.rfind("CommitLimit:", 0) == 0)
		{
			std::istringstream(line.substr(12)) >> commit_limit_kb;
			has_limit = true;
			found++;
		}
		else if (line.rfind("Committed_AS:", 0) == 0)
		{
			std::istringstream(line.substr(13)) >> committed_as_kb;
			has_committed = true;
			found++;
		}
	}

	MemInfo info;

	/* CmaFree pages are included in MemAvailable but cannot be used by std::malloc. */
	if (!has_mem)
	{
		info.mem_available_bytes   = SIZE_MAX;
		info.commit_headroom_bytes = SIZE_MAX;
		return info;
	}

	size_t available_kb        = (mem_available_kb > cma_free_kb) ? mem_available_kb - cma_free_kb : 0;
	info.mem_available_bytes   = available_kb * 1024;
	info.commit_headroom_bytes = (has_limit && has_committed && commit_limit_kb > committed_as_kb)
	                                 ? (commit_limit_kb - committed_as_kb) * 1024
	                                 : SIZE_MAX;
	return info;
}

size_t get_mem_available_bytes() { return read_meminfo().mem_available_bytes; }

size_t get_commit_headroom_bytes() { return read_meminfo().commit_headroom_bytes; }

std::map<uint8_t, size_t> compute_npu_ddr_needed_per_bank(const std::vector<vart::NpuTensorInfo>& tensors,
                                                          const std::vector<uint8_t>&             ddr_list,
                                                          size_t                                  nbBatches,
                                                          size_t                                  batchSize,
                                                          size_t                                  nbImages)
{
	std::map<uint8_t, size_t> bytes_per_bank;

	for (size_t n = 0; n < nbBatches; n++)
	{
		size_t batchSize_curr = batchSize;
		if ((n + 1) * batchSize > nbImages)
			batchSize_curr = nbImages - n * batchSize;

		for (size_t b = 0; b < batchSize_curr; b++)
			for (size_t i = 0; i < tensors.size(); i++)
				bytes_per_bank[ddr_list[(b + i) % ddr_list.size()]] += tensors[i].size_in_bytes;
	}

	return bytes_per_bank;
}

int check_mem_budget(size_t needed_all,
                     size_t needed_fly,
                     size_t onnx_scratch,
                     size_t nbBatches,
                     int&   inbuf_alloc_fail)
{
	MemInfo mi       = read_meminfo();
	size_t  mem_free = (mi.mem_available_bytes > onnx_scratch) ? mi.mem_available_bytes - onnx_scratch : 0;

	/* CommitLimit - Committed_AS caps what the kernel will actually commit.
	 * MemAvailable is optimistic (counts reclaimable page cache) so we use the
	 * stricter commit headroom to decide whether pre-allocating ALL batches is safe.
	 * For the single-batch fallback check we trust mem_free. */
	size_t mem_free_all =
	    std::min(mem_free,
	             mi.commit_headroom_bytes > onnx_scratch ? mi.commit_headroom_bytes - onnx_scratch
	                                                     : static_cast<size_t>(0));

	if (needed_all <= mem_free_all)
		return vart_ml_error::SUCCESS;

	if (needed_fly <= mem_free)
	{
		vart_ml_log(LOG_WARN,
		            "Not enough memory for all %zu batches (%zu MB available). "
		            "Switching to pre-process-on-the-fly mode.\n",
		            nbBatches,
		            mem_free >> 20);
		inbuf_alloc_fail = true;
		return vart_ml_error::SUCCESS;
	}

	return vart_ml_log_err_msg(vart_ml_error::DEVICE_DDR_MALLOC_FAILURE,
	                           "Not enough memory for even a single batch (%zu MB available).\n",
	                           mem_free >> 20);
}

int check_npu_ddr_budget(const std::map<uint8_t, size_t>& needed_all,
                         const std::map<uint8_t, size_t>& needed_fly,
                         std::shared_ptr<vart::Runner>    runner,
                         size_t                           nbBatches,
                         int&                             inbuf_alloc_fail)
{
	MemBudgetResult result     = MemBudgetResult::OK;
	size_t          worst_free = 0;

	for (const auto& [bank, needed] : needed_all)
	{
		size_t bank_free = std::any_cast<std::vector<size_t>>(runner->get_property("ddr_free_bytes"))[bank];
		MemBudgetResult bank_result;

		if (needed <= bank_free)
			bank_result = MemBudgetResult::OK;
		else if (auto it = needed_fly.find(bank); it != needed_fly.end() && it->second <= bank_free)
			bank_result = MemBudgetResult::FALLBACK_ON_THE_FLY;
		else
			bank_result = MemBudgetResult::FATAL;

		if (static_cast<int>(bank_result) > static_cast<int>(result))
		{
			result     = bank_result;
			worst_free = bank_free;
		}
	}

	if (result == MemBudgetResult::OK)
		return vart_ml_error::SUCCESS;

	if (result == MemBudgetResult::FALLBACK_ON_THE_FLY)
	{
		vart_ml_log(LOG_WARN,
		            "Not enough NPU DDR for all %zu batches (%zu MB free on worst bank). "
		            "Switching to pre-process-on-the-fly mode.\n",
		            nbBatches,
		            worst_free >> 20);
		inbuf_alloc_fail = true;
		return vart_ml_error::SUCCESS;
	}

	return vart_ml_log_err_msg(vart_ml_error::DEVICE_DDR_MALLOC_FAILURE,
	                           "Not enough NPU DDR memory for even a single batch "
	                           "(%zu MB free on worst bank).\n",
	                           worst_free >> 20);
}
