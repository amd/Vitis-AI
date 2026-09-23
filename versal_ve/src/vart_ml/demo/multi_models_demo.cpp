/**
 * @file multi_models_demo.cpp
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
#include <mutex>
#include <sstream>
#include <thread>
#include <unistd.h>

#include <opencv2/opencv.hpp>

#include "common.h"
#include <utils/log.h>
#include <vart_ml_runner/vart_runner_factory.hpp>

int main(int argc, char* argv[])
{
	vart::StatusCode status = vart::StatusCode::SUCCESS;
	struct options   options;

	/* use current time as seed for random generator */
	std::srand(std::time({}));

	auto opts = base_opts();
	opts.push_back({ "noFpsProgress", no_argument, nullptr, OPT_NO_FPS_PROGRESS });
	opts.push_back({ nullptr, 0, nullptr, 0 });

	int err = read_options(argc, argv, options, opts.data(), /*multi_snapshot=*/true);
	if (err)
		return err;

	/* Add all the needed run options */
	std::unordered_map<std::string, std::any> runner_options;

	// Only async execution needs multiple threads. Each thread allocates its own IO buffers in DDR,
	// so defaulting to hardware_concurrency() wastes DDR on unused copies.
	runner_options["nb_threads"] = (size_t)1;

	/* Check if images are provided. Otherwise, random data will be used */
	bool use_dummy_image = true;
	if (!options.imgPath.empty() || !options.images_paths.empty())
	{
		runner_options["in_shape_format"] = std::string("NHWC");
		use_dummy_image                   = false;
	}

	/* Create VART ML Runners. */
	std::vector<std::shared_ptr<vart::Runner>> runners;
	for (const auto& path : options.snapshots)
		runners.push_back(vart::RunnerFactory::create_runner(vart::RunnerType::VAIML, path, runner_options));

	float accuracy[runners.size()][2];
	memset(accuracy, 0, sizeof(accuracy));

	auto min_batch_size = get_min_or_max_batch_size(false, runners);
	err                 = read_images_options(options, min_batch_size);
	if (err)
		return err;

	size_t nbImages       = options.nbImages;
	size_t batchSize      = options.batchSize;
	bool   check_accuracy = !use_dummy_image && !options.categories.empty() && !options.gold.empty();

	/* Number of images that got compared to a gold file. */
	std::vector<size_t> nbComparedImages(runners.size(), nbImages);
	if (!check_accuracy)
		nbComparedImages.assign(runners.size(), 0);

	/* Prepare run tensors [nb runners][batch size][nb inputs/outputs] */
	std::vector<std::vector<std::vector<vart::NpuTensor>>> runInputTensors(runners.size());
	std::vector<std::vector<std::vector<vart::NpuTensor>>> runOutputTensors(runners.size());

	/* Configure runners */
	for (size_t m = 0; m < runners.size(); m++)
	{
		runInputTensors[m] = allocate_tensor_buffers(runners[m], vart::TensorDirection::INPUT, batchSize);

		runOutputTensors[m] = allocate_tensor_buffers(runners[m], vart::TensorDirection::OUTPUT, batchSize);
	}

	if (use_dummy_image)
		for (size_t m = 0; m < runners.size(); m++)
			fill_dummy_buffers(runInputTensors[m]);

	const auto start     = std::chrono::high_resolution_clock::now();
	auto       elapsed_s = std::chrono::duration<double>(0);

	for (size_t n = 0; n < nbImages; n += batchSize)
	{
		if (n + batchSize > nbImages)
			batchSize = nbImages - n;

		for (size_t m = 0; m < runners.size(); m++)
		{
			if (!use_dummy_image)
			{
				err = preprocess_batch(options, runInputTensors[m], n, {});
				if (err)
					return err;
			}

			status = runners[m]->execute(runInputTensors[m], runOutputTensors[m]);
			if (status != vart::StatusCode::SUCCESS)
			{
				std::cout << red << "[TEST_ERROR] "
				          << std::any_cast<std::string>(runners[m]->get_property("model_name"))
				          << " failed during execute." << reset << std::endl;
				return static_cast<int>(status);
			}

			if (check_accuracy)
			{
				std::vector<std::vector<float>> results =
				    extract_float_results(runners[m], runOutputTensors[m], batchSize);

				compare_gold(options,
				             n,
				             results,
				             nbComparedImages[m],
				             accuracy[m],
				             std::any_cast<std::string>(runners[m]->get_property("model_name")));
			}

			const auto curr = std::chrono::high_resolution_clock::now();
			elapsed_s       = std::chrono::duration<double>(curr - start);

			if (options.showFpsProgress)
				display_fps_progress(n + batchSize, nbImages, elapsed_s.count());
		}
	}

	for (size_t m = 0; m < runners.size(); m++)
	{
		if ((check_accuracy) && (nbComparedImages[m] > 0))
		{
			std::cout << std::endl;
			std::cout << "============================================================" << std::endl;
			std::cout << "Accuracy Summary:" << std::endl;

			print_accuracy_summary_headless(std::any_cast<std::string>(runners[m]->get_property("model_name"))
			                                    + ' ',
			                                accuracy[m],
			                                nbComparedImages[m]);
		}

		std::cout << "[AMD] [" << std::any_cast<std::string>(runners[m]->get_property("model_name")) << "] "
		          << (nbImages * runners.size()) / elapsed_s.count() << " imgs/s (" << nbImages << " images)"
		          << std::endl;
	}

	free_tensor_buffers(runInputTensors);
	free_tensor_buffers(runOutputTensors);

	return static_cast<int>(status);
}
