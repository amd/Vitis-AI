/**
 * @file multi_threads_demo.cpp
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

#include <chrono>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <thread>

#include <opencv2/opencv.hpp>

#include "common.h"
#include "utils/log.h"
#include "vart_ml_runner/vart_runner_factory.hpp"

int run(std::shared_ptr<vart::Runner>                           runner,
        size_t                                                  nbBatches,
        std::vector<std::vector<std::vector<vart::NpuTensor>>>& runInputTensors,
        std::vector<std::vector<std::vector<vart::NpuTensor>>>& runOutputTensors,
        std::chrono::duration<double>&                          elapsed_s)
{
	vart::StatusCode status = vart::StatusCode::SUCCESS;

	elapsed_s        = std::chrono::duration<double>(0);
	const auto start = std::chrono::high_resolution_clock::now();

	for (size_t n = 0; n < nbBatches; n++)
	{
		status = runner->execute(runInputTensors[n], runOutputTensors[n]);
		if (status != vart::StatusCode::SUCCESS)
		{
			std::cout << red << "[TEST_ERROR] "
			          << std::any_cast<std::string>(runner->get_property("model_name"))
			          << " failed during execute." << reset << std::endl;
			return static_cast<int>(status);
		}
	}

	const auto curr = std::chrono::high_resolution_clock::now();
	elapsed_s       = std::chrono::duration<double>(curr - start);

	return static_cast<int>(status);
}

int main(int argc, char* argv[])
{
	struct options options;

	/* Use current time as seed for random generator. */
	std::srand(std::time({}));

	auto opts = base_opts();
	opts.push_back({ "nbThreads", required_argument, nullptr, OPT_NB_THREADS });
	opts.push_back({ "dataFormat", required_argument, nullptr, OPT_DATA_FORMAT });
	opts.push_back({ "forceInDdr", required_argument, nullptr, OPT_FORCE_IN_DDR });
	opts.push_back({ "forceInOutDdr", required_argument, nullptr, OPT_FORCE_IN_OUT_DDR });
	opts.push_back({ "forceOutDdr", required_argument, nullptr, OPT_FORCE_OUT_DDR });
	opts.push_back({ "fpgaArch", required_argument, nullptr, OPT_FPGA_ARCH });
	opts.push_back({ nullptr, 0, nullptr, 0 });

	int err = read_options(argc, argv, options, opts.data(), /*multi_snapshot=*/true);
	if (err)
		return err;

	/* Add all the needed run options. */
	std::unordered_map<std::string, std::any> runner_options;

	// Only async execution needs multiple threads. Each thread allocates its own IO buffers in DDR,
	// so defaulting to hardware_concurrency() wastes DDR on unused copies.
	runner_options["nb_threads"] = (size_t)1;

	/* Check if images are provided. Otherwise, random data will be used. */
	bool use_dummy_image = true;
	if (!options.imgPath.empty() || !options.images_paths.empty())
	{
		runner_options["in_shape_format"] = std::string("NHWC");
		use_dummy_image                   = false;
	}

	bool check_accuracy = !use_dummy_image && !options.categories.empty() && !options.gold.empty();

	/* If only one snapshot path was provided, duplicate it so it runs in two threads. */
	if (options.snapshots.size() == 1)
	{
		options.snapshots.push_back(options.snapshots.front());
		options.ddr_in_list.resize(options.snapshots.size());
		options.ddr_out_list.resize(options.snapshots.size());
	}

	/* Create VART ML Runners. */
	std::vector<std::shared_ptr<vart::Runner>> runners;
	for (const auto& path : options.snapshots)
		runners.push_back(vart::RunnerFactory::create_runner(vart::RunnerType::VAIML, path, runner_options));

	auto min_batch_size = get_min_or_max_batch_size(false, runners);
	err                 = read_images_options(options, min_batch_size);
	if (err)
		return err;

	/* Number of batches needed. */
	size_t nbBatches = (options.nbImages + options.batchSize - 1) / options.batchSize;

	std::vector<std::vector<vart::NpuTensorInfo>> inputTensors(runners.size());
	std::vector<size_t>                           inputCnt(runners.size());
	std::vector<std::vector<vart::NpuTensorInfo>> outputTensors(runners.size());
	std::vector<size_t>                           outputCnt(runners.size());

	/* DDR list for inputs and outputs buffer, only used if native or physical native are enable. */
	auto ddr_in_list  = options.ddr_in_list;
	auto ddr_out_list = options.ddr_out_list;

	/* Prepare the quantization parameters */
	std::vector<std::vector<float>> quant_coeff_in(runners.size());

	/* Prepare run tensors [nb runners][nb batches][batch size][nb inputs/outputs]. */
	std::vector<std::vector<std::vector<std::vector<vart::NpuTensor>>>> runInputTensors(runners.size());
	std::vector<std::vector<std::vector<std::vector<vart::NpuTensor>>>> runOutputTensors(runners.size());

	/* Pass 1: configure DDR lists, collect tensor info, and accumulate budget across all runners. */
	size_t                    needed_heap  = 0;
	size_t                    onnx_scratch = 0;
	std::map<uint8_t, size_t> needed_ddr_all;
	std::map<uint8_t, size_t> needed_ddr_fly;

	for (size_t m = 0; m < runners.size(); m++)
	{
		/* Configure ddr list for physical input and output buffers */
		err = configure_ddr_lists(runners[m], ddr_in_list[m], ddr_out_list[m]);
		if (err)
			return err;

		/* Get in/out tensors info. */
		inputTensors[m] = runners[m]->get_tensors_info(
		    vart::TensorDirection::INPUT, options.in_isNative ? vart::TensorType::HW : vart::TensorType::CPU);
		outputTensors[m] =
		    runners[m]->get_tensors_info(vart::TensorDirection::OUTPUT,
		                                 options.out_isNative ? vart::TensorType::HW : vart::TensorType::CPU);

		/* Get in/out tensor size */
		inputCnt[m]  = runners[m]->get_num_input_tensors();
		outputCnt[m] = runners[m]->get_num_output_tensors();

		/* Get the quantization parameters */
		for (size_t i = 0; i < inputCnt[m]; i++)
			quant_coeff_in[m].push_back(runners[m]->get_quant_parameters(inputTensors[m][i].name).scale);

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

			needed_heap += nbBatches * options.batchSize * (bytes_per_input + bytes_per_output);
			onnx_scratch += std::any_cast<size_t>(runners[m]->get_property("onnx_scratch_bytes"));
		}

		/* Accumulate NPU DDR budget across runners */
		if (options.in_isNative || options.out_isNative)
		{
			if (options.in_isNative)
			{
				auto in = compute_npu_ddr_needed_per_bank(
				    inputTensors[m], ddr_in_list[m], nbBatches, options.batchSize, options.nbImages);
				for (const auto& [bank, bytes] : in)
					needed_ddr_all[bank] += bytes;
			}
			if (options.out_isNative)
			{
				auto out = compute_npu_ddr_needed_per_bank(
				    outputTensors[m], ddr_out_list[m], nbBatches, options.batchSize, options.nbImages);
				for (const auto& [bank, bytes] : out)
					needed_ddr_all[bank] += bytes;
			}
		}
	}

	/* Single budget check across all runners combined.
	 * multi_threads_demo has no on-the-fly fallback: treat any shortfall as fatal. */
	int inbuf_alloc_fail = false;

	if (!options.in_isNative || !options.out_isNative)
	{
		err = check_mem_budget(needed_heap, needed_heap, onnx_scratch, nbBatches, inbuf_alloc_fail);
		if (err || inbuf_alloc_fail)
			return err ? err
			           : vart_ml_log_err_msg(vart_ml_error::DEVICE_DDR_MALLOC_FAILURE,
			                                 "Not enough memory to pre-allocate all batches for "
			                                 "multi-threaded run.\n");
	}

	if (options.in_isNative || options.out_isNative)
	{
		err = check_npu_ddr_budget(needed_ddr_all, needed_ddr_all, runners[0], nbBatches, inbuf_alloc_fail);
		if (err || inbuf_alloc_fail)
			return err ? err
			           : vart_ml_log_err_msg(vart_ml_error::DEVICE_DDR_MALLOC_FAILURE,
			                                 "Not enough NPU DDR to pre-allocate all batches for "
			                                 "multi-threaded run.\n");
	}

	/* Pass 2: allocate and preprocess buffers for each runner. */
	for (size_t m = 0; m < runners.size(); m++)
	{
		runInputTensors[m].resize(nbBatches);
		runOutputTensors[m].resize(nbBatches);
		for (size_t n = 0; n < nbBatches; n++)
		{
			size_t batchSize_curr = options.batchSize;
			if ((n + 1) * options.batchSize > options.nbImages)
				batchSize_curr = options.nbImages - n * options.batchSize;

			runInputTensors[m][n].resize(batchSize_curr);
			runOutputTensors[m][n].resize(batchSize_curr);

			for (size_t b = 0; b < batchSize_curr; b++)
			{
				/* Initialize the input buffers. */
				for (size_t i = 0; i < inputCnt[m]; i++)
				{
					if (options.in_isNative)
					{
						/* In ultrascale, sample buffers of a same batch must be contiguous in memory. */
						if (options.fpga_arch == "ultrascale")
						{
							err = allocate_ultrascale_batch_tensors(options,
							                                        runners[m],
							                                        inputTensors[m][i],
							                                        batchSize_curr,
							                                        runInputTensors[m][n],
							                                        "input");

							if (err)
								return err;
						}
						else
						{
							/* The performance are better when the load is shared between the DDRs */
							runInputTensors[m][n][b].push_back(std::move(runners[m]->allocate_npu_tensor(
							    inputTensors[m][i], ddr_in_list[m][(b + i) % ddr_in_list.size()])));
						}
					}
					else
					{
						auto* buf = std::malloc(inputTensors[m][i].size_in_bytes);
						runInputTensors[m][n][b].push_back(
						    vart::NpuTensor(inputTensors[m][i], buf, vart::MemoryType::USER_POINTER_CMA));
					}
				}

				/* Initialize the output buffers. */
				for (size_t i = 0; i < outputCnt[m]; i++)
				{
					if (options.out_isNative)
					{
						/* In ultrascale, sample buffers of a same batch must be contiguous in memory. */
						if (options.fpga_arch == "ultrascale")
						{
							allocate_ultrascale_batch_tensors(options,
							                                  runners[m],
							                                  outputTensors[m][i],
							                                  batchSize_curr,
							                                  runOutputTensors[m][n],
							                                  "output");

							if (err)
								return err;
						}
						else
						{
							/* The performance are better when the load is shared between the DDRs */
							runOutputTensors[m][n][b].push_back(std::move(runners[m]->allocate_npu_tensor(
							    outputTensors[m][i], ddr_out_list[m][(b + i) % ddr_in_list.size()])));
						}
					}
					else
					{
						auto* buf = std::malloc(outputTensors[m][i].size_in_bytes);
						runOutputTensors[m][n][b].push_back(
						    vart::NpuTensor(outputTensors[m][i], buf, vart::MemoryType::USER_POINTER_CMA));
					}
				}
			}

			/* If no images were given, fill the input tensor with random data. */
			if (use_dummy_image)
				fill_dummy_buffers(runInputTensors[m][n]);
			else
			{
				int err = preprocess_batch(options, runInputTensors[m][n], n, quant_coeff_in[m]);
				if (err)
					return err;
			}
		}
	}

	/* Start threads. */
	std::vector<std::thread>                   threads;
	std::vector<std::chrono::duration<double>> elapsed_s(runners.size());
	for (size_t m = 0; m < runners.size(); m++)
		threads.push_back(std::thread(run,
		                              runners[m],
		                              nbBatches,
		                              std::ref(runInputTensors[m]),
		                              std::ref(runOutputTensors[m]),
		                              std::ref(elapsed_s[m])));

	for (auto& t : threads)
		t.join();

	for (size_t m = 0; m < runners.size(); m++)
	{
		/* If needed, compare gold and display accuracy. */
		if (check_accuracy)
		{
			size_t nbComparedImages = options.nbImages;
			float  accuracy[2]      = { 0, 0 };

			for (size_t n = 0; n < nbBatches; n++)
			{
				std::vector<std::vector<float>> results;

				err = unquantize_tensors_buffers(
				    runners[m], runOutputTensors[m][n], outputTensors[m], !options.useOnnxSubgraphs, results);
				if (err)
					return err;

				compare_gold(options,
				             n,
				             results,
				             nbComparedImages,
				             accuracy,
				             std::any_cast<std::string>(runners[m]->get_property("model_name")));
			}

			if (nbComparedImages > 0)
				print_accuracy_summary(std::any_cast<std::string>(runners[m]->get_property("model_name")),
				                       accuracy,
				                       nbComparedImages);
		}

		std::cout << "[AMD] [" << std::any_cast<std::string>(runners[m]->get_property("model_name")) << "] "
		          << options.nbImages / elapsed_s[m].count() << " imgs/s (" << options.nbImages << " images)"
		          << std::endl;
	}

	if (!options.in_isNative)
		free_tensor_buffers(runInputTensors);
	if (!options.out_isNative)
		free_tensor_buffers(runOutputTensors);

	return 0;
}
