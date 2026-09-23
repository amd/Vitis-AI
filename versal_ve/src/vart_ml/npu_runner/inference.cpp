/**
 * @file inference.cpp
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
 *
 */

#include <ctime>
#include <sstream>
#include <stdexcept>
#include <sys/time.h>

#include "io/io.h"
#include "npu_runner.h"
#include "utils/log.h"
#include "utils/npu_reg.h"
#include "utils/vcd_stats.h"

// Number of seconds to wait for inference to finish
#define STATUS_POLL_TIMEOUT 1

#define LICENSE_DATA_OFF   0x100
#define LICENSE_DATE_OFF   0x118
#define LICENSE_STATUS_OFF 0x128
#define LICENSE_LEN        5

// the code about the license will be removed in a future version
static int activate_license(const npu_snapshot_t* snap)
{
	uint32_t x                         = 0;
	uint32_t license_data[LICENSE_LEN] = { 0x2FBF5528, 0xC5D8B000, 0x681DD2C9, 0xB65C08FF, 0x657F00C7 };
	npu_write_cfg(LICENSE_DATA_OFF, license_data, LICENSE_LEN, snap->ip_idx);
	npu_write_cfg(LICENSE_DATE_OFF, &x, 1, snap->ip_idx);

#ifndef DEBUG
	time_t start_time = time(NULL);
#endif
	do
	{
		npu_read_cfg(LICENSE_STATUS_OFF, &x, 1, snap->ip_idx);
#ifndef DEBUG
		if (time(NULL) > start_time + STATUS_POLL_TIMEOUT)
			return vart_ml_log_err_msg(vart_ml_error::LICENSE_INVALID,
			                           "ERROR invalid licence. Please, verify the license data.\n");
#endif
	} while (x != 0);

	return vart_ml_error::SUCCESS;
}

static void npu_load_configs(const npu_snapshot_t* snap)
{
	for (auto& p : snap->ctrl_reg_config)
		npu_write_cfg(p.first, p.second.data(), p.second.size(), snap->ip_idx);
}

static void npu_set_nbuff_addr_us(const npu_snapshot_t* snap, const std::vector<uint64_t>& nbuff)
{
	// NBUFF: write config area phy addr - 28 bits
	uint32_t config_msb = static_cast<uint32_t>(snap->config_addr[0].nbuff_conf_val);

	npu_write_cfg(NBUFF_CONFIG_AREA_OFFSET_USCALE, (const uint32_t*)&config_msb, 1, snap->ip_idx);

	uint32_t temp_msb = static_cast<uint32_t>(snap->tmp_area_addr[0].nbuff_conf_val);
	npu_write_cfg(NBUFF_TEMP_AREA_OFFSET_USCALE, (const uint32_t*)&temp_msb, 1, snap->ip_idx);

	std::vector<uint32_t> nbuff_32b(nbuff.size());
	for (size_t i = 0; i < nbuff.size(); i++)
		nbuff_32b[i] = static_cast<uint32_t>(nbuff[i]);

	npu_write_cfg(
	    NBUFF_BASE_OFFSET_USCALE, (const uint32_t*)(nbuff_32b.data()), nbuff_32b.size(), snap->ip_idx);
}

static void npu_clean_nbuff_us(const npu_snapshot_t* snap)
{
	supervisor_reg sup_reg;
	npu_read_cfg(SUPERVISOR_REG_SYS_OFFSET, &sup_reg.reg, 1, snap->ip_idx);

	sup_reg.bf.use_nbuf = 0;
	npu_write_cfg(SUPERVISOR_REG_SYS_OFFSET, (const uint32_t*)&sup_reg, 1, snap->ip_idx);
}

int npu_start_inference(const npu_snapshot_t* snap, struct vcd_context& vcd_context)
{
	int err;

	vcd_event(vcd_context, NPU_INFERENCE_MCTRL_CONFIG, 1);
	npu_load_configs(snap);
	vcd_event(vcd_context, NPU_INFERENCE_MCTRL_CONFIG, 0);

	if (is_license_en(snap->ip_idx))
	{
		err = activate_license(snap);
		if (err)
			return err;
	}

	vcd_event(vcd_context, NPU_INFERENCE_CORE_CONFIG, 1);
	for (auto& [s, c] : snap->active_cores)
	{
		if (snap->arch == FpgaArchitecture::V2)
		{
			/* V2 STATUS is a 64-bit register; hardware requires a 2-word write.
			 * Read-modify-write to preserve RO fields in the upper word. */
			lm_coreBus_masterControl_status_versal status{};
			npu_read_cfg(STATUS_OFFSET(s, c), reinterpret_cast<uint32_t*>(&status.reg), 2, snap->ip_idx);
			status.bf.system_en = 1;

			npu_write_cfg(STATUS_OFFSET(s, c), reinterpret_cast<uint32_t*>(&status.reg), 2, snap->ip_idx);
		}
		else
		{
			uint32_t x = 1;
			npu_write_cfg(STATUS_OFFSET(s, c), &x, 1, snap->ip_idx);
		}
	}
	vcd_event(vcd_context, NPU_INFERENCE_CORE_CONFIG, 0);

	vcd_event(vcd_context, NPU_INFERENCE_IRQ_CONFIG, 1);

	uint64_t interrupt_offset =
	    (snap->arch == FpgaArchitecture::V2) ? NPU_INTERRUPT_CTRL_V2 : NPU_INTERRUPT_CTRL;
	uint32_t interrupt_mask =
	    (snap->arch == FpgaArchitecture::V2) ? NPU_INTERRUPT_MASK_V2 : NPU_INTERRUPT_MASK;
	uint32_t interrupt_ctrl = 0;
	npu_read_cfg(interrupt_offset, &interrupt_ctrl, 1, snap->ip_idx);

	if (snap->interrupt_en)
	{
		/* Enable interrupt after initiating the inference,
		 * otherwise we will get an extra and unwanted interrupt and false result
		 * at the start.
		 */
		if (!(interrupt_ctrl & interrupt_mask))
		{
			interrupt_ctrl |= interrupt_mask;
			npu_write_cfg(interrupt_offset, &interrupt_ctrl, 1, snap->ip_idx);
		}
	}
	else if (interrupt_ctrl & interrupt_mask)
	{
		/* Clear any leftover interrupt enable from a prior XRT session
		 * to prevent an unhandled interrupt from hanging the bus. */
		interrupt_ctrl &= ~interrupt_mask;
		npu_write_cfg(interrupt_offset, &interrupt_ctrl, 1, snap->ip_idx);
	}

	vcd_event(vcd_context, NPU_INFERENCE_IRQ_CONFIG, 0);

	return vart_ml_error::SUCCESS;
}

static lm_coreBus_masterControl_status
npu_get_status(FpgaArchitecture arch, size_t sysindex, size_t coreindex, uint32_t ip_idx)
{
	lm_coreBus_masterControl_status status;
	if (arch == FpgaArchitecture::V1)
		status = lm_coreBus_masterControl_status_uscale{};
	else if (arch == FpgaArchitecture::V2)
		status = lm_coreBus_masterControl_status_versal{};
	else
		status = lm_coreBus_masterControl_status_aieml_v1c{};
	std::visit(
	    [&sysindex, &coreindex, &ip_idx](auto& x) {
		    npu_read_cfg(STATUS_OFFSET(sysindex, coreindex),
		                 reinterpret_cast<uint32_t*>(&x.reg),
		                 sizeof(x) / sizeof(uint32_t),
		                 ip_idx);
	    },
	    status);
	return status;
}

#ifndef DEBUG
/* Read each core's status once, combining the done check and first-core progress
 * tracking that previously required two separate hardware reads per poll iteration. */
static int
npu_poll_inference_state(const npu_snapshot_t* snap, bool* is_done, uint32_t* sublayer, uint32_t* supra)
{
	*is_done = true;
	for (auto& [s, c] : snap->active_cores)
	{
		lm_coreBus_masterControl_status status = npu_get_status(snap->arch, s, c, snap->ip_idx);

		bool core_done = false;
		std::visit(
		    [&core_done, sublayer, supra, snap](const auto& x) {
			    using T = std::decay_t<decltype(x)>;
			    if constexpr (std::is_same_v<lm_coreBus_masterControl_status_uscale, T>)
			    {
				    core_done = x.bf.idle && x.bf.subLayer_count == snap->sublayer_count
				                && x.bf.index_master_control_network == snap->index_mctrl_network;
				    *sublayer = x.bf.subLayer_count;
				    *supra    = x.bf.index_master_control_network;
			    }
			    else if constexpr (std::is_same_v<lm_coreBus_masterControl_status_versal, T>)
			    {
				    uint32_t nbSupra = static_cast<uint32_t>(x.bf.supra_mem_idx_lsb)
				                       + (static_cast<uint32_t>(x.bf.supra_mem_idx_msb) << 6);
				    core_done = x.bf.system_idle && nbSupra == snap->index_mctrl_network - 1
				                && x.bf.sublayer_count_run == snap->sublayer_count;
				    *sublayer = x.bf.sublayer_count_run;
				    *supra    = nbSupra;
			    }
			    else if constexpr (std::is_same_v<lm_coreBus_masterControl_status_aieml_v1c, T>)
			    {
				    core_done = x.bf.system_run == 0 && x.bf.supra_count == snap->nbSupra;
				    *supra    = x.bf.supra_count;
			    }
			    else
				    return vart_ml_log_err(vart_ml_error::DEVICE_UNSUPPORTED_ARCH);
		    },
		    status);

		*is_done &= core_done;

		// Return the state of the first unfinished core
		if (!*is_done)
			return vart_ml_error::SUCCESS;
	}

	return vart_ml_error::SUCCESS;
}

static uint64_t timeus(void)
{
	struct timeval t;
	gettimeofday(&t, NULL);
	return ((int64_t)t.tv_sec) * 1000000 + ((int64_t)t.tv_usec);
}
#endif

static int npu_is_inference_done(const npu_snapshot_t* snap, size_t sysindex, size_t coreindex, bool* retval)
{
	FpgaArchitecture arch = snap->arch;

	lm_coreBus_masterControl_status status = npu_get_status(arch, sysindex, coreindex, snap->ip_idx);
	std::visit(
	    [retval, snap](const auto& x) {
		    using T = std::decay_t<decltype(x)>;
		    if constexpr (std::is_same_v<lm_coreBus_masterControl_status_uscale, T>)
		    {
			    *retval = x.bf.idle && x.bf.subLayer_count == snap->sublayer_count
			              && x.bf.index_master_control_network == snap->index_mctrl_network;
		    }
		    else if constexpr (std::is_same_v<lm_coreBus_masterControl_status_versal, T>)
		    {
			    uint32_t nbSupra = static_cast<uint32_t>(x.bf.supra_mem_idx_lsb)
			                       + (static_cast<uint32_t>(x.bf.supra_mem_idx_msb) << 6);
			    *retval = x.bf.system_idle && nbSupra == snap->index_mctrl_network - 1
			              && x.bf.sublayer_count_run == snap->sublayer_count;
		    }
		    else if constexpr (std::is_same_v<lm_coreBus_masterControl_status_aieml_v1c, T>)
			    *retval = x.bf.system_run == 0 && x.bf.supra_count == snap->nbSupra;
		    else
			    return vart_ml_log_err(vart_ml_error::DEVICE_UNSUPPORTED_ARCH);
	    },
	    status);

	return vart_ml_error::SUCCESS;
}

int npu_is_inference_done(const npu_snapshot_t* snap, bool* retval)
{
	for (auto& [s, c] : snap->active_cores)
	{
		int err = npu_is_inference_done(snap, s, c, retval);
		if (err)
			return err;

		if (!*retval)
			return vart_ml_error::SUCCESS;
	}

	return vart_ml_error::SUCCESS;
}

int npu_wait_for_inference(const npu_snapshot* snap)
{
	int err;

	if (snap->interrupt_en)
	{
		err = npu_wait_interrupt(snap->ip_idx);
		if (err)
		{
			err = vart_ml_log_err_msg(vart_ml_error::DEVICE_INFERENCE_TIMEOUT,
			                          "Interrupt timed out while waiting for inference to finish\n");

			vart_ml_log(LOG_ERR, "Checking snapshot configuration...\n");
			((npu_snapshot*)snap)->checkConfig = true;
			if (!npu_run_snapshot((npu_snapshot_t*)snap))
				vart_ml_log(LOG_ERR, "Snapshot configuration OK\n");
			else
				vart_ml_log(LOG_ERR, "Snapshot configuration CORRUPTED\n");

			return err;
		}
	}

	else
	{
#ifndef DEBUG
		uint64_t timeout    = (snap->arch == FpgaArchitecture::AIEML_V1C) ? 5000000 * STATUS_POLL_TIMEOUT
		                                                                  : 1000000 * STATUS_POLL_TIMEOUT;
		bool     is_done    = false;
		uint64_t start_time = timeus();
		uint32_t sublayerPrev, supraPrev;
		err = npu_poll_inference_state(snap, &is_done, &sublayerPrev, &supraPrev);
		if (err)
			return err;
#else
		bool is_done = false;
#endif
		do
		{
#ifndef DEBUG
			uint32_t sublayer, supra;
			err = npu_poll_inference_state(snap, &is_done, &sublayer, &supra);
			if (err)
				return err;

			if (sublayer != sublayerPrev || supra != supraPrev)
			{
				sublayerPrev = sublayer;
				supraPrev    = supra;
				start_time   = timeus();
			}
			if (timeus() > start_time + timeout)
			{
				std::ostringstream oss;
				for (auto& [s, c] : snap->active_cores)
				{
					lm_coreBus_masterControl_status status = npu_get_status(snap->arch, s, c, snap->ip_idx);
					size_t                          si = s, ci = c;
					std::visit(
					    [&oss, &si, &ci](const auto& x) {
						    oss << "Core (" << si << "," << ci << ") : 0x" << std::hex << x.reg << std::dec
						        << "\n";
					    },
					    status);
				}

				npu_check_asserts(snap->ip_idx);
				err = vart_ml_log_err_msg(vart_ml_error::DEVICE_INFERENCE_TIMEOUT, oss.str().c_str());

				vart_ml_log(LOG_ERR, "Checking snapshot configuration...\n");
				((npu_snapshot*)snap)->checkConfig = true;
				if (!npu_run_snapshot((npu_snapshot_t*)snap))
					vart_ml_log(LOG_ERR, "Snapshot configuration OK\n");
				else
					vart_ml_log(LOG_ERR, "Snapshot configuration CORRUPTED\n");

				return err;
			}
#else
			err = npu_is_inference_done(snap, &is_done);
			if (err)
				return err;
#endif

		} while (!is_done);
	}

	if (snap->fpgaFamily != FpgaFamily::VERSAL)
	{
		/* Do not remove these writes, they are needed */
		lm_coreBus_masterControl_status_uscale x;
		x.reg = 0;
		for (auto& [s, c] : snap->active_cores)
		{
			npu_write_cfg(STATUS_OFFSET(s, c), &x.reg, 1, snap->ip_idx);
			npu_write_cfg(ENABLE_OFFSET(s, c), &x.reg, 1, snap->ip_idx);
		}

		// Remove nbuff enable flag
		npu_clean_nbuff_us(snap);
	}

	return vart_ml_error::SUCCESS;
}

bool npu_check_asserts(uint32_t ip_idx)
{
	if (!is_assert_en(ip_idx))
		return true;
	FpgaArchitecture arch;

	int err = npu_get_architecture(&arch);
	if (err)
		throw std::runtime_error(vart_ml_error::exception_message(err));

	uint32_t buf[NB_ASSERT_REGS];
	uint32_t status;
	npu_read_cfg(
	    (arch == FpgaArchitecture::AIEML_V1C) ? ASSERTS_OFFSET_V1C : ASSERTS_OFFSET, &status, 1, ip_idx);
	if (status == 0)
		return true;

	npu_read_cfg((arch == FpgaArchitecture::AIEML_V1C) ? ASSERTS_BASE_OFFSET_V1C : ASSERTS_BASE_OFFSET,
	             buf,
	             NB_ASSERT_REGS,
	             ip_idx);
	int assertion_found = 0;
	for (size_t i = 0; i < NB_ASSERT_REGS; i++)
	{
		if (buf[i] == 0)
			continue;

		for (size_t j = 0; j < 32; j++)
		{
			if (buf[i] & (1u << (31 - j)))
			{
				vart_ml_log(LOG_ERR, "Assertion %zu raised\n", 32 * i + j);
				assertion_found = 1;
			}
		}
	}

	if (!assertion_found)
		vart_ml_log(LOG_ERR, "An unknown VART ML assertion was raised.\n");

	vart_ml_log(LOG_ERR, "Please reboot the board.\n");
	return false;
}

static int
npu_check_core(size_t sysindex, size_t coreindex, uint32_t ip_idx, FpgaArchitecture arch, bool* isOk)
{
	lm_coreBus_masterControl_status status = npu_get_status(arch, sysindex, coreindex, ip_idx);

	int err = std::visit(
	    [isOk, &sysindex, &coreindex, &ip_idx](const auto& x) {
		    using T      = std::decay_t<decltype(x)>;
		    T expected   = {};
		    expected.reg = 0;
		    npu_read_cfg(STATUS_OFFSET(sysindex, coreindex),
		                 reinterpret_cast<uint32_t*>(&expected.reg),
		                 sizeof(T) / sizeof(uint32_t),
		                 ip_idx);

		    if constexpr (std::is_same_v<lm_coreBus_masterControl_status_uscale, T>)
			    expected.bf.idle = 1;
		    else if constexpr (std::is_same_v<lm_coreBus_masterControl_status_versal, T>)
			    expected.bf.system_idle = 1;
		    else if constexpr (std::is_same_v<lm_coreBus_masterControl_status_aieml_v1c, T>)
		    {
			    expected.bf.system_run   = 0;
			    expected.bf.system_error = 0;
		    }
		    else
			    return vart_ml_log_err(vart_ml_error::DEVICE_UNSUPPORTED_ARCH);

		    *isOk = expected.reg == x.reg;
		    return vart_ml_error::SUCCESS;
	    },
	    status);

	if (err)
		return err;

	if (!*isOk)
		vart_ml_log(LOG_ERR,
		            "Core %zu of system %zu of IP %u is unusable, reboot the board.\n",
		            coreindex,
		            sysindex,
		            ip_idx);

	return vart_ml_error::SUCCESS;
}

int npu_check_cores(const npu_snapshot_t* snap)
{
	uint32_t ip_idx = snap->ip_idx;
	bool     isOk   = true;

	npu_get_inference_mutex(ip_idx);

	for (auto& [s, c] : snap->active_cores)
	{
		bool res;
		int  err = npu_check_core(s, c, ip_idx, snap->arch, &res);
		if (err)
		{
			npu_release_inference_mutex(ip_idx);
			return err;
		}

		isOk &= res;
	}

	npu_release_inference_mutex(ip_idx);

	if (!isOk)
		return vart_ml_log_err(vart_ml_error::DEVICE_BAD_CORE_STATUS);

	return vart_ml_error::SUCCESS;
}

int
npu_set_nbuff_addr(const npu_snapshot_t* snap, const std::vector<std::vector<uint64_t>>& nbuff, size_t start)
{
	FpgaArchitecture arch = snap->arch;

	if (snap->debug_show_IO_address)
	{
		std::map<size_t, std::string> nbuf2text;

		if (arch == FpgaArchitecture::AIEML_V1C)
			for (size_t i = 0; i < snap->tmp_area_addr.size(); i++)
				nbuf2text[i] = "DDR_TMP[" + std::to_string(i) + "]";

		for (size_t i = 0; i < snap->inputs.size(); i++)
		{
			size_t iter = (arch == FpgaArchitecture::AIEML_V1C) ? snap->inputs[i].batchSize
			                                                    : npu_get_nbcores(snap->ip_idx);
			for (size_t b = 0; b < iter; b++)
				nbuf2text[snap->inputs[i].nbuf_idx[0][b]] =
				    " inputs[" + std::to_string(i) + "][" + std::to_string(b) + "] " + snap->inputs[i].name;
		}

		for (size_t i = 0; i < snap->constants.size(); i++)
			nbuf2text[snap->constants[i].nbuf_idx[0][0]] =
			    " consts[" + std::to_string(i) + "] " + snap->constants[i].name;

		for (size_t i = 0; i < snap->outputs.size(); i++)
		{
			size_t iter = (arch == FpgaArchitecture::AIEML_V1C) ? snap->outputs[i].batchSize
			                                                    : npu_get_nbcores(snap->ip_idx);
			for (size_t b = 0; b < iter; b++)
				nbuf2text[snap->outputs[i].nbuf_idx[0][b]] =
				    "outputs[" + std::to_string(i) + "][" + std::to_string(b) + "] " + snap->outputs[i].name;
		}

		for (size_t sys = 0; sys < nbuff.size(); sys++)
			for (size_t i = 0; i < nbuff[sys].size(); i++)
			{
				std::string info = nbuf2text.find(i) != nbuf2text.end() ? nbuf2text[i] : "unused";
				vart_ml_log(LOG_INFO,
				            "[VART] sys %2zu nbuf %2zu @ 0x%012llx # %s\n",
				            sys,
				            i,
				            (unsigned long long)nbuff[sys][i],
				            info.c_str());
			}
	}

	if (arch == FpgaArchitecture::V2)
	{
		for (size_t i = 0; i < npu_get_nbddrs(); i++)
		{
			// NBUFF: write config area phy addr - 28 bits
			uint32_t config_msb = static_cast<uint32_t>(snap->config_addr[i].nbuff_conf_val);
			npu_write_cfg(NBUFF_CONFIG_AREA_OFFSET_AIE1(i), (const uint32_t*)&config_msb, 1, snap->ip_idx);

			uint32_t temp_msb = static_cast<uint32_t>(snap->tmp_area_addr[i].nbuff_conf_val);
			npu_write_cfg(NBUFF_TEMP_AREA_OFFSET_AIE1(i), (const uint32_t*)&temp_msb, 1, snap->ip_idx);
		}

		std::vector<uint32_t> nbuff_32b;
		for (size_t sys = 0; sys < nbuff.size(); sys++)
		{
			nbuff_32b.clear();
			for (size_t i = 0; i < nbuff[sys].size(); i++)
				nbuff_32b.push_back(static_cast<uint32_t>(nbuff[sys][i]));

			npu_write_cfg(NBUFF_BASE_OFFSET_AIE1(sys),
			              (const uint32_t*)(nbuff_32b.data()),
			              nbuff_32b.size(),
			              snap->ip_idx);
		}
	}
	else if (arch == FpgaArchitecture::AIEML_V1C)
	{
		uint64_t nbuff_offset = NBUFF_BASE_OFFSET + start * sizeof(uint64_t);

		npu_write_cfg(nbuff_offset,
		              (const uint32_t*)nbuff[0].data(),
		              sizeof(uint64_t) * nbuff[0].size() / sizeof(uint32_t),
		              snap->ip_idx);

		uint32_t index = start;

		npu_write_cfg(NBUFF_INDEX, &index, 1, snap->ip_idx);
	}
	else // FpgaArchitecture::V1
		// We assume a single system architecture
		npu_set_nbuff_addr_us(snap, nbuff[0]);

	return vart_ml_error::SUCCESS;
}
