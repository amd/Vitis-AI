/**
 * @file hot_swap_demo.cpp
 *
 * @copyright Copyright 2026 Advanced Micro Devices Inc.
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

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>

#include <opencv2/opencv.hpp>

#include "common.h"
#include <utils/log.h>
#include <vart_ml_runner/vart_runner_factory.hpp>

static void print_addrs(const char* label, const std::vector<uint64_t>& addrs)
{
	std::cout << label << " phy addrs: [";
	for (size_t i = 0; i < addrs.size(); i++)
	{
		if (i)
			std::cout << ", ";
		std::cout << "0x" << std::hex << addrs[i] << std::dec;
	}
	std::cout << "]";
}

static void print_ddr_free(const std::vector<size_t>& ddr_free)
{
	std::cout << "DDR free [";
	for (size_t i = 0; i < ddr_free.size(); i++)
	{
		if (i)
			std::cout << ", ";
		std::cout << "bank" << i << ": " << ddr_free[i];
	}
	std::cout << "]";
}

int main(int argc, char* argv[])
{
	vart::StatusCode status = vart::StatusCode::SUCCESS;
	struct options   options;

	auto opts = base_opts();
	opts.push_back({ nullptr, 0, nullptr, 0 });

	int err = read_options(argc, argv, options, opts.data());
	if (err)
		return err;

	/* Add all the needed run options */
	std::unordered_map<std::string, std::any> runner_options;

	// Each thread allocates its own IO buffers in DDR, we only need one so defaulting to
	// hardware_concurrency() wastes DDR on unused copies.
	runner_options["nb_threads"] = (size_t)1;

	/* Check if images are provided. Otherwise, random data will be used */
	bool use_dummy_image = true;
	if (!options.imgPath.empty() || !options.images_paths.empty())
	{
		runner_options["in_shape_format"] = std::string("NHWC");
		use_dummy_image                   = false;
	}

	/* Create VART ML Runner. */
	auto runner =
	    vart::RunnerFactory::create_runner(vart::RunnerType::VAIML, options.snapshots[0], runner_options);

	float accuracy[2] = { 0, 0 };

	err = read_images_options(options, runner->get_batch_size());
	if (err)
		return err;

	size_t nbIterations = options.nbImages / options.batchSize;
	if (nbIterations == 0)
		nbIterations = 10;

	bool check_accuracy = !use_dummy_image && !options.categories.empty() && !options.gold.empty();

	/* Number of images that got compared to a gold file. */
	size_t nbComparedImages = check_accuracy ? options.nbImages : 0;

	auto inputInfos  = runner->get_tensors_info(vart::TensorDirection::INPUT, vart::TensorType::HW);
	auto outputInfos = runner->get_tensors_info(vart::TensorDirection::OUTPUT, vart::TensorType::HW);

	size_t ddr_failures  = 0;
	size_t addr_failures = 0;
	size_t exec_failures = 0;

	std::vector<size_t>   baseline_ddr_free;
	std::vector<uint64_t> baseline_input_phy;
	std::vector<uint64_t> baseline_output_phy;

	for (size_t iter = 0; iter < nbIterations; iter++)
	{
		if (iter > 0)
		{
			std::cout << "---\nIteration " << iter << "/" << nbIterations << ": creating runner..."
			          << std::endl;
			runner.reset();
			runner = vart::RunnerFactory::create_runner(
			    vart::RunnerType::VAIML, options.snapshots[0], runner_options);
		}
		else
			std::cout << "---\nIteration 0/" << nbIterations << std::endl;

		/* ---- DDR free space check ---- */
		auto ddr_free = std::any_cast<std::vector<size_t>>(runner->get_property("ddr_free_bytes"));
		bool ddr_ok   = true;

		if (iter == 0)
			baseline_ddr_free = ddr_free;
		else if (ddr_free != baseline_ddr_free)
		{
			ddr_ok = false;
			ddr_failures++;
			print_ddr_free(ddr_free);
			std::cout << red << " MISMATCH" << reset << std::endl;
			for (size_t b = 0; b < ddr_free.size(); b++)
			{
				if (ddr_free[b] != baseline_ddr_free[b])
					std::cout << red << "  bank" << b << " expected " << baseline_ddr_free[b] << " got "
					          << ddr_free[b] << " (delta " << (int64_t)(ddr_free[b] - baseline_ddr_free[b])
					          << ")" << reset << std::endl;
			}
		}

		/* ---- Allocate native IO tensors for address checking ---- */
		std::vector<std::vector<vart::NpuTensor>> runInputTensors(1);
		std::vector<std::vector<vart::NpuTensor>> runOutputTensors(1);

		for (size_t i = 0; i < inputInfos.size(); i++)
			runInputTensors[0].push_back(runner->allocate_npu_tensor(inputInfos[i]));

		for (size_t i = 0; i < outputInfos.size(); i++)
			runOutputTensors[0].push_back(runner->allocate_npu_tensor(outputInfos[i]));

		/* ---- Record / verify physical addresses ---- */
		std::vector<uint64_t> cur_input_phy, cur_output_phy;
		bool                  addr_ok = true;

		for (auto& t : runInputTensors[0])
			cur_input_phy.push_back(t.get_physical_address());
		for (auto& t : runOutputTensors[0])
			cur_output_phy.push_back(t.get_physical_address());

		if (iter == 0)
		{
			baseline_input_phy  = cur_input_phy;
			baseline_output_phy = cur_output_phy;

			print_addrs("Input ", cur_input_phy);
			std::cout << std::endl;
			print_addrs("Output", cur_output_phy);
			std::cout << std::endl;
		}
		else if (cur_input_phy != baseline_input_phy || cur_output_phy != baseline_output_phy)
		{
			addr_ok = false;
			addr_failures++;
			if (cur_input_phy != baseline_input_phy)
			{
				print_addrs("Input ", cur_input_phy);
				std::cout << red << " MISMATCH" << reset << std::endl;
			}
			if (cur_output_phy != baseline_output_phy)
			{
				print_addrs("Output", cur_output_phy);
				std::cout << red << " MISMATCH" << reset << std::endl;
			}
		}

		/* ---- Run inference ---- */
		bool exec_ok = true;

		if (!use_dummy_image)
		{
			auto cpuInputTensors =
			    allocate_tensor_buffers(runner, vart::TensorDirection::INPUT, options.batchSize);
			auto cpuOutputTensors =
			    allocate_tensor_buffers(runner, vart::TensorDirection::OUTPUT, options.batchSize);

			size_t img_idx = (iter * options.batchSize) % options.images_paths.size();

			err = preprocess_batch(options, cpuInputTensors, img_idx, {});
			if (err)
				return err;

			status = runner->execute(cpuInputTensors, cpuOutputTensors);

			if (status != vart::StatusCode::SUCCESS)
			{
				exec_ok = false;
				std::cout << red << "[TEST_ERROR] "
				          << std::any_cast<std::string>(runner->get_property("model_name"))
				          << " failed during execute." << reset << std::endl;
				exec_failures++;
			}
			else if (check_accuracy)
			{
				std::vector<std::vector<float>> results =
				    extract_float_results(runner, cpuOutputTensors, options.batchSize);

				compare_gold(options,
				             img_idx,
				             results,
				             nbComparedImages,
				             accuracy,
				             std::any_cast<std::string>(runner->get_property("model_name")));
			}
		}
		else
		{
			status = runner->execute(runInputTensors, runOutputTensors);

			if (status != vart::StatusCode::SUCCESS)
			{
				exec_ok = false;
				std::cout << red << "[TEST_ERROR] Inference FAILED (status " << static_cast<int>(status)
				          << ")" << reset << std::endl;
				exec_failures++;
			}
		}

		if (iter == 0)
			std::cout << green << "Baseline recorded" << reset << std::endl;
		else if (ddr_ok && addr_ok && exec_ok)
			std::cout << green << "Iteration OK" << reset << std::endl;
	}

	/* ---- Final summary ---- */
	std::cout << "\n[HOT_SWAP] " << nbIterations << " iterations" << std::endl;

	if (ddr_failures == 0 && addr_failures == 0 && exec_failures == 0)
		std::cout << green << "[HOT_SWAP] PASSED. No DDR leaks. Addresses stable." << reset << std::endl;
	else
	{
		if (ddr_failures)
			std::cout << red << "[HOT_SWAP] DDR free mismatch in " << ddr_failures << " iteration(s)" << reset
			          << std::endl;
		if (addr_failures)
			std::cout << red << "[HOT_SWAP] Address mismatch in " << addr_failures << " iteration(s)" << reset
			          << std::endl;
		if (exec_failures)
			std::cout << red << "[TEST_ERROR] Inference failed in " << exec_failures << " iteration(s)"
			          << reset << std::endl;
		std::cout << red << "[HOT_SWAP] FAILED." << reset << std::endl;
	}

	if (nbComparedImages > 0)
		print_accuracy_summary(
		    std::any_cast<std::string>(runner->get_property("model_name")), accuracy, nbComparedImages);

	return (ddr_failures || addr_failures || exec_failures) ? 1 : 0;
}
