/**
 * @file async_demo.cpp
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

#include <opencv2/opencv.hpp>

#include "common.h"
#include <utils/log.h>
#include <vart_ml_runner/vart_runner_factory.hpp>

#define TIMEOUT 1000

int main(int argc, char* argv[])
{
	vart::StatusCode status = vart::StatusCode::SUCCESS;
	struct options   options;

	/* use current time as seed for random generator */
	std::srand(std::time({}));

	auto opts = base_opts();
	opts.push_back({ "noFpsProgress", no_argument, nullptr, OPT_NO_FPS_PROGRESS });
	opts.push_back({ "nbThreads", required_argument, nullptr, OPT_NB_THREADS });
	opts.push_back({ nullptr, 0, nullptr, 0 });

	int err = read_options(argc, argv, options, opts.data());
	if (err)
		return err;

	/* Add all the needed run options */
	std::unordered_map<std::string, std::any> runner_options;

	/* Check if images are provided. Otherwise, random data will be used */
	bool use_dummy_image = true;
	if (!options.imgPath.empty() || !options.images_paths.empty())
	{
		runner_options["in_shape_format"] = std::string("NHWC");
		use_dummy_image                   = false;
	}

	/* Configure multi-threading: default to 2 threads if --nbThreads was not given */
	if (!options.isMultiThread)
		options.nbThreads = 2;

	runner_options["nb_threads"] = options.nbThreads;

	/* Create VART ML Runner. */
	auto runner =
	    vart::RunnerFactory::create_runner(vart::RunnerType::VAIML, options.snapshots[0], runner_options);

	float accuracy[2] = { 0, 0 };

	err = read_images_options(options, runner->get_batch_size());
	if (err)
		return err;

	size_t nbImages  = options.nbImages;
	size_t batchSize = options.batchSize;

	/* Number of images that got compared to a gold file. */
	size_t nbComparedImages = nbImages;
	if (use_dummy_image || options.categories.empty() || options.gold.empty())
		nbComparedImages = 0;

	/* Prepare run tensors [nb threads][batch size][nb inputs/outputs] */
	std::vector<std::vector<std::vector<vart::NpuTensor>>> runInputTensors(options.nbThreads);
	std::vector<std::vector<std::vector<vart::NpuTensor>>> runOutputTensors(options.nbThreads);

	/* Configure runners */
	for (size_t t = 0; t < options.nbThreads; t++)
	{
		runInputTensors[t] = allocate_tensor_buffers(runner, vart::TensorDirection::INPUT, batchSize);

		runOutputTensors[t] = allocate_tensor_buffers(runner, vart::TensorDirection::OUTPUT, batchSize);
	}

	if (use_dummy_image)
		for (size_t t = 0; t < options.nbThreads; t++)
			fill_dummy_buffers(runInputTensors[t]);

	const auto start     = std::chrono::high_resolution_clock::now();
	auto       elapsed_s = std::chrono::duration<double>(0);

	/* Declaration of arrays of pointers dedicated to multi-thread run. */
	vart::JobHandle job_handle[options.nbThreads];
	size_t          thread_idx     = 0;
	size_t          thread_img_idx = 0;

	for (size_t n = 0; n < nbImages; n += batchSize)
	{
		if (!use_dummy_image)
		{
			if (n + batchSize > nbImages)
			{
				runInputTensors[thread_idx].resize(nbImages - n);
				runOutputTensors[thread_idx].resize(nbImages - n);
			}

			err = preprocess_batch(options, runInputTensors[thread_idx], n, {});
			if (err)
				return err;
		}

		job_handle[thread_idx] =
		    runner->execute_async(runInputTensors[thread_idx], runOutputTensors[thread_idx]);

		thread_idx++;

		// Once all threads are spawned, wait for completion.
		if (thread_idx == options.nbThreads || n + batchSize >= nbImages)
		{
			for (size_t t = 0; t < thread_idx; t++)
			{
				status = runner->wait(job_handle[t], std::chrono::milliseconds(TIMEOUT));
				if (status != vart::StatusCode::SUCCESS)
				{
					std::cout << red << "[TEST_ERROR] "
					          << std::any_cast<std::string>(runner->get_property("model_name"))
					          << " failed during execute." << reset << std::endl;
					return static_cast<int>(status);
				}

				if (!use_dummy_image && !options.categories.empty() && !options.gold.empty())
				{
					std::vector<std::vector<float>> results =
					    extract_float_results(runner, runOutputTensors[t], runOutputTensors[t].size());

					compare_gold(options,
					             thread_img_idx,
					             results,
					             nbComparedImages,
					             accuracy,
					             std::any_cast<std::string>(runner->get_property("model_name")));

					thread_img_idx += batchSize;
				}
			}

			const auto curr = std::chrono::high_resolution_clock::now();
			elapsed_s       = std::chrono::duration<double>(curr - start);

			if (options.showFpsProgress)
				display_fps_progress(n + batchSize, nbImages, elapsed_s.count());
			thread_idx = 0;
		}
	}

	if (nbComparedImages > 0)
		print_accuracy_summary(
		    std::any_cast<std::string>(runner->get_property("model_name")), accuracy, nbComparedImages);

	std::cout << "[AMD] [" << std::any_cast<std::string>(runner->get_property("model_name")) << "] "
	          << nbImages / elapsed_s.count() << " imgs/s (" << nbImages << " images)" << std::endl;

	free_tensor_buffers(runInputTensors);
	free_tensor_buffers(runOutputTensors);

	return static_cast<int>(status);
}
