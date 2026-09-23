/**
 * @file clocks.cpp
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

#include <cstdlib>
#include <iostream>

#include "clocks.h"
#include "io/io.h"
#include "npu_runner/npu_runner.h"
#include "utils/log.h"
#include "utils/shell.h"

int frequency(int argc, const char** argv)
{
	float    clk;
	uint32_t ip_idx;

	int err = npu_connect_peripherals();
	if (err)
		return err;

	switch (argc)
	{
	case 1:
		for (size_t i = 0; i < npu_get_nb_ip(); i++)
		{
			err = npu_connect_ip(i);
			if (err)
				return err;

			if (!is_mmcm_en(i))
				return vart_ml_log_err_msg(vart_ml_error::CONFIG_UNSUPPORTED_FEATURE,
				                           "Frequency handling is not available for this board.\n");

			err = npu_read_mmcm_freq(i, &clk);
			if (err)
				return err;

			vart_ml_log(LOG_INFO, "Current frequency");

			if (npu_get_nb_ip() != 1)
				vart_ml_log(LOG_INFO, " of IP %zu", i);

			vart_ml_log(LOG_INFO, ": %d MHz\n", (int)clk);
		}
		break;
	case 2:
		ip_idx = parse_uint(argv[1]);
		if (ip_idx >= npu_get_nb_ip())
			return vart_ml_log_err_msg(vart_ml_error::TOOLS_BAD_USAGE,
			                           "IP index is too high, there is only %d IPs available\n",
			                           npu_get_nb_ip());

		err = npu_connect_ip(ip_idx);
		if (err)
			return err;

		err = npu_read_mmcm_freq(ip_idx, &clk);
		if (err)
			return err;

		vart_ml_log(LOG_INFO, "Current frequency: %d MHz\n", (int)clk);
		break;
	case 3:
		ip_idx = parse_uint(argv[1]);
		if (ip_idx >= npu_get_nb_ip())
			return vart_ml_log_err_msg(vart_ml_error::TOOLS_BAD_USAGE,
			                           "IP index is too high, there is only %d IPs available\n",
			                           npu_get_nb_ip());

		err = npu_connect_ip(ip_idx);
		if (err)
			return err;

		if (!is_mmcm_en(ip_idx))
			return vart_ml_log_err_msg(vart_ml_error::CONFIG_UNSUPPORTED_FEATURE,
			                           "Frequency handling is not available for this board.\n");

		err = npu_change_mmcm_freq(ip_idx, strtof(argv[2], NULL));
		if (err)
			return err;

		// Check the new frequency
		err = npu_read_mmcm_freq(ip_idx, &clk);
		if (err)
			return err;

		vart_ml_log(LOG_INFO, "Current frequency: %d MHz\n", (int)clk);
		break;
	default:
		return vart_ml_log_err_msg(
		    vart_ml_error::TOOLS_BAD_USAGE, "Usage: vart_ml_tools %s [target frequency].\n", argv[0]);
	}

	return vart_ml_error::SUCCESS;
}

int aieFrequency(int argc, const char** argv)
{
	float clk;

	int err = npu_connect_peripherals();
	if (err)
		return err;

	err = npu_connect_ip(0);
	if (err)
		return err;

	switch (argc)
	{
	case 1:
		err = npu_read_aie_freq(&clk);
		if (err)
			return err;

		vart_ml_log(LOG_INFO, "Current AIE frequency: %d MHz\n", (int)clk);
		break;
	case 2:
		err = npu_change_aie_freq(strtof(argv[1], NULL));
		if (err)
			return err;

		// Check the new frequency
		err = npu_read_aie_freq(&clk);
		if (err)
			return err;

		vart_ml_log(LOG_INFO, "Current AIE frequency: %d MHz\n", (int)clk);
		break;
	default:
		return vart_ml_log_err_msg(
		    vart_ml_error::TOOLS_BAD_USAGE, "Usage: vart_ml_tools %s [target frequency].\n", argv[0]);
		break;
	}

	return vart_ml_error::SUCCESS;
}

int start_clocks(int argc, const char** argv)
{
	int err = npu_connect_peripherals();
	if (err)
		return err;

	uint32_t ip_idx;
	switch (argc)
	{
	case 1:
		for (size_t i = 0; i < npu_get_nb_ip(); i++)
		{
			err = npu_connect_ip(i);
			if (err)
				return err;

			err = npu_start_clocks(i, 0xff);
			if (err)
				return err;
		}
		break;
	case 2:
		ip_idx = parse_uint(argv[1]);
		if (ip_idx >= npu_get_nb_ip())
			return vart_ml_log_err_msg(vart_ml_error::TOOLS_BAD_USAGE,
			                           "IP index is too high, there is only %d IPs available\n",
			                           npu_get_nb_ip());

		err = npu_connect_ip(ip_idx);
		if (err)
			return err;

		err = npu_start_clocks(ip_idx, 0xff);
		if (err)
			return err;
		break;
	default:
		return vart_ml_log_err_msg(
		    vart_ml_error::TOOLS_BAD_USAGE, "usage: vart_ml_tools %s [ip_idx].\n", argv[0]);
		break;
	}

	return vart_ml_error::SUCCESS;
}

int stop_clocks(int argc, const char** argv)
{
	int err = npu_connect_peripherals();
	if (err)
		return err;

	uint32_t ip_idx;
	switch (argc)
	{
	case 1:
		for (size_t i = 0; i < npu_get_nb_ip(); i++)
		{
			err = npu_connect_ip(i);
			if (err)
				return err;

			err = npu_stop_clocks(i);
			if (err)
				return err;
		}
		break;
	case 2:
		ip_idx = parse_uint(argv[1]);
		if (ip_idx >= npu_get_nb_ip())
			return vart_ml_log_err_msg(vart_ml_error::TOOLS_BAD_USAGE,
			                           "IP index is too high, there is only %d IPs available\n",
			                           npu_get_nb_ip());

		err = npu_connect_ip(ip_idx);
		if (err)
			return err;

		err = npu_stop_clocks(ip_idx);
		if (err)
			return err;
		break;
	default:
		return vart_ml_log_err_msg(
		    vart_ml_error::TOOLS_BAD_USAGE, "usage: vart_ml_tools %s [ip_idx].\n", argv[0]);
		break;
	}

	return vart_ml_error::SUCCESS;
}
