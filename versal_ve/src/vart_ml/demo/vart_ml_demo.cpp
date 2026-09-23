/**
 * @file vart_ml_runner.cpp
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
#include <any>
#include <chrono>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
#include <optional>
#include <sstream>
#include <thread>
#include <unistd.h>
#include <vector>

#include <opencv2/opencv.hpp>

#include "common.h"
#include "utils/log.h"
#include "utils/parsing_combinators.h"
#include "utils/vcd_stats.h"
#include "vart_ml_runner/vart_runner_factory.hpp"

#define NOT_NATIVE 0
#define NATIVE     1
#define TIMEOUT    1000 // in milliseconds

static void setup_options(struct options& options, std::unordered_map<std::string, std::any>& runner_options)
{
	/* Check if we need to enable VART ML Runner's npu_only. */
	runner_options["npu_only"] = !options.useOnnxSubgraphs;

	/* Check if images are provided. Otherwise, random data will be used */
	if (!options.imgPath.empty() || !options.images_paths.empty())
		runner_options["in_shape_format"] = std::string("NHWC");

	/* Configure multi-threading */
	runner_options["nb_threads"] = options.nbThreads;
}

static int setup_quantization_parameters(const struct options&             options,
                                         std::shared_ptr<vart::Runner>     runner,
                                         std::vector<vart::NpuTensorInfo>& inputTensors,
                                         std::vector<vart::NpuTensorInfo>& outputTensors,
                                         size_t                            inputCnt,
                                         size_t                            outputCnt,
                                         std::vector<float>&               quant_coeff_in)
{
	/* Get the quantization parameters */
	for (size_t i = 0; i < inputCnt; i++)
		quant_coeff_in.push_back(runner->get_quant_parameters(inputTensors[i].name).scale);

	/* Setup external quantization for input and output tensors if needed */
	if (options.useExternalQuant)
	{
		for (size_t i = 0; i < inputCnt; i++)
		{
			inputTensors[i].data_type =
			    runner->get_tensors_info(vart::TensorDirection::INPUT, vart::TensorType::HW)[i].data_type;

			if (inputTensors[i].data_type == vart::DataType::INT8)
				inputTensors[i].size_in_bytes = inputTensors[i].size;
			else
			{
				vart_ml_log(LOG_ERR, "Unsupported external quantization data type for input\n");
				return vart_ml_error::TEST_FAILURE;
			}
		}

		for (size_t i = 0; i < outputCnt; i++)
		{
			outputTensors[i].data_type =
			    runner->get_tensors_info(vart::TensorDirection::OUTPUT, vart::TensorType::HW)[i].data_type;

			if (outputTensors[i].data_type == vart::DataType::INT8)
				outputTensors[i].size_in_bytes = outputTensors[i].size;
			else
			{
				vart_ml_log(LOG_ERR, "Unsupported external quantization data type for output\n");
				return vart_ml_error::TEST_FAILURE;
			}
		}
	}

	return vart_ml_error::SUCCESS;
}

static int allocate_input_tensors(const struct options&                                   options,
                                  std::shared_ptr<vart::Runner>                           runner,
                                  const std::vector<vart::NpuTensorInfo>&                 inputTensors,
                                  size_t                                                  inputCnt,
                                  size_t                                                  nbBatches,
                                  const std::vector<uint8_t>&                             ddr_in_list,
                                  std::vector<std::vector<std::vector<vart::NpuTensor>>>& runInputTensors)
{
	int err;

	runInputTensors.resize(nbBatches);
	for (size_t n = 0; n < nbBatches; n++)
	{
		size_t batchSize_curr = options.batchSize;
		if ((n + 1) * options.batchSize > options.nbImages)
			batchSize_curr = options.nbImages - n * options.batchSize;

		runInputTensors[n].resize(batchSize_curr);

		for (size_t b = 0; b < batchSize_curr; b++)
		{
			for (size_t i = 0; i < inputCnt; i++)
			{
				if (options.in_isNative)
				{
					/* In ultrascale, sample buffers of a same batch must be contiguous in memory. */
					if (options.fpga_arch == "ultrascale")
					{
						/* Fill all batches at once and do nothing later */
						if (b == 0)
						{
							err = allocate_ultrascale_batch_tensors(options,
							                                        runner,
							                                        inputTensors[i],
							                                        batchSize_curr,
							                                        runInputTensors[n],
							                                        "input");
							if (err)
								return err;
						}
					}
					else
					{
						/* The performance are better when the load is shared between the DDRs */
						runInputTensors[n][b].push_back(std::move(runner->allocate_npu_tensor(
						    inputTensors[i], ddr_in_list[(b + i) % ddr_in_list.size()])));
					}
				}
				else
				{
					auto* buf = std::malloc(inputTensors[i].size_in_bytes);
					runInputTensors[n][b].push_back(
					    vart::NpuTensor(inputTensors[i], buf, vart::MemoryType::USER_POINTER_CMA));
				}
			}
		}
	}

	return vart_ml_error::SUCCESS;
}

static int allocate_output_tensors(const struct options&                                   options,
                                   std::shared_ptr<vart::Runner>                           runner,
                                   const std::vector<vart::NpuTensorInfo>&                 outputTensors,
                                   size_t                                                  outputCnt,
                                   size_t                                                  nbBatches,
                                   const std::vector<uint8_t>&                             ddr_out_list,
                                   bool                                                    useGoldFiles,
                                   std::vector<std::vector<std::vector<vart::NpuTensor>>>& runOutputTensors)
{
	int err;

	/* Initialize the output buffers */
	runOutputTensors.resize(nbBatches);
	for (size_t n = 0; n < nbBatches; n++)
	{
		size_t batchSize_curr = options.batchSize;
		if ((n + 1) * options.batchSize > options.nbImages)
			batchSize_curr = options.nbImages - n * options.batchSize;

		runOutputTensors[n].resize(batchSize_curr);

		for (size_t b = 0; b < batchSize_curr; b++)
			for (size_t i = 0; i < outputCnt; i++)
			{
				if (options.out_isNative)
				{
					if (options.fpga_arch == "ultrascale")
					{
						if (b == 0)
						{
							err = allocate_ultrascale_batch_tensors(options,
							                                        runner,
							                                        outputTensors[i],
							                                        batchSize_curr,
							                                        runOutputTensors[n],
							                                        "output");
							if (err)
								return err;
						}
					}
					else
					{
						/* The performance are better when the load is shared between the DDRs */
						runOutputTensors[n][b].push_back(std::move(runner->allocate_npu_tensor(
						    outputTensors[i], ddr_out_list[(b + i) % ddr_out_list.size()])));
					}
				}
				else
				{
					auto* buf = std::malloc(outputTensors[i].size_in_bytes);
					runOutputTensors[n][b].push_back(
					    vart::NpuTensor(outputTensors[i], buf, vart::MemoryType::USER_POINTER_CMA));
				}
			}

		// No need for more buffer, only one batch will be use
		if (!useGoldFiles)
			break;
	}

	return vart_ml_error::SUCCESS;
}

static void
handle_input_allocation_failure(std::shared_ptr<vart::Runner>                           runner,
                                const std::vector<vart::NpuTensorInfo>&                 inputTensors,
                                size_t                                                  inputCnt,
                                const std::vector<uint8_t>&                             ddr_in_list,
                                bool                                                    in_isNative,
                                std::vector<std::vector<std::vector<vart::NpuTensor>>>& runInputTensors)
{
	/* Clean-up previously allocated buffers before break */
	runInputTensors.clear();

	runInputTensors.resize(1);
	runInputTensors[0].resize(runner->get_batch_size());

	for (size_t b = 0; b < runner->get_batch_size(); b++)
		for (size_t i = 0; i < inputCnt; i++)
			if (in_isNative)
				/* The performance are better when the load is shared between the DDRs */
				runInputTensors[0][b].push_back(std::move(
				    runner->allocate_npu_tensor(inputTensors[i], ddr_in_list[(b + i) % ddr_in_list.size()])));
			else
			{
				auto* buf = std::malloc(inputTensors[i].size_in_bytes);
				runInputTensors[0][b].push_back(
				    vart::NpuTensor(inputTensors[i], buf, vart::MemoryType::USER_POINTER_CMA));
			}
}

static vart::StatusCode wait_for_async_jobs(const std::vector<std::shared_ptr<vart::Runner>>& runners,
                                            vart::JobHandle*                                  job_handle,
                                            size_t                                            nbThreads,
                                            size_t                                            thread_idx,
                                            int8_t                                            execute_loop_id)
{
	vart::StatusCode status = vart::StatusCode::SUCCESS;

	for (size_t m = 0; m < runners.size(); m++)
		for (size_t t = 0; t < thread_idx; t++)
		{
			status = runners[m]->wait(job_handle[m * nbThreads + t], std::chrono::milliseconds(TIMEOUT));
			if (status != vart::StatusCode::SUCCESS)
				std::cout << red << "[TEST_ERROR] "
				          << std::any_cast<std::string>(runners[m]->get_property("model_name"))
				          << " failed during execute." << reset << std::endl;

			struct vcd_context vcd_context = { VCD_GLOBAL_ID, t };
			vcd_event(vcd_context, execute_loop_id, 0);
		}

	return status;
}

static void display_execution_fps(const struct options& options,
                                  size_t                runners_size,
                                  size_t                n,
                                  size_t                r,
                                  double                elapsed_seconds)
{
	size_t nbImages_curr = ((n + 1) * options.batchSize <= options.nbImages)
	                           ? ((n + 1) * options.batchSize + r * options.nbImages)
	                           : (r + 1) * options.nbImages;

	std::cout << "[VART]  Running " << runners_size << " models\t" << std::setw(8) << std::setprecision(2)
	          << std::setfill(' ') << std::fixed
	          << (n + options.batchSize + r * options.nbImages) / elapsed_seconds << " imgs/s. ("
	          << std::setw(std::to_string(nbImages_curr).size()) << nbImages_curr << " images)\r"
	          << std::flush;
}

// TODO: Get the correct gold file if native is true
static int read_gold_file(const std::string& filename,
                          const std::string& data_type,
                          size_t             tensor_size,
                          void**             gold_ptr,
                          size_t*            element_size)
{
	static std::vector<float>   gold_float;
	static std::vector<uint8_t> gold_uint8;

	int err;
	if (data_type == "FLOAT32")
	{
		err = readBinFile(filename, gold_float);
		if (err)
			return err;
		*gold_ptr     = gold_float.data();
		*element_size = tensor_size * sizeof(float);
	}
	else if (data_type == "UINT8")
	{
		err = readBinFile(filename, gold_uint8);
		if (err)
			return err;
		*gold_ptr     = gold_uint8.data();
		*element_size = tensor_size * sizeof(uint8_t);
	}
	else
		return vart_ml_error::CONFIG_INVALID_QUANTIZATION_TYPE;

	return vart_ml_error::SUCCESS;
}

static void display_loading_progress(size_t current_batch, size_t total_batches)
{
	std::cout << "Loading images: " << current_batch * 100 / total_batches << "%\r" << std::flush;
}

static int load_gold_inputs(const struct options&                                   options,
                            const std::vector<vart::NpuTensorInfo>&                 inputTensors,
                            size_t                                                  inputCnt,
                            size_t                                                  nbBatches,
                            size_t                                                  model_idx,
                            std::vector<std::vector<std::vector<vart::NpuTensor>>>& runInputTensors)
{
	int err;
	for (size_t n = 0; n < nbBatches; n++)
		for (size_t i = 0; i < inputCnt; i++)
		{
			void*  gold_in;
			size_t gold_size;
			err = read_gold_file(options.goldFiles[model_idx].at(inputTensors[i].name)[n],
			                     options.data_types[model_idx].at(inputTensors[i].name),
			                     inputTensors[i].size,
			                     &gold_in,
			                     &gold_size);
			if (err)
				return vart_ml_log_err_msg(
				    (vart_ml_error::vart_ml_error_id)err,
				    "Gold input %s data_type %s is unsupported. Please use a valid data type.\n",
				    inputTensors[i].name.c_str(),
				    options.data_types[model_idx].at(inputTensors[i].name).c_str());

			for (size_t b = 0; b < options.batchSize; b++)
				memcpy(runInputTensors[n][b][i].get_virtual_address(),
				       &((uint8_t*)gold_in)[b * gold_size],
				       gold_size);
		}

	return vart_ml_error::SUCCESS;
}

static int preprocess_images(const struct options&                                   options,
                             size_t                                                  nbBatches,
                             const std::vector<vart::NpuTensorInfo>&                 inputTensors,
                             size_t                                                  inputCnt,
                             const std::vector<float>&                               quant_coeff_in,
                             size_t                                                  runner_idx,
                             std::vector<std::vector<std::vector<vart::NpuTensor>>>& runInputTensors)
{
	int err;

	if (options.useSnapshotGold)
	{
		err = load_gold_inputs(options, inputTensors, inputCnt, nbBatches, runner_idx, runInputTensors);
		if (err)
			return err;
	}
	else
	{
		for (size_t n = 0; n < nbBatches; n++)
		{
			err = preprocess_batch(options, runInputTensors[n], n * options.batchSize, quant_coeff_in);
			if (err)
				return err;

			display_loading_progress(n * options.batchSize, nbBatches * options.batchSize);
		}
		std::cout << "\rLoading images: 100%" << std::endl;
	}

	return vart_ml_error::SUCCESS;
}

static int evaluate_snapshot_gold_accuracy(
    const struct options&                                                      options,
    const std::vector<std::shared_ptr<vart::Runner>>&                          runners,
    const std::vector<std::vector<vart::NpuTensorInfo>>&                       outputTensors,
    const std::vector<size_t>&                                                 outputCnt,
    size_t                                                                     nbBatches,
    const std::vector<std::vector<std::vector<std::vector<vart::NpuTensor>>>>& runOutputTensors,
    float                                                                      accuracy[][2])
{
	int err;
	for (size_t n = 0; n < nbBatches; n++)
		for (size_t m = 0; m < runners.size(); m++)
			for (size_t i = 0; i < outputCnt[m]; i++)
			{
				std::string gold_data_type = options.data_types[m].at(outputTensors[m][i].name);
				void*       gold_ptr;
				size_t      element_size;
				err = read_gold_file(options.goldFiles[m].at(outputTensors[m][i].name)[n],
				                     gold_data_type,
				                     outputTensors[m][i].size,
				                     &gold_ptr,
				                     &element_size);
				if (err)
					return vart_ml_log_err_msg(
					    (vart_ml_error::vart_ml_error_id)err,
					    "Gold output %s data_type %s is unsupported. Please use a valid data type.\n",
					    outputTensors[m][i].name.c_str(),
					    gold_data_type.c_str());

				std::vector<uint8_t> results(element_size * options.batchSize);
				for (size_t b = 0; b < options.batchSize; b++)
					memcpy(&results[b * element_size],
					       runOutputTensors[m][n][b][i].get_virtual_address(),
					       element_size);

				bool match = (memcmp(results.data(), gold_ptr, element_size * options.batchSize) == 0);
				if (match)
				{
					accuracy[m][0] += options.batchSize;
					accuracy[m][1] += options.batchSize;
				}
				else
					vart_ml_log(LOG_INFO,
					            "%sMismatch at image %zu for output %s of model %s%s\n",
					            red.c_str(),
					            n * options.batchSize,
					            outputTensors[m][i].name.c_str(),
					            std::any_cast<std::string>(runners[m]->get_property("model_name")).c_str(),
					            reset.c_str());
			}

	return vart_ml_error::SUCCESS;
}

static int evaluate_image_gold_accuracy(
    const struct options&                                                options,
    const std::vector<std::shared_ptr<vart::Runner>>&                    runners,
    const std::vector<std::vector<vart::NpuTensorInfo>>&                 outputTensors,
    size_t                                                               nbBatches,
    std::vector<std::vector<std::vector<std::vector<vart::NpuTensor>>>>& runOutputTensors,
    size_t&                                                              nbComparedImages,
    float                                                                accuracy[][2])
{
	int err = 0;

	for (size_t m = 0; m < runners.size(); m++)
	{
		for (size_t n = 0; n < nbBatches; n++)
		{
			std::vector<std::vector<float>> results;

			err = unquantize_tensors_buffers(
			    runners[m], runOutputTensors[m][n], outputTensors[m], !options.useOnnxSubgraphs, results);
			if (err)
				return err;

			compare_gold(options,
			             n * options.batchSize,
			             results,
			             nbComparedImages,
			             accuracy[m],
			             std::any_cast<std::string>(runners[m]->get_property("model_name")));
		}
	}

	if (nbComparedImages > 0)
	{
		std::cout << std::endl;
		std::cout << "============================================================" << std::endl;
		std::cout << "Accuracy Summary:" << std::endl;

		for (size_t m = 0; m < runners.size(); m++)
			print_accuracy_summary_headless(std::any_cast<std::string>(runners[m]->get_property("model_name"))
			                                    + ' ',
			                                accuracy[m],
			                                nbComparedImages);
	}

	return 0;
}

static int print_snapshot_gold_summary(const struct options&                             options,
                                       const std::vector<std::shared_ptr<vart::Runner>>& runners,
                                       const std::vector<size_t>&                        outputCnt,
                                       float                                             accuracy[][2])
{
	int err = 0;

	std::cout << std::endl;
	std::cout << "============================================================" << std::endl;
	std::cout << "Accuracy Summary:" << std::endl;
	for (size_t m = 0; m < runners.size(); m++)
	{
		print_accuracy_summary_headless(std::any_cast<std::string>(runners[m]->get_property("model_name"))
		                                    + ' ',
		                                accuracy[m],
		                                options.nbImages * outputCnt[m]);

		if (accuracy[m][0] != options.nbImages * outputCnt[m])
		{
			vart_ml_log(LOG_INFO,
			            "[AMD] %s[TEST_ERROR]: %s: found mismatch with gold.%s\n",
			            red.c_str(),
			            std::any_cast<std::string>(runners[m]->get_property("model_name")).c_str(),
			            reset.c_str());
			err++;
			break;
		}
	}

	return err;
}

static void print_final_performance_summary(const struct options&                             options,
                                            const std::vector<std::shared_ptr<vart::Runner>>& runners,
                                            bool                                              display_fps,
                                            size_t                                            totalImages,
                                            double                                            elapsed_seconds)
{
	if (options.in_isNative || options.out_isNative)
	{
		std::cout << "[AMD] VART ML runner data format was set to ";
		if (options.in_isNative && options.out_isNative)
			std::cout << ("NATIVE.");
		else
		{
			std::cout << (options.in_isNative ? "IN-NATIVE." : "");
			std::cout << (options.out_isNative ? "OUT-NATIVE." : "");
		}
		std::cout << std::endl;
	}

	if (display_fps)
		std::cout << "[AMD] Running " << runners.size() << " models " << totalImages / elapsed_seconds
		          << " imgs/s (" << totalImages << " images)" << std::endl;
	else
		std::cout << "[AMD] Unable to report imgs/s performance using pre-process-on-the-fly mode."
		          << std::endl;

	if (options.isMultiThread)
		std::cout << "WARNING: Multi-threaded execution (" << options.nbThreads << ") "
		          << "may have harmed statistics in the performance summary." << std::endl;
}

int main(int argc, char* argv[])
{
	vart::StatusCode status = vart::StatusCode::SUCCESS;
	struct options   options;

	auto opts = base_opts();
	opts.push_back({ "nbThreads", required_argument, nullptr, OPT_NB_THREADS });
	opts.push_back({ "dataFormat", required_argument, nullptr, OPT_DATA_FORMAT });
	opts.push_back({ "forceInDdr", required_argument, nullptr, OPT_FORCE_IN_DDR });
	opts.push_back({ "forceInOutDdr", required_argument, nullptr, OPT_FORCE_IN_OUT_DDR });
	opts.push_back({ "forceOutDdr", required_argument, nullptr, OPT_FORCE_OUT_DDR });
	opts.push_back({ "fpgaArch", required_argument, nullptr, OPT_FPGA_ARCH });
	opts.push_back({ "repeat", required_argument, nullptr, OPT_REPEAT });
	opts.push_back({ "noFpsProgress", no_argument, nullptr, OPT_NO_FPS_PROGRESS });
	opts.push_back({ "useExternalQuant", no_argument, nullptr, OPT_USE_EXTERNAL_QUANT });
	opts.push_back({ "useOnnxSubgraphs", no_argument, nullptr, OPT_USE_ONNX_SUBGRAPHS });
	opts.push_back({ "useSnapshotGold", no_argument, nullptr, OPT_USE_SNAPSHOT_GOLD });
	opts.push_back({ nullptr, 0, nullptr, 0 });

	int err = read_options(argc, argv, options, opts.data(), /*multi_snapshot=*/true);
	if (err)
		return err;

	std::unordered_map<std::string, std::any> runner_options;
	setup_options(options, runner_options);

	/* Create VART ML Runners. */
	std::vector<std::shared_ptr<vart::Runner>> runners;
	for (const auto& path : options.snapshots)
		runners.push_back(vart::RunnerFactory::create_runner(vart::RunnerType::VAIML, path, runner_options));

	auto min_batch_size = get_min_or_max_batch_size(false, runners);
	err                 = read_images_options(options, min_batch_size);
	if (err)
		return err;

	/* Number of batches needed */
	size_t nbBatches = (options.nbImages + options.batchSize - 1) / options.batchSize;

	/* Number of images that got compared to a gold file. */
	size_t nbComparedImages = options.nbImages;

	float accuracy[runners.size()][2];
	memset(accuracy, 0, sizeof(accuracy));

	/* Create the input buffers */
	std::vector<std::vector<vart::NpuTensorInfo>> inputTensors(runners.size());
	std::vector<size_t>                           inputCnt(runners.size());

	/* Create the output buffers */
	std::vector<std::vector<vart::NpuTensorInfo>> outputTensors(runners.size());
	std::vector<size_t>                           outputCnt(runners.size());

	/* Set to true if memory is insufficient to pre-allocate all batches; switches to on-the-fly mode. */
	int inbuf_alloc_fail = false;

	/* Note: FPS will be displayed if all input images were preprocessed prior to inference execution. */
	bool display_fps = true;

	/* DDR list for inputs and outputs buffer, only used if native or physical native are enable*/
	auto ddr_in_list  = options.ddr_in_list;
	auto ddr_out_list = options.ddr_out_list;

	/* Prepare the quantization parameters */
	std::vector<std::vector<float>> quant_coeff_in(runners.size());

	/* Prepare run tensors [nb runners][nb batches][batch size][nb inputs/outputs] */
	std::vector<std::vector<std::vector<std::vector<vart::NpuTensor>>>> runInputTensors(runners.size());
	std::vector<std::vector<std::vector<std::vector<vart::NpuTensor>>>> runOutputTensors(runners.size());

	bool useGoldFiles = options.useSnapshotGold || !options.gold.empty();

	/* Pass 1: configure DDR lists, collect tensor info, and accumulate budget across all runners. */
	size_t                    needed_heap_all    = 0;
	size_t                    needed_heap_fly    = 0;
	size_t                    onnx_scratch_total = 0;
	std::map<uint8_t, size_t> needed_ddr_all;
	std::map<uint8_t, size_t> needed_ddr_fly;
	size_t                    output_batches = useGoldFiles ? nbBatches : 1;

	for (size_t m = 0; m < runners.size(); m++)
	{
		/* Configure ddr list for physical input and output buffers */
		err = configure_ddr_lists(runners[m], ddr_in_list[m], ddr_out_list[m]);
		if (err)
			return err;

		/* Get in/out tensor */
		inputTensors[m] = runners[m]->get_tensors_info(
		    vart::TensorDirection::INPUT, options.in_isNative ? vart::TensorType::HW : vart::TensorType::CPU);
		outputTensors[m] =
		    runners[m]->get_tensors_info(vart::TensorDirection::OUTPUT,
		                                 options.out_isNative ? vart::TensorType::HW : vart::TensorType::CPU);

		/* Get in/out tensor size */
		inputCnt[m]  = runners[m]->get_num_input_tensors();
		outputCnt[m] = runners[m]->get_num_output_tensors();

		err = setup_quantization_parameters(options,
		                                    runners[m],
		                                    inputTensors[m],
		                                    outputTensors[m],
		                                    inputCnt[m],
		                                    outputCnt[m],
		                                    quant_coeff_in[m]);
		if (err)
			return err;

		/* Accumulate CMA (heap) budget across runners */
		if (!options.in_isNative || !options.out_isNative)
		{
			size_t bytes_per_input = 0;
			if (!options.in_isNative)
				for (const auto& t : inputTensors[m])
					bytes_per_input += t.size_in_bytes;
			size_t bytes_per_output = 0;
			if (!options.out_isNative)
				for (const auto& t : outputTensors[m])
					bytes_per_output += t.size_in_bytes;

			size_t out_cost = output_batches * options.batchSize * bytes_per_output;
			needed_heap_all += nbBatches * options.batchSize * bytes_per_input + out_cost;
			needed_heap_fly += options.batchSize * bytes_per_input + out_cost;
			onnx_scratch_total += std::any_cast<size_t>(runners[m]->get_property("onnx_scratch_bytes"));
		}

		/* Accumulate NPU DDR budget across runners */
		if (options.in_isNative || options.out_isNative)
		{
			if (options.in_isNative)
			{
				auto in_all = compute_npu_ddr_needed_per_bank(
				    inputTensors[m], ddr_in_list[m], nbBatches, options.batchSize, options.nbImages);
				auto in_fly = compute_npu_ddr_needed_per_bank(
				    inputTensors[m], ddr_in_list[m], 1, options.batchSize, options.nbImages);
				for (const auto& [bank, bytes] : in_all)
					needed_ddr_all[bank] += bytes;
				for (const auto& [bank, bytes] : in_fly)
					needed_ddr_fly[bank] += bytes;
			}
			if (options.out_isNative)
			{
				auto out = compute_npu_ddr_needed_per_bank(
				    outputTensors[m], ddr_out_list[m], output_batches, options.batchSize, options.nbImages);
				for (const auto& [bank, bytes] : out)
				{
					needed_ddr_all[bank] += bytes;
					needed_ddr_fly[bank] += bytes;
				}
			}
		}
	}

	/* Single budget check across all runners combined. */
	if (!options.in_isNative || !options.out_isNative)
	{
		err = check_mem_budget(
		    needed_heap_all, needed_heap_fly, onnx_scratch_total, nbBatches, inbuf_alloc_fail);
		if (err)
			return err;
	}

	if (!inbuf_alloc_fail && (options.in_isNative || options.out_isNative))
	{
		err = check_npu_ddr_budget(needed_ddr_all, needed_ddr_fly, runners[0], nbBatches, inbuf_alloc_fail);
		if (err)
			return err;
	}

	if (inbuf_alloc_fail && options.isMultiThread)
		return vart_ml_log_err_msg(vart_ml_error::DEVICE_DDR_MALLOC_FAILURE,
		                           "Not enough memory for all %zu batches. "
		                           "Unable to switch to pre-process-on-the-fly mode in "
		                           "multi-thread run. Abort.\n",
		                           nbBatches);

	/* Pass 2: allocate buffers for each runner. */
	for (size_t m = 0; m < runners.size(); m++)
	{
		/* Initialize the input buffers */
		if (!inbuf_alloc_fail)
		{
			err = allocate_input_tensors(options,
			                             runners[m],
			                             inputTensors[m],
			                             inputCnt[m],
			                             nbBatches,
			                             ddr_in_list[m],
			                             runInputTensors[m]);
			if (err)
				return err;
		}
		else
		{
			handle_input_allocation_failure(runners[m],
			                                inputTensors[m],
			                                inputCnt[m],
			                                ddr_in_list[m],
			                                options.in_isNative,
			                                runInputTensors[m]);
			display_fps = false;
		}

		/* Initialize the output buffers */
		err = allocate_output_tensors(options,
		                              runners[m],
		                              outputTensors[m],
		                              outputCnt[m],
		                              nbBatches,
		                              ddr_out_list[m],
		                              useGoldFiles,
		                              runOutputTensors[m]);
		if (err)
			return err;

		/* Preprocess all images here to allow repeat and/or multi-threading */
		if (!inbuf_alloc_fail)
		{
			err = preprocess_images(
			    options, nbBatches, inputTensors[m], inputCnt[m], quant_coeff_in[m], m, runInputTensors[m]);
			if (err)
				return err;
		}
	}

	/* Declaration of arrays of pointers dedicated to multi-model/multi-thread run. */
	vart::JobHandle job_handle[runners.size()][options.nbThreads];
	size_t          thread_idx = 0;

	/* Execution loop */
	const auto start     = std::chrono::high_resolution_clock::now();
	auto       elapsed_s = std::chrono::duration<double>(0);

	int8_t execute_loop_id = vcd_register_global_event("execute_loop");
	for (size_t r = 0; r < options.repeatCnt; r++)
	{
		for (size_t n = 0; n < nbBatches; n++)
		{
			struct vcd_context vcd_context = { VCD_GLOBAL_ID, thread_idx };
			vcd_event(vcd_context, execute_loop_id, 1);

			for (size_t m = 0; m < runners.size(); m++)
			{
				if (inbuf_alloc_fail)
				{
					err = preprocess_batch(
					    options, runInputTensors[m][0], n * options.batchSize, quant_coeff_in[m]);
					if (err)
						return err;
				}

				size_t batch_idx     = inbuf_alloc_fail ? 0 : n;
				size_t batch_idx_out = useGoldFiles ? n : 0;

				if (runners.size() > 1 || options.isMultiThread)
					job_handle[m][thread_idx] = runners[m]->execute_async(runInputTensors[m][batch_idx],
					                                                      runOutputTensors[m][batch_idx_out]);
				else
					status = runners[m]->execute(runInputTensors[m][batch_idx],
					                             runOutputTensors[m][batch_idx_out]);
				if (status != vart::StatusCode::SUCCESS)
				{
					std::cout << red << "[TEST_ERROR] "
					          << std::any_cast<std::string>(runners[m]->get_property("model_name"))
					          << " failed during execute." << reset << std::endl;
					return static_cast<int>(status);
				}
			}

			thread_idx++;

			// Once all threads are spawned, wait for completion.
			if (thread_idx == options.nbThreads || (n + 1) * options.batchSize >= options.nbImages)
			{
				if (runners.size() > 1 || options.isMultiThread)
				{
					status = wait_for_async_jobs(
					    runners, &job_handle[0][0], options.nbThreads, thread_idx, execute_loop_id);
					if (status != vart::StatusCode::SUCCESS)
						return static_cast<int>(status);
				}

				if (display_fps)
				{
					const auto curr = std::chrono::high_resolution_clock::now();
					elapsed_s       = std::chrono::duration<double>(curr - start);

					if (options.showFpsProgress)
						display_execution_fps(options, runners.size(), n, r, elapsed_s.count());
				}

				thread_idx = 0;
			}
		}
	}
	std::cout << std::endl;

	if (options.useSnapshotGold)
	{
		err = evaluate_snapshot_gold_accuracy(
		    options, runners, outputTensors, outputCnt, nbBatches, runOutputTensors, accuracy);
		if (err)
			return err;

		err += print_snapshot_gold_summary(options, runners, outputCnt, accuracy);
	}
	// Without the categories or the gold, the accuracy can not be calculated.
	else if (!options.images_paths.empty() && !options.categories.empty() && !options.gold.empty())
	{
		err = evaluate_image_gold_accuracy(
		    options, runners, outputTensors, nbBatches, runOutputTensors, nbComparedImages, accuracy);
		if (err)
			return err;
	}

	print_final_performance_summary(
	    options, runners, display_fps, options.repeatCnt * options.nbImages, elapsed_s.count());

	if (!options.in_isNative)
		free_tensor_buffers(runInputTensors);
	if (!options.out_isNative)
		free_tensor_buffers(runOutputTensors);

	return static_cast<int>(status);
}
