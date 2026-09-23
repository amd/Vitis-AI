/**
 * @file ctrlbus_poke.cpp
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
#include <ctime>
#include <unistd.h>

#include "ctrlbus_poke.h"
#include "io/io.h"
#include "npu_runner/npu_runner.h"
#include "utils/fpga_info.h"
#include "utils/log.h"
#include "utils/npu_reg.h"
#include "utils/shell.h"

#define NO_TIMEOUT -1

int ctrlbus_poke(int argc, const char** argv)
{
	uint32_t ip_idx;
	uint64_t addr;
	uint32_t buf;
	int      err;

	switch (argc)
	{
	case 2:
		if (npu_get_nb_ip())
		{
			err = npu_connect_ip(0);
			if (err)
				return err;

			addr = parse_uint(argv[1]);
			npu_read_cfg(addr, &buf, 1, 0);
			vart_ml_log(LOG_INFO, "0x%08x\n", buf);
		}
		else
			return vart_ml_log_err_msg(vart_ml_error::TOOLS_BAD_USAGE,
			                           "Usage: vart_ml_tools %s <ip_idx> <address> [word to write].\n",
			                           argv[0]);
		break;
	case 3:
		ip_idx = parse_uint(argv[1]);
		if (ip_idx >= npu_get_nb_ip() + npu_get_nb_pp())
			return vart_ml_log_err_msg(vart_ml_error::TOOLS_BAD_USAGE,
			                           "IP index is too high, there is only %u IPs available\n",
			                           npu_get_nb_ip() + npu_get_nb_pp());

		err = npu_connect_ip(ip_idx);
		if (err)
			return err;

		addr = parse_uint(argv[2]);
		npu_read_cfg(addr, &buf, 1, ip_idx);
		vart_ml_log(LOG_INFO, "0x%08x\n", buf);
		break;
	case 4:
		ip_idx = parse_uint(argv[1]);
		if (ip_idx >= npu_get_nb_ip() + npu_get_nb_pp())
			return vart_ml_log_err_msg(vart_ml_error::TOOLS_BAD_USAGE,
			                           "IP index is too high, there is only %u IPs available\n",
			                           npu_get_nb_ip() + npu_get_nb_pp());

		err = npu_connect_ip(ip_idx);
		if (err)
			return err;

		addr = parse_uint(argv[2]);
		buf  = parse_uint(argv[3]);
		npu_write_cfg(addr, &buf, 1, ip_idx);
		break;
	default:
		return vart_ml_log_err_msg(vart_ml_error::TOOLS_BAD_USAGE,
		                           "Usage: vart_ml_tools %s [ip_idx] <address> [word to write].\n",
		                           argv[0]);
	}

	return vart_ml_error::SUCCESS;
}

int timestamp(int argc, const char** argv)
{
	uint32_t timestamp;
	uint32_t ip_idx;
	int      err;

	switch (argc)
	{
	case 1:
		for (size_t i = 0; i < npu_get_nb_ip(); i++)
		{
			err = npu_connect_ip(i);
			if (err)
				return err;

			npu_read_cfg(TIMESTAMP_OFFSET, &timestamp, 1, i);

			vart_ml_log(LOG_INFO, "Timestamp");

			if (npu_get_nb_ip() != 1)
				vart_ml_log(LOG_INFO, " IP %zu", i);

			vart_ml_log(LOG_INFO, ": 0x%08x\n", timestamp);
		}
		break;
	case 2:
		ip_idx = parse_uint(argv[1]);
		if (ip_idx >= npu_get_nb_ip() + npu_get_nb_pp())
			return vart_ml_log_err_msg(vart_ml_error::TOOLS_BAD_USAGE,
			                           "IP index is too high, there is only %u IPs available\n",
			                           npu_get_nb_ip() + npu_get_nb_pp());

		err = npu_connect_ip(ip_idx);
		if (err)
			return err;

		npu_read_cfg(TIMESTAMP_OFFSET, &timestamp, 1, ip_idx);

		vart_ml_log(LOG_INFO, "Timestamp");

		if (npu_get_nb_ip() != 1)
			vart_ml_log(LOG_INFO, " IP %u", ip_idx);

		vart_ml_log(LOG_INFO, ": 0x%08x\n", timestamp);

		break;
	default:
		return vart_ml_log_err_msg(
		    vart_ml_error::TOOLS_BAD_USAGE, "usage: vart_ml_tools %s [ip_idx].\n", argv[0]);
		break;
	}

	return vart_ml_error::SUCCESS;
}

static int valToTemperature(uint32_t value_, FpgaFamily fpgaFamily_, float* temp_)
{
	*temp_ = 0.;

	if (fpgaFamily_ == ZYNQ)
	{
		*temp_ = (((float)value_ * 509.3140064) / 0x10000) - 280.23087870;
	}
	else if (fpgaFamily_ == VERSAL) // Temperature is stored in Q8.7 (signed) format
	{
		*temp_ = ((value_ & 0x4000) == 0) ? ((float)value_ / 128) : -1 * ((float)(~(value_) + 1) / 128);
	}
	else
		return vart_ml_log_err_msg(
		    vart_ml_error::CONFIG_UNEXPECTED_FPGA_FAMILY, "Invalid FPGA family (%d).\n", fpgaFamily_);

	return vart_ml_error::SUCCESS;
}

static int TemperatureToVal(float temp_, FpgaFamily fpgaFamily_, uint32_t* value_)
{
	*value_ = 0;

	if (fpgaFamily_ == ZYNQ)
	{
		*value_ = (uint32_t)((temp_ + 280.23087870) * 0x10000 / 509.3140064);
	}
	else if (fpgaFamily_ == VERSAL) // Temperature is stored in Q8.7 (signed) format
	{
		*value_ = (temp_ < 0) ? ~((uint32_t)(temp_ * -128)) + 1 : (uint32_t)(temp_ * 128);
	}
	else
		return vart_ml_log_err_msg(
		    vart_ml_error::CONFIG_UNEXPECTED_FPGA_FAMILY, "Invalid FPGA family (%d).", fpgaFamily_);

	return vart_ml_error::SUCCESS;
}

static int getTemperature(FpgaFamily fpgaFamily_, float* temp_)
{
	uint32_t buf;

	if (fpgaFamily_ == ZYNQ)
	{
		npu_read_cfg(TEMP_OFFSET, &buf, 1, 0);
	}
	else if (fpgaFamily_ == VERSAL)
	{
		npu_read_noc(NOC_TEMP_OFFSET, &buf, 1);
	}
	else
		return vart_ml_log_err_msg(
		    vart_ml_error::CONFIG_UNEXPECTED_FPGA_FAMILY, "Invalid FPGA family (%d).", fpgaFamily_);

	return valToTemperature(buf, fpgaFamily_, temp_);
}

static int getMaxTemperature(FpgaFamily fpgaFamily_, float* temp_)
{
	uint32_t buf;

	if (fpgaFamily_ == ZYNQ)
	{
		npu_read_cfg(MAX_TEMP_OFFSET, &buf, 1, 0);
	}
	else if (fpgaFamily_ == VERSAL)
	{
		npu_read_noc(NOC_MAX_TEMP_OFFSET, &buf, 1);
	}
	else
		return vart_ml_log_err_msg(
		    vart_ml_error::CONFIG_UNEXPECTED_FPGA_FAMILY, "Invalid FPGA family (%d).", fpgaFamily_);

	return valToTemperature(buf, fpgaFamily_, temp_);
}

static int getOverTempValue(FpgaFamily fpgaFamily_, float* temp_)
{
	uint32_t buf;

	if (fpgaFamily_ == ZYNQ)
	{
		npu_read_cfg(OVER_TEMP_VALUE_OFFSET, &buf, 1, 0);
	}
	else if (fpgaFamily_ == VERSAL)
	{
		npu_read_noc(NOC_OVER_TEMP_VALUE_OFFSET, &buf, 1);
	}
	else
		return vart_ml_log_err_msg(
		    vart_ml_error::CONFIG_UNEXPECTED_FPGA_FAMILY, "Invalid FPGA family (%d).", fpgaFamily_);

	return valToTemperature(buf, fpgaFamily_, temp_);
}

static int setOverTempValue(float overtemp_, FpgaFamily fpgaFamily_)
{
	uint32_t buf;

	int err = TemperatureToVal(overtemp_, fpgaFamily_, &buf);
	if (err)
		return err;

	if (fpgaFamily_ == ZYNQ)
	{
		npu_write_cfg(OVER_TEMP_VALUE_OFFSET, &buf, 1, 0);
	}
	else if (fpgaFamily_ == VERSAL)
	{
		npu_write_noc(NOC_OVER_TEMP_VALUE_OFFSET, &buf, 1);
	}
	else
		return vart_ml_log_err_msg(
		    vart_ml_error::CONFIG_UNEXPECTED_FPGA_FAMILY, "Invalid FPGA family (%d).", fpgaFamily_);

	return vart_ml_error::SUCCESS;
}

static int isOverTempDetected(FpgaFamily fpgaFamily_, bool* is_overtemp_)
{
	uint32_t buf;
	*is_overtemp_ = false;

	if (fpgaFamily_ == ZYNQ)
	{
		npu_read_cfg(OVER_TEMP_FLAG_OFFSET, &buf, 1, 0);
		if ((buf & OVER_TEMP_MASK) != 0)
			*is_overtemp_ = true;
	}
	else if (fpgaFamily_ == VERSAL)
	{
		// TODO: add OT flag access for Versal
	}
	else
		return vart_ml_log_err_msg(
		    vart_ml_error::CONFIG_UNEXPECTED_FPGA_FAMILY, "Invalid FPGA family (%d).", fpgaFamily_);

	return vart_ml_error::SUCCESS;
}

int temperature(int argc, const char** argv)
{
	unsigned int refreshInterval;
	int          timeout;

	// Check if the argument are correct
	if (argc > 3)
		return vart_ml_log_err_msg(
		    vart_ml_error::TOOLS_BAD_USAGE, "usage: vart_ml_tools %s [time interval] [timeout].\n", argv[0]);

	else if (argc == 1)
	{
		refreshInterval = 1;
		timeout         = NO_TIMEOUT;
	}
	else if (argc == 2)
	{
		refreshInterval = atoi(argv[1]);
		if (refreshInterval < 1 || refreshInterval > 120)
			return vart_ml_log_err_msg(
			    vart_ml_error::TOOLS_BAD_ARG,
			    "Bad argument, the refresh interval should be between 1 and 120 seconds.\n");

		timeout = NO_TIMEOUT;
	}
	else
	{
		refreshInterval = atoi(argv[1]);
		if (refreshInterval < 1 || refreshInterval > 120)
			return vart_ml_log_err_msg(
			    vart_ml_error::TOOLS_BAD_ARG,
			    "Bad argument, the refresh interval should be between 1 and 120 seconds.\n");

		timeout = atoi(argv[2]);
	}

	int err = npu_connect_peripherals();
	if (err)
		return err;

	err = npu_connect_ip(0);
	if (err)
		return err;

	enum FpgaFamily fpgaFamily;
	err = npu_get_fpgafamily(&fpgaFamily);
	if (err)
		return err;

	if (fpgaFamily == VERSAL)
	{
		err = npu_connect_peripherals();
		if (err)
			return err;
	}

	if ((fpgaFamily == ZYNQ) && (!is_tempmonitor_en(0)))
		return vart_ml_log_err_msg(vart_ml_error::DEVICE_TEMP_READ_FAILURE,
		                           "Temperature monitoring is not enable on this board.\n");

	// Check if the board is overheating
	bool is_overtemp;
	err = isOverTempDetected(fpgaFamily, &is_overtemp);
	if (err)
		return err;

	if (is_overtemp)
		return vart_ml_log_err_msg(
		    vart_ml_error::DEVICE_TEMP_READ_FAILURE,
		    "Cannot read temperature, maybe on overtemp interruption has been raised.\n");

	if (timeout == NO_TIMEOUT)
		vart_ml_log(LOG_INFO,
		            "Monitoring FPGA temperatures in Celsius degree with a refresh interval of %u second(s), "
		            "forever loop (hit CTRL-C to interrupt).\n",
		            refreshInterval);
	else
		vart_ml_log(LOG_INFO,
		            "Monitoring FPGA temperatures in Celsius degree with a refresh interval of %u second(s) "
		            "for %d second(s).\n",
		            refreshInterval,
		            timeout);

	// Initialize the session maximum temperature
	float currTemp = 0;
	err            = getTemperature(fpgaFamily, &currTemp);
	if (err)
		return err;

	float maxTemp = currTemp;

	// Initialize start time
	time_t    startTime = time(nullptr);
	struct tm tm_info;
	localtime_r(&startTime, &tm_info);

	// Initialize currentTime time
	time_t currentTime = startTime;

	while ((timeout == NO_TIMEOUT) || (currentTime - startTime <= timeout))
	{
		// Check if the board is overheating
		err = isOverTempDetected(fpgaFamily, &is_overtemp);
		if (err)
			return err;

		if (is_overtemp)
			return vart_ml_log_err_msg(
			    vart_ml_error::DEVICE_TEMP_READ_FAILURE,
			    "Cannot read temperature, maybe on overtemp interruption has been raised.\n");

		// Convert current time to be printed
		char date[10] = {};
		sprintf(date, "%02d:%02d:%02d", tm_info.tm_hour, tm_info.tm_min, tm_info.tm_sec);

		err = getTemperature(fpgaFamily, &currTemp);
		if (err)
			return err;

		if (currTemp > maxTemp)
		{
			maxTemp = currTemp;
		}

		float absMaxTemp = 0.;
		err              = getMaxTemperature(fpgaFamily, &absMaxTemp);
		if (err)
			return err;

		float overTemp = 0.;
		err            = getOverTempValue(fpgaFamily, &overTemp);
		if (err)
			return err;

		// Print the temperature
		vart_ml_log(LOG_INFO,
		            "Board 0: at %s temperature (Celsius degrees): current [ %.1f ] / max on this session [ "
		            "%.1f ] / max since FPGA loading [ %.1f ] / max allowed [ %.1f ]\n",
		            date,
		            currTemp,
		            maxTemp,
		            absMaxTemp,
		            overTemp);

		if ((timeout != NO_TIMEOUT) && (currentTime + refreshInterval - startTime > timeout))
		{
			sleep(startTime + timeout - currentTime);
			break;
		}

		sleep(refreshInterval);

		// Get the current time
		currentTime = time(nullptr);
		localtime_r(&currentTime, &tm_info);
	}

	return vart_ml_error::SUCCESS;
}

int overtemp(int argc, const char** argv)
{
	if (argc != 1 && argc != 2)
		return vart_ml_log_err_msg(
		    vart_ml_error::TOOLS_BAD_USAGE, "usage: vart_ml_tools %s [new overtemp value].\n", argv[0]);

	int err = npu_connect_peripherals();
	if (err)
		return err;

	err = npu_connect_ip(0);
	if (err)
		return err;

	enum FpgaFamily fpgaFamily;
	err = npu_get_fpgafamily(&fpgaFamily);
	if (err)
		return err;

	if (fpgaFamily == VERSAL)
	{
		err = npu_connect_peripherals();
		if (err)
			return err;
	}

	if ((fpgaFamily == ZYNQ) && (!is_tempmonitor_en(0)))
		return vart_ml_log_err_msg(vart_ml_error::DEVICE_TEMP_READ_FAILURE,
		                           "Temperature monitoring is not enable on this board.\n");

	if (argc == 2)
	{
		float overtemp = atof(argv[1]);
		if (overtemp < 1 || overtemp > 125)
			return vart_ml_log_err_msg(
			    vart_ml_error::TOOLS_BAD_ARG,
			    "Bad argument, the overtemp should be between 1 and 125 Celsius degrees.\n");

		vart_ml_log(LOG_INFO,
		            "Setting the maximum allowed temperature which can be raised by the FPGA in Celsius "
		            "degrees to %.1f\n",
		            overtemp);

		err = setOverTempValue(overtemp, fpgaFamily);
		if (err)
			return err;
	}
	else
	{
		float overtemp = 0.;
		err            = getOverTempValue(fpgaFamily, &overtemp);
		if (err)
			return err;

		vart_ml_log(
		    LOG_INFO,
		    "Display the maximum allowed temperature which can be raised by the FPGA in Celsius degrees.\n");

		// Print the maximum allowed temperature
		vart_ml_log(LOG_INFO, "Board 0 maximum allowed temperature %.1f C\n", overtemp);
	}

	return vart_ml_error::SUCCESS;
}

static int check_ip_cores(npu_snapshot_t* snap)
{
	int err = vart_ml_error::SUCCESS;

	if (snap->ip_idx >= npu_get_nb_ip())
		return vart_ml_log_err_msg(vart_ml_error::TOOLS_BAD_USAGE,
		                           "IP index is too high, there is only %u IPs available\n",
		                           npu_get_nb_ip());

	err = npu_connect_ip(snap->ip_idx);
	if (err)
		return err;

	err = npu_get_architecture(&snap->arch);
	if (err)
		return err;

	for (size_t s = 0; s < npu_get_nbsystems(snap->ip_idx); s++)
		for (size_t c = 0; c < npu_get_nbcores(snap->ip_idx); c++)
			snap->active_cores.push_back({ s, c });

	err = npu_check_cores(snap);

	if (npu_get_nb_ip() == 1)
		vart_ml_log(LOG_INFO, "Cores status : ");
	else
		vart_ml_log(LOG_INFO, "Cores status for IP %zu : ", snap->ip_idx);

	if (err == vart_ml_error::SUCCESS)
		vart_ml_log(LOG_INFO, "OK\n");
	else if (err == vart_ml_error::DEVICE_BAD_CORE_STATUS)
		vart_ml_log(LOG_INFO, "IDLE\n");

	return err;
}

int check_cores(int argc, const char** argv)
{
	int err = npu_connect_peripherals();
	if (err)
		return err;

	npu_snapshot_t* snap = new npu_snapshot_t;

	switch (argc)
	{
	case 1:
		for (uint32_t ip_idx = 0; ip_idx < npu_get_nb_ip(); ip_idx++)
		{
			snap->ip_idx = ip_idx;
			err          = check_ip_cores(snap);
		}
		break;
	case 2:
		snap->ip_idx = parse_uint(argv[1]);
		err          = check_ip_cores(snap);
		break;
	default:
		return vart_ml_log_err_msg(
		    vart_ml_error::TOOLS_BAD_USAGE, "usage: vart_ml_tools %s [ip_idx].\n", argv[0]);
		break;
	}

	delete snap;

	return err;
}

int noc_poke(int argc, const char** argv)
{
	if (argc != 2 && argc != 3)
		return vart_ml_log_err_msg(
		    vart_ml_error::TOOLS_BAD_USAGE, "usage: vart_ml_tools %s <address> [word to write].\n", argv[0]);

	int err = npu_connect_peripherals();
	if (err)
		return err;

	err = npu_connect_ip(0);
	if (err)
		return err;

	enum FpgaFamily fpgaFamily;
	err = npu_get_fpgafamily(&fpgaFamily);
	if (err)
		return err;

	if (fpgaFamily != VERSAL)
		return vart_ml_log_err_msg(vart_ml_error::DEVICE_UNSUPPORTED_FEATURE,
		                           "NOC access is not available on non VERSAL board.\n");

	uint32_t buf;
	char*    endptr;
	uint64_t addr = (uint64_t)parse_uint_c(argv[1], &endptr);
	size_t   size = npu_get_noclen();

	if (endptr[0] != '\0')
		return vart_ml_log_err_msg(vart_ml_error::DEVICE_BAD_ARG, "Cannot parse address %s.\n", argv[1]);
	else if (addr < NOC_PMC_BASE_ADDR || NOC_PMC_BASE_ADDR + size <= addr)
		return vart_ml_log_err_msg(vart_ml_error::DEVICE_BAD_ARG,
		                           "Cannot access NOC at this address 0x%llx\n"
		                           "MMAP NOC base addr is 0x%x and its length is 0x%zx.\n",
		                           addr,
		                           NOC_PMC_BASE_ADDR,
		                           size);

	if (argc == 3)
	{
		uint32_t data = (uint32_t)parse_uint_c(argv[2], &endptr);
		if (endptr[0] != '\0')
			return vart_ml_log_err_msg(vart_ml_error::DEVICE_BAD_ARG, "Cannot parse data %s.\n", argv[2]);

		npu_write_noc(addr - NOC_PMC_BASE_ADDR, &data, 1);

		/* Check if the value is correctly written */
		npu_read_noc(addr - NOC_PMC_BASE_ADDR, &buf, 1);
		if (buf != data)
			return vart_ml_log_err_msg(vart_ml_error::DEVICE_NOC_RW_FAILURE,
			                           "Data was not written correctly: %x instead of %x.\n",
			                           buf,
			                           data);
	}
	else
		npu_read_noc(addr - NOC_PMC_BASE_ADDR, &buf, 1);

	vart_ml_log(LOG_INFO, "Reading from NOC at address 0x%llx = 0x%x\n", addr, buf);

	return vart_ml_error::SUCCESS;
}

int reset_mutex(int argc, const char** argv)
{
	uint32_t ip_idx;
	int      err;

	switch (argc)
	{
	case 1:
		for (size_t i = 0; i < npu_get_nb_ip(); i++)
		{
			err = npu_connect_ip(i);
			if (err)
				return err;

			npu_release_mutex(VERSAL_MCTRL_NPU_INFERENCE_MUTEX_OFFSET, i);
		}
		break;
	case 2:
		ip_idx = parse_uint(argv[1]);
		if (ip_idx >= npu_get_nb_ip())
			return vart_ml_log_err_msg(vart_ml_error::TOOLS_BAD_USAGE,
			                           "IP index is too high, there is only %u IPs available\n",
			                           npu_get_nb_ip());

		err = npu_connect_ip(ip_idx);
		if (err)
			return err;

		npu_release_mutex(VERSAL_MCTRL_NPU_INFERENCE_MUTEX_OFFSET, ip_idx);
		break;
	default:
		return vart_ml_log_err_msg(
		    vart_ml_error::TOOLS_BAD_USAGE, "usage: vart_ml_tools %s [ip_idx].\n", argv[0]);
		break;
	}

	return vart_ml_error::SUCCESS;
}
