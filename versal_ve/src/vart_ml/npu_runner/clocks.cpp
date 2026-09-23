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
 *
 */

#include <algorithm>
#include <cmath>
#include <map>
#include <unistd.h>

#include "io/io.h"
#include "npu_runner.h"
#include "utils/fpga_info.h"
#include "utils/log.h"
#include "utils/npu_reg.h"

#define NB_SAMPLES 500

/**
 * @brief Structure of a snapshot
 *
 * This structure contains all the parameters needed to run a snapshot. They are retrieved by parsing the
 * snapshot files.
 *
 */
struct npu_clock_frequency
{
	float frequency10 = 1.0f;
	float frequency4x = 1.0f;
	float frequency2x = 1.0f;
	float frequency1x = 1.0f;
};

static std::map<size_t, struct npu_clock_frequency> clock_state;

struct mmcm_config
{
	uint32_t startConfig;
	uint32_t isConfigValid;

	uint32_t clkFbOut_mul;
	uint32_t clkFbOut_mulFrac;
	uint32_t clkFbOut_mulFracEna;

	uint32_t divClk_divide;

	uint32_t clkOut0_div;
	uint32_t clkOut0_divFrac;
	uint32_t clkOut0_divFracEna;

	uint32_t clk_ena;
};

int npu_set_clock_state(uint32_t ip_idx)
{
	const char* info;
	int         err;

	// Start clock if needed
	err = npu_start_clocks(ip_idx, (1 << npu_get_nbsystems(ip_idx)) - 1);
	if (err)
		return err;

	err = get_user_config("runSession.summary", &info);
	if (err)
		return err;

	if (info == NULL || std::string(info) == "none")
		return vart_ml_error::SUCCESS;

	err = npu_measure_freq(CLK_10M, ip_idx, 0, &clock_state[ip_idx].frequency10);
	if (err)
		return err;

	if (clock_state[ip_idx].frequency10 == 0.0f)
	{
		err = npu_stop_clocks(ip_idx);
		if (err)
			return err;

		return vart_ml_log_err_msg(vart_ml_error::DEVICE_CLK_FREQ_MEASURE_FAILURE,
		                           "clk_10m should not be zero.\n");
	}

	err = npu_measure_freq(CLK_4X, ip_idx, 0, &clock_state[ip_idx].frequency4x);
	if (err)
		return err;

	if (clock_state[ip_idx].frequency4x == 0.0f)
	{
		err = npu_stop_clocks(ip_idx);
		if (err)
			return err;

		return vart_ml_log_err_msg(vart_ml_error::DEVICE_CLK_FREQ_MEASURE_FAILURE,
		                           "clk_4x should not be zero.\n");
	}

	FpgaArchitecture arch;
	err = npu_get_architecture(&arch);
	if (err)
		return err;

	if (arch != FpgaArchitecture::AIEML_V1C)
	{
		err = npu_measure_freq(CLK_2X, ip_idx, 0, &clock_state[ip_idx].frequency2x);
		if (err)
			return err;

		if (clock_state[ip_idx].frequency2x == 0.0f)
		{
			err = npu_stop_clocks(ip_idx);
			if (err)
				return err;

			return vart_ml_log_err_msg(vart_ml_error::DEVICE_CLK_FREQ_MEASURE_FAILURE,
			                           "clk_2x should not be zero.\n");
		}

		err = npu_measure_freq(CLK_1X, ip_idx, 0, &clock_state[ip_idx].frequency1x);
		if (err)
			return err;

		if (clock_state[ip_idx].frequency1x == 0.0f)
		{
			err = npu_stop_clocks(ip_idx);
			if (err)
				return err;

			return vart_ml_log_err_msg(vart_ml_error::DEVICE_CLK_FREQ_MEASURE_FAILURE,
			                           "clk_1x should not be zero.\n");
		}
	}

	return vart_ml_error::SUCCESS;
}

int npu_start_clocks(uint32_t ip_idx, uint8_t systems)
{
	FpgaArchitecture arch;

	int err = npu_get_architecture(&arch);
	if (err)
		return err;

	if (arch == FpgaArchitecture::V1)
	{
		uint32_t x = systems;
		// Start clocks
		npu_write_cfg(MMCM_CLK_ENA, &x, 1, ip_idx);
	}

	return vart_ml_error::SUCCESS;
}

int npu_stop_clocks(uint32_t ip_idx)
{
	uint32_t   x = 0;
	FpgaFamily fpgaFamily;

	int err = npu_get_fpgafamily(&fpgaFamily);
	if (err)
		return err;

	if (fpgaFamily != FpgaFamily::VERSAL)
		npu_write_cfg(MMCM_CLK_ENA, &x, 1, ip_idx);

	return vart_ml_error::SUCCESS;
}

static timespec timespecfrom(double val) { return { (time_t)(val * 1e-6), (long)(val * 1e3) % (long)1e9 }; }

static int npu_compute_mmcm_cfg(const float target_freq, struct mmcm_config& config)
{
	FpgaFamily fpgaFamily;
	int        err = npu_get_fpgafamily(&fpgaFamily);
	if (err)
		return err;

	const float mult_step    = (fpgaFamily == FpgaFamily::VERSAL) ? 1 : 0.125;
	const float div_step     = (fpgaFamily == FpgaFamily::VERSAL) ? 1 : 0.125;
	const float vco_max      = (fpgaFamily == FpgaFamily::VERSAL) ? 4320 : 1600;
	const float vco_min      = (fpgaFamily == FpgaFamily::VERSAL) ? 2160 : 600;
	const float min_mult_ref = (fpgaFamily == FpgaFamily::VERSAL) ? 5 : 2;
	const float max_mult_ref = (fpgaFamily == FpgaFamily::VERSAL) ? 432 : 128;
	const float min_div      = (fpgaFamily == FpgaFamily::VERSAL) ? 1 : 2;
	const float max_div      = (fpgaFamily == FpgaFamily::VERSAL) ? 107 : 128;
	const float min_clkdiv   = (fpgaFamily == FpgaFamily::VERSAL) ? 3 : 1;
	const float max_clkdiv   = (fpgaFamily == FpgaFamily::VERSAL) ? 432 : 106;

	const unsigned int REFCLK_FREQ = npu_get_inputfreq(0);
	float              best_freq   = 0.0f;
	float              chos_mult   = 0.0f;
	float              chos_div    = 0.0f;
	float              chos_clkdiv = 0;

	for (uint32_t clkdiv = min_clkdiv; clkdiv <= max_clkdiv; clkdiv++)
	{
		float min_mult =
		    std::max(min_mult_ref, std::floor(vco_min / (static_cast<float>(REFCLK_FREQ) / clkdiv)));
		float max_mult =
		    std::min(max_mult_ref, std::floor(vco_max / (static_cast<float>(REFCLK_FREQ) / clkdiv)));

		for (float mult = min_mult; mult <= max_mult; mult += mult_step)
		{
			for (float div = min_div; div <= max_div; div = div + div_step)
			{
				float vco  = (static_cast<float>(REFCLK_FREQ) / clkdiv) * mult;
				float freq = vco / div;
				if ((freq <= target_freq) && (vco >= vco_min) && (vco <= vco_max))
				{
					if (freq == target_freq || (target_freq - freq) <= (target_freq - best_freq))
					{
						chos_mult   = mult;
						chos_div    = div;
						chos_clkdiv = clkdiv;
						best_freq   = freq;
						if (best_freq == target_freq)
							break;
					}
				}
			}
			if (best_freq == target_freq)
				break;
		}
	}

	float junk;
	config.clkFbOut_mul        = static_cast<uint32_t>(chos_mult);
	config.clkFbOut_mulFrac    = static_cast<uint32_t>(1000.0f * std::modf(chos_mult, &junk));
	config.clkFbOut_mulFracEna = (config.clkFbOut_mulFrac != 0);

	config.divClk_divide      = static_cast<uint32_t>(std::floor(chos_clkdiv));
	config.clkOut0_div        = static_cast<uint32_t>(chos_div);
	config.clkOut0_divFrac    = static_cast<uint32_t>(1000.0f * std::modf(chos_div, &junk));
	config.clkOut0_divFracEna = (config.clkOut0_divFrac != 0);

	return vart_ml_error::SUCCESS;
}

int npu_change_mmcm_freq(uint32_t ip_idx, const float target_freq)
{
	struct mmcm_config cfg = {};
	int                err;

	err = npu_compute_mmcm_cfg(target_freq, cfg);
	if (err)
		return err;

	FpgaFamily fpgaFamily;
	err = npu_get_fpgafamily(&fpgaFamily);
	if (err)
		return err;

	int keepgoing;
	if (fpgaFamily != FpgaFamily::VERSAL)
	{
		npu_write_cfg(MMCM_CFG_BASE, (const uint32_t*)&cfg, sizeof(cfg) / sizeof(uint32_t), ip_idx);

		uint32_t ena_state;
		npu_read_cfg(MMCM_CLK_ENA, &ena_state, 1, ip_idx);
		err = npu_stop_clocks(ip_idx);
		if (err)
			return err;

		/* Activate config */
		uint32_t x = 1;
		npu_write_cfg(MMCM_CFG_BASE, &x, 1, ip_idx);

		keepgoing = 50;
		do
		{
			if (keepgoing-- == 0)
				return vart_ml_log_err_msg(vart_ml_error::DEVICE_MMCM_CONFIG_TIMEOUT,
				                           "Can't configure the MMCM, timeout expired.\n");

			npu_read_cfg(MMCM_CFG_VALID, &x, 1, ip_idx);
			usleep(100e3);

		} while (!x);

		npu_write_cfg(MMCM_CLK_ENA, &ena_state, 1, ip_idx);
	}
	else
	{
		uint32_t data = 0x01;

		FpgaArchitecture arch;
		err = npu_get_architecture(&arch);
		if (err)
			return err;

		uint64_t cfg_base = (arch == FpgaArchitecture::AIEML_V1C) ? MMCM_CFG_BASE_V1C : MMCM_CFG_BASE;

		npu_write_cfg(cfg_base, (uint32_t*)&data, 1, ip_idx);

		keepgoing = 50;
		do
		{
			if (keepgoing-- == 0)
				return vart_ml_log_err_msg(vart_ml_error::DEVICE_MMCM_CONFIG_TIMEOUT,
				                           "Can't read the MMCM, timeout expired.\n");

			npu_read_cfg(cfg_base, (uint32_t*)&data, 1, ip_idx);
			usleep(100e3);

		} while (data != 0x0003);

		uint32_t clkfbout_mult_1 = (22 + cfg.clkFbOut_mul % 2) << 8;
		uint32_t clkfbout_mult_2 = ((cfg.clkFbOut_mul / 2) << 8) + cfg.clkFbOut_mul / 2;

		npu_write_cfg(MMCM_VERSAL_CLKFBOUT_MULT01(cfg_base), (uint32_t*)&clkfbout_mult_1, 1, ip_idx);
		npu_write_cfg(MMCM_VERSAL_CLKFBOUT_MULT02(cfg_base), (uint32_t*)&clkfbout_mult_2, 1, ip_idx);

		uint32_t clkout0_div_1 = (6656 + ((cfg.clkOut0_div % 2) << 15)) + ((cfg.clkOut0_div % 2) << 13)
		                         + ((cfg.clkOut0_div & 2) << 7);
		uint32_t clkout0_div_2 = (cfg.clkOut0_div / 4 << 8) + cfg.clkOut0_div / 4;

		npu_write_cfg(MMCM_VERSAL_CLKDIV01(cfg_base), (uint32_t*)&clkout0_div_1, 1, ip_idx);
		npu_write_cfg(MMCM_VERSAL_CLKDIV02(cfg_base), (uint32_t*)&clkout0_div_2, 1, ip_idx);

		uint32_t divclk_divide_1 = (cfg.divClk_divide % 2) << 10;
		uint32_t divclk_divide_2 = ((cfg.divClk_divide / 2) << 8) + cfg.divClk_divide / 2;

		npu_write_cfg(MMCM_VERSAL_DIV01(cfg_base), (uint32_t*)&divclk_divide_1, 1, ip_idx);
		npu_write_cfg(MMCM_VERSAL_DIV02(cfg_base), (uint32_t*)&divclk_divide_2, 1, ip_idx);

		data = 0x00;

		npu_write_cfg(cfg_base, (uint32_t*)&data, 1, ip_idx);

		keepgoing = 50;
		do
		{
			if (keepgoing-- == 0)
				return vart_ml_log_err_msg(vart_ml_error::DEVICE_MMCM_CONFIG_TIMEOUT,
				                           "Can't read the MMCM, timeout expired.\n");

			npu_read_cfg(cfg_base, (uint32_t*)&data, 1, ip_idx);
			usleep(100e3);

		} while (data != 0x0300);
	}

	return vart_ml_error::SUCCESS;
}

int npu_read_mmcm_freq(uint32_t ip_idx, float* freq)
{
	struct mmcm_config cfg;
	float              result;
	FpgaFamily         fpgaFamily;

	int err = npu_get_fpgafamily(&fpgaFamily);
	if (err)
		return err;

	if (fpgaFamily != FpgaFamily::VERSAL)
	{
		npu_read_cfg(MMCM_CFG_BASE, (uint32_t*)&cfg, sizeof(cfg) / sizeof(uint32_t), ip_idx);

		float mul = cfg.clkFbOut_mul + (cfg.clkFbOut_mulFracEna ? cfg.clkFbOut_mulFrac / 1000.0f : 0.0f);
		float div = cfg.divClk_divide
		            * (cfg.clkOut0_div + (cfg.clkOut0_divFracEna ? cfg.clkOut0_divFrac / 1000.0f : 0.0f));
		result = mul / div;
	}
	else
	{
		uint32_t data = 0x01;

		FpgaArchitecture arch;
		err = npu_get_architecture(&arch);
		if (err)
			return err;

		uint64_t cfg_base = (arch == FpgaArchitecture::AIEML_V1C) ? MMCM_CFG_BASE_V1C : MMCM_CFG_BASE;

		npu_write_cfg(cfg_base, (uint32_t*)&data, 1, ip_idx);

		int keepgoing = 50;
		do
		{
			if (keepgoing-- == 0)
				return vart_ml_log_err_msg(vart_ml_error::DEVICE_MMCM_CONFIG_TIMEOUT,
				                           "Can't read the MMCM, timeout expired.\n");

			npu_read_cfg(cfg_base, (uint32_t*)&data, 1, ip_idx);
			usleep(100e3);

		} while (data != 0x0003);

		uint32_t clkfbout_mult_1;
		uint32_t clkfbout_mult_2;

		npu_read_cfg(MMCM_VERSAL_CLKFBOUT_MULT01(cfg_base), (uint32_t*)&clkfbout_mult_1, 1, ip_idx);
		npu_read_cfg(MMCM_VERSAL_CLKFBOUT_MULT02(cfg_base), (uint32_t*)&clkfbout_mult_2, 1, ip_idx);

		cfg.clkFbOut_mul = (clkfbout_mult_2 >> 8) * 2 + (clkfbout_mult_1 >> 8) - 22;

		uint32_t clkout0_div_1;
		uint32_t clkout0_div_2;

		npu_read_cfg(MMCM_VERSAL_CLKDIV01(cfg_base), (uint32_t*)&clkout0_div_1, 1, ip_idx);
		npu_read_cfg(MMCM_VERSAL_CLKDIV02(cfg_base), (uint32_t*)&clkout0_div_2, 1, ip_idx);

		cfg.clkOut0_div =
		    (clkout0_div_2 >> 8) * 4 + (((clkout0_div_1 - 6656) >> 7) & 2) + ((clkout0_div_1 - 6656) >> 15);

		uint32_t divclk_divide_1;
		uint32_t divclk_divide_2;

		npu_read_cfg(MMCM_VERSAL_DIV01(cfg_base), (uint32_t*)&divclk_divide_1, 1, ip_idx);
		npu_read_cfg(MMCM_VERSAL_DIV02(cfg_base), (uint32_t*)&divclk_divide_2, 1, ip_idx);

		cfg.divClk_divide = (divclk_divide_2 >> 8) * 2 + (divclk_divide_1 >> 10);

		result = (float)cfg.clkFbOut_mul / (cfg.clkOut0_div * cfg.divClk_divide);

		data = 0x00;

		npu_write_cfg(cfg_base, (uint32_t*)&data, 1, ip_idx);

		keepgoing = 50;
		do
		{
			if (keepgoing-- == 0)
				return vart_ml_log_err_msg(vart_ml_error::DEVICE_MMCM_CONFIG_TIMEOUT,
				                           "Can't read the MMCM, timeout expired.\n");

			npu_read_cfg(cfg_base, (uint32_t*)&data, 1, ip_idx);
			usleep(100e3);

		} while (data != 0x0300);
	}

	*freq = result * npu_get_inputfreq(ip_idx);
	return vart_ml_error::SUCCESS;
}

int npu_read_aie_freq(float* freq)
{
	bool is_ve2302 = (npu_get_nbaiepercolumn(0) == AIE_PER_COLUMN_VE2302);

	uint64_t base_addr = is_ve2302 ? NOC_AIE_ML_FREQ_VE2302_BASE_ADDR : NOC_AIE_ML_FREQ_VE2802_BASE_ADDR;

	FpgaArchitecture arch;
	int              err = npu_get_architecture(&arch);
	if (err)
		return err;

	if (arch == FpgaArchitecture::V2)
		base_addr = NOC_AIE_V2_FREQ_BASE_ADDR;

	float    div = 2.0f;
	uint32_t ref;
	npu_read_noc(base_addr + NOC_ME_CORE_REF_CTRL_OFFSET, &ref, 1);
	if (ref == 0x02000100)
		div = 1.0f;
	else if (ref != 0x02000200)
		return vart_ml_log_err_msg(vart_ml_error::DEVICE_UNEXPECTED_ARG_VALUE,
		                           "Wrong value for ME_CORE_REF_CTRL detected (should be 0x02000200).\n");

	uint32_t aieFreq;
	npu_read_noc(base_addr + NOC_AIE_FREQ_OFFSET, &aieFreq, 1);
	if ((aieFreq & 0xffff00ff) != 0x10000)
		return vart_ml_log_err_msg(vart_ml_error::DEVICE_UNEXPECTED_ARG_VALUE,
		                           "Wrong value for Freq register.\n");

	uint32_t pll;
	npu_read_noc(base_addr + NOC_ME_PLL_STATUS_OFFSET, &pll, 1);
	if (pll != 0x00000005)
		return vart_ml_log_err_msg(vart_ml_error::DEVICE_UNEXPECTED_ARG_VALUE,
		                           "Wrong value for PLL status detected (should be 0x00000005).\n");

	*freq = ((float)((aieFreq >> 8) & 0xff) * (100.0 / div)) / 6.0;
	return vart_ml_error::SUCCESS;
}

int npu_change_aie_freq(const float target_freq)
{
	if (target_freq < 800 || 1500 < target_freq)
		return vart_ml_log_err_msg(vart_ml_error::DEVICE_BAD_ARG,
		                           "AIE clock frequence is out of range [800:1500].\n");

	bool is_ve2302 = (npu_get_nbaiepercolumn(0) == AIE_PER_COLUMN_VE2302);

	uint64_t base_addr = is_ve2302 ? NOC_AIE_ML_FREQ_VE2302_BASE_ADDR : NOC_AIE_ML_FREQ_VE2802_BASE_ADDR;

	FpgaArchitecture arch;
	int              err = npu_get_architecture(&arch);
	if (err)
		return err;

	if (arch == FpgaArchitecture::V2)
		base_addr = NOC_AIE_V2_FREQ_BASE_ADDR;

	float    div = 2.0f;
	uint32_t ref;
	npu_read_noc(base_addr + NOC_ME_CORE_REF_CTRL_OFFSET, &ref, 1);
	if (ref == 0x02000100)
		div = 1.0f;
	else if (ref != 0x02000200)
		return vart_ml_log_err_msg(vart_ml_error::DEVICE_UNEXPECTED_ARG_VALUE,
		                           "Wrong value for ME_CORE_REF_CTRL detected (should be 0x02000200).\n");

	uint32_t freq;
	npu_read_noc(base_addr + NOC_AIE_FREQ_OFFSET, &freq, 1);
	if ((freq & 0xffff00ff) != 0x10000)
		return vart_ml_log_err_msg(vart_ml_error::DEVICE_UNEXPECTED_ARG_VALUE,
		                           "Wrong value for Freq register.\n");

	uint32_t pll;
	npu_read_noc(base_addr + NOC_ME_PLL_STATUS_OFFSET, &pll, 1);
	if (pll != 0x00000005)
		return vart_ml_log_err_msg(vart_ml_error::DEVICE_UNEXPECTED_ARG_VALUE,
		                           "Wrong value for PLL status detected (should be 0x00000005).\n");

	int aieFreq = static_cast<int>((target_freq * 6.0f) / (100.0f / div));

	uint32_t data = 0xf9e8d7c6;
	npu_write_noc(base_addr + NOC_AIE_FREQ_VALID_OFFSET, &data, 1);

	data = 0x0001 << 16 | aieFreq << 8 | 0x09;
	npu_write_noc(base_addr + NOC_AIE_FREQ_OFFSET, &data, 1);

	data = 0x0001 << 16 | aieFreq << 8 | 0x08;
	npu_write_noc(base_addr + NOC_AIE_FREQ_OFFSET, &data, 1);

	data = 0x0001 << 16 | aieFreq << 8 | 0x00;
	npu_write_noc(base_addr + NOC_AIE_FREQ_OFFSET, &data, 1);

	data = 0x1;
	npu_write_noc(base_addr + NOC_AIE_FREQ_VALID_OFFSET, &data, 1);

	usleep(10e3);
	return vart_ml_error::SUCCESS;
}

/* Measure SRVBus clock frequency using a tick counter implemented in PL. */
static int npu_measure_srvbus_frequency(uint32_t ip_idx, float* freq)
{
	int      err;
	uint32_t en_reg_val = 1;
	uint32_t c1, c2;
	uint64_t en_offset, counter_offset;

	FpgaArchitecture arch;
	err = npu_get_architecture(&arch);
	if (err)
		return err;

	en_offset =
	    (arch == FpgaArchitecture::AIEML_V1C) ? TICK_COUNTER_ENABLE_OFFSET_V1C : TICK_COUNTER_ENABLE_OFFSET;
	counter_offset = (arch == FpgaArchitecture::AIEML_V1C) ? TICK_COUNTER_OFFSET_V1C : TICK_COUNTER_OFFSET;

	npu_write_cfg(en_offset, &en_reg_val, 1, ip_idx);
	npu_read_cfg(counter_offset, &c1, 1, ip_idx);
	sleep(1);
	npu_read_cfg(counter_offset, &c2, 1, ip_idx);

	en_reg_val = 0;
	npu_write_cfg(en_offset, &en_reg_val, 1, ip_idx);

	uint32_t delta = c2 - c1;
	*freq          = delta / 1e6f;

	return vart_ml_error::SUCCESS;
}

int npu_measure_freq(enum clock clk, uint32_t ip_idx, size_t ddr_idx, float* freq, bool force_print)
{
	static float ref_clk_ratio = 0;
	uint64_t     off;
	uint32_t     freq_sample;

	if ((clk == CLK_DDR && ddr_idx >= npu_get_nbddrs()) || (clk != CLK_DDR && ddr_idx > 0))
		return vart_ml_log_err_msg(vart_ml_error::DEVICE_CLK_FREQ_MEASURE_FAILURE,
		                           "Clock DDR index %d (type %d) doesn't exist.\n",
		                           ddr_idx,
		                           clk);

	// Control bus clock frequency check
	if (ref_clk_ratio == 0)
	{
		vart_ml_log_level log_lvl = (force_print) ? LOG_INFO : LOG_DBG;
		float             measured_freq;
		float             freq_clk_10m;

		int err = npu_measure_srvbus_frequency(ip_idx, &measured_freq);
		if (err)
			return err;

		int config_freq = npu_get_ctrlbusfreq(ip_idx);

		float abs_freq_diff = fabsf(measured_freq - (float)config_freq);

		if (abs_freq_diff > (config_freq * TICK_COUNTER_TOLERANCE))
		{
			vart_ml_log(LOG_WARN,
			            "Warning: Absolute error of Control Bus clock frequency exceeds %.0f%% tolerance.\n",
			            TICK_COUNTER_TOLERANCE * 100);
			log_lvl = LOG_WARN;
		}

		vart_ml_log(log_lvl,
		            "Measured Control Bus clock frequency: %.2f MHz, expected %.2f MHz\n",
		            measured_freq,
		            (float)config_freq);

		// Read freq meter on PL
		npu_read_cfg(CLK_10M_OFFSET, &freq_sample, 1, ip_idx);

		if (freq_sample == 0)
			return vart_ml_log_err_msg(vart_ml_error::DEVICE_CLK_FREQ_MEASURE_FAILURE,
			                           "Failed to read freq meter.\n");

		// Compute actual freq value
		freq_clk_10m  = roundf(npu_get_mntrclkcoef(ip_idx) / freq_sample * 100) / 100;
		ref_clk_ratio = measured_freq / freq_clk_10m;

		vart_ml_log(log_lvl,
		            "Control bus freq - measured: %.2f MHz, from compile: %d MHz, freq meter: %.2f MHz. "
		            "Applying ratio %f "
		            "on freq meter.\n",
		            measured_freq,
		            config_freq,
		            freq_clk_10m,
		            ref_clk_ratio);
	}

	switch (clk)
	{
	case CLK_10M:
		off = CLK_10M_OFFSET;
		break;
	case CLK_4X:
		off = CLK4X_OFFSET;
		break;
	case CLK_2X:
		off = CLK2X_OFFSET;
		break;
	case CLK_1X:
		off = CLK1X_OFFSET;
		break;
	case CLK_DDR:
		off = CLK_DDR_OFFSET(ddr_idx);
		break;
	default:
		return vart_ml_log_err_msg(vart_ml_error::DEVICE_CLK_FREQ_MEASURE_FAILURE, "Unknown clock type.\n");
	}

	float acc = 0;
	for (size_t i = 0; i < NB_SAMPLES; i++)
	{
		npu_read_cfg(off, &freq_sample, 1, ip_idx);
		acc += freq_sample;
		usleep(1000);
	}

	if (acc == 0)
	{
		*freq = 0.0f;
		return vart_ml_error::SUCCESS;
	}

	acc /= NB_SAMPLES;
	/* VART ML's frequency counters work by dividing the frequency
	   to be measured by a factor, then by counting how many periods
	   of a reference clock the divided clock's period spans.
	   clkmntrcoef is the reference clock frequency times the divider.
	*/
	*freq = roundf(npu_get_mntrclkcoef(ip_idx) * ref_clk_ratio / acc * 100) / 100;
	return vart_ml_error::SUCCESS;
}

int npu_read_preciseTimer(uint32_t ip_idx, struct timespec* precise_time)
{
	uint32_t hwNbCyclesCounter = 0;
	int      err;

	FpgaFamily fpgaFamily;
	err = npu_get_fpgafamily(&fpgaFamily);
	if (err)
		return err;

	FpgaArchitecture arch;
	err = npu_get_architecture(&arch);
	if (err)
		return err;

	if (fpgaFamily != FpgaFamily::VERSAL)
		npu_read_cfg(TIMER_OFFSET(0, 0), &hwNbCyclesCounter, 1, ip_idx);
	else if (arch == FpgaArchitecture::V2)
		npu_read_cfg(TIMER_OFFSET_VERSAL(0, 0), &hwNbCyclesCounter, 1, ip_idx);
	else
		npu_read_cfg(TIMER_OFFSET_VERSAL_V1C, &hwNbCyclesCounter, 1, ip_idx);

	*precise_time = (arch == FpgaArchitecture::AIEML_V1C)
	                    ? timespecfrom(hwNbCyclesCounter / clock_state[ip_idx].frequency4x)
	                    : timespecfrom(hwNbCyclesCounter / clock_state[ip_idx].frequency2x);

	return vart_ml_error::SUCCESS;
}
