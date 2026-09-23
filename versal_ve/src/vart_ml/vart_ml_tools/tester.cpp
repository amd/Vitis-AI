/**
 * @file tester.cpp
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
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <random>
#include <sstream>
#include <unistd.h>

#include "io/io.h"
#include "npu_runner/npu_runner.h"
#include "tester.h"
#include "utils/fpga_info.h"
#include "utils/log.h"
#include "utils/npu_reg.h"
#include "utils/shell.h"
#include "ddr_debug.h"

#define NB_DDR_TESTS 10000

static FpgaArchitecture arch;
static enum FpgaFamily  fpgaFamily;

uint32_t randomize(void)
{
	uint32_t                                tmp;
	std::random_device                      r;
	std::default_random_engine              randengine(r());
	std::uniform_int_distribution<uint32_t> uniform_dist;

	tmp = uniform_dist(randengine);
	return tmp;
}

uint64_t random_ddr_offset(uint64_t ddr_len)
{
	uint64_t off;
	for (size_t i = 0; i < 2; i++)
		((uint32_t*)&off)[i] = randomize();

	// Ensure word-alignment
	return (off % ddr_len) & ~3ull;
}

static int test_ctrlbus(uint32_t ip_idx)
{
	size_t mctrl_mem_base, mctrl_mem_len, mctrl_mem_mask;

	if (fpgaFamily != VERSAL)
	{
		mctrl_mem_base = MCTRL_MEM_BASE;
		mctrl_mem_len  = MCTRL_MEM_LEN;
		mctrl_mem_mask = MCTRL_MEM_MASK;
	}
	else if (arch != FpgaArchitecture::AIEML_V1C)
	{
		mctrl_mem_base = VERSAL_MCTRL_MEM_TEST_BASE;
		mctrl_mem_len  = VERSAL_MCTRL_MEM_TEST_LEN;
		mctrl_mem_mask = VERSAL_MCTRL_MEM_TEST_MASK;
	}
	else
	{
		mctrl_mem_base = NBUFF_BASE_OFFSET;
		mctrl_mem_len  = NBUFF_LEN;
		mctrl_mem_mask = 0xFFFFFFFF;
	}

	uint32_t testval, readval;
	/* i is the number of tested *words* */
	for (uint32_t i = 0; i < mctrl_mem_len / 4; i++)
	{
		testval = randomize();
		npu_write_cfg(mctrl_mem_base + 4 * i, &testval, 1, ip_idx);
		npu_read_cfg(mctrl_mem_base + 4 * i, &readval, 1, ip_idx);

		if ((readval & mctrl_mem_mask) != (testval & mctrl_mem_mask))
			return vart_ml_log_err_msg(vart_ml_error::DEVICE_RW_TEST_FAILURE,
			                           "Failure at CTRLBus offset 0x%x\n"
			                           "Expected 0x%x, read 0x%x\n",
			                           mctrl_mem_base + 4 * i,
			                           testval,
			                           readval);
	}

	return vart_ml_error::SUCCESS;
}

static void test_ctrlbus_perf(uint32_t ip_idx)
{
	size_t mctrl_mem_base, mctrl_mem_len;

	if (fpgaFamily != VERSAL)
	{
		mctrl_mem_base = MCTRL_MEM_BASE;
		mctrl_mem_len  = MCTRL_MEM_LEN;
	}
	else if (arch != FpgaArchitecture::AIEML_V1C)
	{
		mctrl_mem_base = VERSAL_MCTRL_MEM_TEST_BASE;
		mctrl_mem_len  = VERSAL_MCTRL_MEM_TEST_LEN;
	}
	else
	{
		mctrl_mem_base = NBUFF_BASE_OFFSET;
		mctrl_mem_len  = NBUFF_LEN;
	}

	size_t                word_count = mctrl_mem_len / 4;
	std::vector<uint32_t> testval(word_count);
	uint32_t              readval;

	for (uint32_t i = 0; i < word_count; i++)
		testval[i] = randomize();

	auto start = std::chrono::high_resolution_clock::now();
	/* i is the number of tested *words* */
	for (uint32_t i = 0; i < word_count; i++)
	{
		npu_write_cfg(mctrl_mem_base + 4 * i, &testval[i], 1, ip_idx);
		npu_read_cfg(mctrl_mem_base + 4 * i, &readval, 1, ip_idx);
	}
	auto curr      = std::chrono::high_resolution_clock::now();
	auto elapsed_s = std::chrono::duration<double>(curr - start);

	vart_ml_log(LOG_INFO,
	            "[W/R]    :%10.2f kbytes/s. (%zu bytes)\n",
	            (double)mctrl_mem_len / 1000.0 / elapsed_s.count(),
	            mctrl_mem_len);

	/* WRITE one 4-byte word at a time. */
	start = std::chrono::high_resolution_clock::now();
	for (uint32_t i = 0; i < word_count; i++)
		npu_write_cfg(mctrl_mem_base + 4 * i, &i, 1, ip_idx);

	/* Force wait until completion of last posted write. */
	npu_read_cfg(mctrl_mem_base + 4 * (word_count - 1), &readval, 1, ip_idx);
	curr      = std::chrono::high_resolution_clock::now();
	elapsed_s = std::chrono::duration<double>(curr - start);

	vart_ml_log(LOG_INFO,
	            "[W]      :%10.2f kbytes/s. (%zu bytes)\n",
	            (double)mctrl_mem_len / 1000.0 / elapsed_s.count(),
	            mctrl_mem_len);

	/* WRITE BURST */
	start = std::chrono::high_resolution_clock::now();
	npu_write_cfg(mctrl_mem_base, testval.data(), word_count, ip_idx);

	/* Force wait until completion of last posted write. */
	npu_read_cfg(mctrl_mem_base + 4 * (word_count - 1), &readval, 1, ip_idx);
	curr      = std::chrono::high_resolution_clock::now();
	elapsed_s = std::chrono::duration<double>(curr - start);

	vart_ml_log(LOG_INFO,
	            "[W burst]:%10.2f kbytes/s. (%zu bytes)\n",
	            (double)mctrl_mem_len / 1000.0 / elapsed_s.count(),
	            mctrl_mem_len);

	/* READ one 4-byte word at a time. */
	start = std::chrono::high_resolution_clock::now();
	for (uint32_t i = 0; i < word_count; i++)
		npu_read_cfg(mctrl_mem_base + 4 * i, &readval, 1, ip_idx);

	curr      = std::chrono::high_resolution_clock::now();
	elapsed_s = std::chrono::duration<double>(curr - start);

	vart_ml_log(LOG_INFO,
	            "[R]      :%10.2f kbytes/s. (%zu bytes)\n",
	            (double)mctrl_mem_len / 1000.0 / elapsed_s.count(),
	            mctrl_mem_len);

	/* READ BURST */
	start = std::chrono::high_resolution_clock::now();
	npu_read_cfg(mctrl_mem_base, testval.data(), word_count, ip_idx);

	curr      = std::chrono::high_resolution_clock::now();
	elapsed_s = std::chrono::duration<double>(curr - start);

	vart_ml_log(LOG_INFO,
	            "[R burst]:%10.2f kbytes/s. (%zu bytes)\n",
	            (double)mctrl_mem_len / 1000.0 / elapsed_s.count(),
	            mctrl_mem_len);
}

static int test_extddrs(void)
{
	bool testOk = true;
	int  err;

	for (size_t n = 0; n < npu_get_nbddrs(); n++)
	{
		for (int i = 0; i < NB_DDR_TESTS; i++)
		{
			uint64_t off;
			off = npu_get_extmemBaseAddr(n) + random_ddr_offset(npu_get_extmemlen(n));

			uint32_t testval = randomize();
			uint32_t readval;
			err = npu_write_ddr(npu_get_addr_from_phy_addr(off), (uint8_t*)&testval, sizeof(testval));
			if (err)
				return err;

			err = npu_read_ddr(npu_get_addr_from_phy_addr(off), (uint8_t*)&readval, sizeof(readval));
			if (err)
				return err;

			if (readval != testval)
			{
				vart_ml_log(LOG_INFO, "Failure DDR at offset 0x%llx.\n", off);
				vart_ml_log(LOG_INFO, "Expected 0x%x testval, read 0x%x.\n", testval, readval);
				testOk = false;
			}
		}
	}

	if (!testOk)
		return vart_ml_error::DEVICE_DDR_RW_FAILURE;

	return vart_ml_error::SUCCESS;
}

static int npu_sanity_checks(uint32_t ip_idx)
{
	uint32_t timestamp;
	int      err;

	npu_read_cfg(TIMESTAMP_OFFSET, &timestamp, 1, ip_idx);

	vart_ml_log(LOG_INFO, "Timestamp");

	if (npu_get_nb_ip() != 1)
		vart_ml_log(LOG_INFO, " IP %zu", ip_idx);

	vart_ml_log(LOG_INFO, ": 0x%08x\n", timestamp);

	if (timestamp == 0)
		return vart_ml_log_err_msg(vart_ml_error::DEVICE_BAD_TIMESTAMP,
		                           "Timestamp should not be zero, something's wrong.");

	if (npu_check_asserts(ip_idx))
		vart_ml_log(LOG_INFO, "No assertion was raised.\n");

	size_t nbsystems = npu_get_nbsystems(ip_idx);
	size_t nbddrs    = npu_get_nbddrs();
	err              = npu_start_clocks(ip_idx, (1 << nbsystems) - 1);
	if (err)
		return err;

	npu_read_cfg(MMCM_OFFSET, &timestamp, 1, 0);
	vart_ml_log(LOG_INFO, "MMCM: 0x%x.\n", timestamp);

	float clk;
	err = npu_measure_freq(CLK_10M, ip_idx, 0, &clk, true);
	if (err)
		return err;

	if (clk == 0.0)
	{
		err = npu_stop_clocks(ip_idx);
		if (err)
			return err;

		return vart_ml_log_err_msg(vart_ml_error::DEVICE_CLK_FREQ_MEASURE_FAILURE,
		                           "clk_10m should not be zero.\n");
	}
	vart_ml_log(LOG_INFO, "Clock %-3dMHz frequency: %.2f MHz.\n", npu_get_ctrlbusfreq(ip_idx), clk);

	err = npu_measure_freq(CLK_4X, ip_idx, 0, &clk);
	if (err)
		return err;

	if (clk == 0.0)
	{
		err = npu_stop_clocks(ip_idx);
		if (err)
			return err;

		return vart_ml_log_err_msg(vart_ml_error::DEVICE_CLK_FREQ_MEASURE_FAILURE,
		                           "clk_4x should not be zero.\n");
	}
	vart_ml_log(LOG_INFO, "Clock 4x     frequency: %.2f MHz.\n", clk);

	if (arch != FpgaArchitecture::AIEML_V1C)
	{
		err = npu_measure_freq(CLK_2X, ip_idx, 0, &clk);
		if (err)
			return err;

		if (clk == 0.0)
		{
			err = npu_stop_clocks(ip_idx);
			if (err)
				return err;

			return vart_ml_log_err_msg(vart_ml_error::DEVICE_CLK_FREQ_MEASURE_FAILURE,
			                           "clk_2x should not be zero.\n");
		}
		vart_ml_log(LOG_INFO, "Clock 2x     frequency: %.2f MHz.\n", clk);

		err = npu_measure_freq(CLK_1X, ip_idx, 0, &clk);
		if (err)
			return err;

		if (clk == 0.0)
		{
			err = npu_stop_clocks(ip_idx);
			if (err)
				return err;

			return vart_ml_log_err_msg(vart_ml_error::DEVICE_CLK_FREQ_MEASURE_FAILURE,
			                           "clk_1x should not be zero.\n");
		}
		vart_ml_log(LOG_INFO, "Clock 1x     frequency: %.2f MHz.\n", clk);

		for (size_t i = 0; i < nbddrs; i++)
		{
			err = npu_measure_freq(CLK_DDR, ip_idx, i, &clk);
			if (err)
				return err;

			if (clk == 0.0)
			{
				err = npu_stop_clocks(ip_idx);
				if (err)
					return err;

				return vart_ml_log_err_msg(
				    vart_ml_error::DEVICE_CLK_FREQ_MEASURE_FAILURE, "clk_ddr[%zu] should not be zero.\n", i);
			}
			vart_ml_log(LOG_INFO, "DDR %zu, clock frequency: %.2f MHz\n", i, clk);
		}
	}

	err = npu_stop_clocks(ip_idx);
	if (err)
		return err;

	return vart_ml_error::SUCCESS;
}

/* DDR test plan:
   1) Test that Linux can read/write the full extent of DDR VART uses, correctly
   2) Check that the DDR debug module can read+write, then write+read correctly
   3) Check that 256-word blocks can be written from ARM, then read as-is by VART
   4) Check that the opposite ils also possible

*/
static bool checkresult(std::string                  test_name,
                        uint64_t                     off,
                        uint32_t                     testcst,
                        const std::vector<uint32_t>& found,
                        uint64_t                     addr_base = 0)
{
	for (size_t i = 0; i < found.size(); i++)
	{
		if ((found[i] ^ (off + sizeof(uint32_t) * i)) != testcst)
			break;

		if (i == found.size() - 1)
			return true;
	}

	vart_ml_log(LOG_INFO, "%s failed at address 0x%llx\n", test_name.c_str(), addr_base + off);
	vart_ml_log(LOG_INFO, "DDR address / Expected / Found:\n");
	for (size_t i = 0; i < found.size(); i++)
		if ((found[i] ^ (off + sizeof(uint32_t) * i)) != testcst)
			vart_ml_log(LOG_INFO,
			            "0x%llx: 0x%x 0x%x\n",
			            addr_base + off + i * sizeof(uint32_t),
			            (testcst ^ (off + sizeof(uint32_t) * i)),
			            found[i]);
	return false;
}
static int ddr_debug_module_quickchecks(uint32_t ip_idx, size_t ddr_idx)
{
	std::vector<uint32_t> buf(256 / sizeof(uint32_t));

	uint64_t ddr_base = npu_get_extmemBaseAddr(ddr_idx);
	uint64_t ddr_len  = npu_get_extmemlen(ddr_idx);

	/* For arithmetic reasons, it is very hard to distribute 256-byte blocks
	 * evenly through the DDR, all of them 512-bit aligned, while making sure
	 * that the last 512 bits of DDR are also tested. For instance, there are
	 * only two numbers of tested blocks that will work for 2GiB DDRs: 2 and 48.
	 * This test is cruder but good enough.
	 */
	size_t step = ddr_len / 128;
	int    err;

	for (size_t i = 0; i < 128; i++)
	{
		for (size_t j = 0; j < 256 / sizeof(uint32_t); j++)
			buf[j] = 0xdeadbeef ^ (step * i + sizeof(uint32_t) * j);

		for (size_t j = 0; j < 256 / 64; j++)
		{
			err = ddr_debug_write(ip_idx, ddr_base + step * i + 64 * j, buf.data() + 16 * j);
			if (err)
				return err;
		}
	}

	for (size_t i = 0; i < 256 / sizeof(uint32_t); i++)
		buf[i] = 0xdeadbeef ^ (ddr_len - 256 + sizeof(uint32_t) * i);

	for (size_t i = 0; i < 256 / 64; i++)
	{
		err = ddr_debug_write(ip_idx, ddr_base + ddr_len - 256 + 64 * i, buf.data() + 16 * i);
		if (err)
			return err;
	}

	for (size_t i = 0; i < 128; i++)
	{
		for (size_t j = 0; j < 256 / 64; j++)
		{
			err = ddr_debug_read(ip_idx, (uint64_t)(ddr_base + step * i + 64 * j), buf.data() + 16 * j);
			if (err)
				return err;
		}

		checkresult("DDR debug test", step * i, 0xdeadbeef, buf, ddr_base);
	}

	for (size_t i = 0; i < 256 / 64; i++)
	{
		err = ddr_debug_read(ip_idx, (uint64_t)(ddr_base + ddr_len - 256 + 64 * i), buf.data() + 16 * i);
		if (err)
			return err;
	}

	if (!checkresult("DDR debug test", ddr_len - 256, 0xdeadbeef, buf, ddr_base))
		return vart_ml_error::TEST_FAILURE;

	return vart_ml_error::SUCCESS;
}

static int npu_test_reads(uint32_t ip_idx, size_t ddr_idx)
{
	std::vector<uint32_t> buf(256 / sizeof(uint32_t));

	uint64_t ddr_base = npu_get_extmemBaseAddr(ddr_idx);
	uint64_t ddr_len  = npu_get_extmemlen(ddr_idx);
	size_t   step     = ddr_len / 1024;

	bool testOk = true;
	int  err;

	for (size_t i = 0; i < 1024; i++)
	{
		for (size_t j = 0; j < 256 / sizeof(uint32_t); j++)
			buf[j] = 0xc01dcafe ^ (step * i + sizeof(uint32_t) * j);

		err = npu_write_ddr(npu_get_addr_from_phy_addr(ddr_base + (step * i)),
		                    (uint8_t*)buf.data(),
		                    buf.size() * sizeof(uint32_t));
		if (err)
			return err;
	}

	for (size_t i = 0; i < 1024; i++)
	{
		/* ddr_debug_read reads 512 bits = 64 bytes = 16 words. */
		for (size_t j = 0; j < 256 / 64; j++)
		{
			err = ddr_debug_read(ip_idx, (uint64_t)(ddr_base + step * i + 64 * j), buf.data() + 16 * j);
			if (err)
				return err;
		}

		if (!checkresult("DDR read test", step * i, 0xc01dcafe, buf, ddr_base))
			testOk = false;
	}

	if (!testOk)
		return vart_ml_error::DEVICE_DDR_RW_FAILURE;

	return vart_ml_error::SUCCESS;
}

static int npu_test_writes(uint32_t ip_idx, size_t ddr_idx)
{
	std::vector<uint32_t> buf(256 / sizeof(uint32_t));

	uint64_t ddr_base = npu_get_extmemBaseAddr(ddr_idx);
	uint64_t ddr_len  = npu_get_extmemlen(ddr_idx);
	size_t   step     = ddr_len / 1024;

	bool testOk = true;
	int  err;

	for (size_t i = 0; i < 1024; i++)
	{
		for (size_t j = 0; j < 256 / sizeof(uint32_t); j++)
			buf[j] = 0xbaadf00d ^ (step * i + sizeof(uint32_t) * j);
		for (size_t j = 0; j < 256 / 64; j++)
		{
			err = ddr_debug_write(ip_idx, (uint64_t)(ddr_base + step * i + 64 * j), buf.data() + 16 * j);
			if (err)
				return err;
		}
	}

	for (size_t i = 0; i < 1024; i++)
	{
		err = npu_read_ddr(npu_get_addr_from_phy_addr(ddr_base + (step * i)),
		                   (uint8_t*)buf.data(),
		                   buf.size() * sizeof(uint32_t));
		if (err)
			return err;

		if (!checkresult("DDR write test", step * i, 0xbaadf00d, buf, ddr_base))
			testOk = false;
	}

	if (!testOk)
		return vart_ml_error::DEVICE_DDR_RW_FAILURE;

	return vart_ml_error::SUCCESS;
}

static int npu_testwrapping()
{
	bool testOk = true;
	int  err;

	/* Write a block of 512 bits at every address of kind 2**i that's
	 * properly aligned, then read them all.
	 */
	std::vector<uint32_t> buf(64 / sizeof(uint32_t));
	for (size_t n = 0; n < npu_get_nbddrs(); n++)
	{
		uint64_t ddr_base;
		ddr_base = npu_get_extmemBaseAddr(n);

		for (uint64_t off = 1; off < npu_get_extmemlen(n); off *= 64)
		{
			for (size_t i = 0; i < buf.size(); i++)
				buf[i] = 0xabcdef01 ^ ((off & ~63ull) + sizeof(uint32_t) * i);

			err = npu_write_ddr(npu_get_addr_from_phy_addr((ddr_base + off) & ~63ull),
			                    (uint8_t*)buf.data(),
			                    buf.size() * sizeof(uint32_t));
			if (err)
				return err;
		}

		for (uint64_t off = 1; off < npu_get_extmemlen(n); off *= 64)
		{
			err = npu_read_ddr(npu_get_addr_from_phy_addr((ddr_base + off) & ~63ull),
			                   (uint8_t*)buf.data(),
			                   buf.size() * sizeof(uint32_t));
			if (err)
				return err;

			if (!checkresult("DDR wrapping test", off & ~63ull, 0xabcdef01, buf, ddr_base))
				testOk = false;
		}
	}

	if (!testOk)
		return vart_ml_error::DEVICE_DDR_RW_FAILURE;

	return vart_ml_error::SUCCESS;
}

int tester(int argc, const char** argv)
{
	if (argc > 4)
		return vart_ml_log_err_msg(vart_ml_error::TOOLS_BAD_USAGE,
		                           "Usage: vart_ml_tools %s [ip_idx] [--test-ctrl] [--test-ddr].\n",
		                           argv[0]);

	bool test_ctrl   = false;
	bool test_ddr    = false;
	bool test_all_ip = false;

	if (argc == 1)
	{
		test_all_ip = true;
	}
	else
		for (int i = 1; i < argc; i++)
			if (strcmp(argv[i], "--test-ctrl") == 0)
			{
				test_ctrl = true;
				if (i == 1)
					test_all_ip = true;
			}
			else if (strcmp(argv[i], "--test-ddr") == 0)
			{
				test_ddr = true;
				if (i == 1)
					test_all_ip = true;
			}

	int err = npu_connect_peripherals();
	if (err)
		return err;

	if (test_all_ip)
	{
		for (size_t i = 0; i < npu_get_nb_ip(); i++)
		{
			if (npu_get_nb_ip() != 1)
				vart_ml_log(LOG_INFO, "\nIP %zu:\n", i);

			err = npu_connect_ip(i);
			if (err)
				return err;

			err = npu_get_architecture(&arch);
			if (err)
				return err;

			vart_ml_log(LOG_INFO, "Architecture: %s\n", stringFromArch(arch));

			err = npu_get_fpgafamily(&fpgaFamily);
			if (err)
				return err;

			vart_ml_log(LOG_INFO, "FPGA family: %s\n", stringFromFpgaFamily(fpgaFamily));

			err = npu_sanity_checks(i);
			if (err)
				return err;

			vart_ml_log(LOG_INFO, "Sanity test: OK\n");

			err = test_ctrlbus(i);
			if (err)
				return err;

			vart_ml_log(LOG_INFO, "Control bus memory: OK\n");
		}
		vart_ml_log(LOG_INFO, "\n");
	}
	else
	{
		uint32_t ip_idx = parse_uint(argv[1]);
		if (ip_idx >= npu_get_nb_ip())
			return vart_ml_log_err_msg(vart_ml_error::TOOLS_BAD_USAGE,
			                           "IP index is too high, there is only %u IPs available\n",
			                           npu_get_nb_ip());

		err = npu_connect_ip(ip_idx);
		if (err)
			return err;

		err = npu_get_architecture(&arch);
		if (err)
			return err;

		vart_ml_log(LOG_INFO, "Architecture: %s\n", stringFromArch(arch));

		err = npu_get_fpgafamily(&fpgaFamily);
		if (err)
			return err;

		vart_ml_log(LOG_INFO, "FPGA family: %s\n", stringFromFpgaFamily(fpgaFamily));

		err = npu_sanity_checks(ip_idx);
		if (err)
			return err;

		vart_ml_log(LOG_INFO, "Sanity test: OK\n");

		err = test_ctrlbus(ip_idx);
		if (err)
			return err;

		vart_ml_log(LOG_INFO, "Control bus memory: OK\n");
	}

	if (!test_ctrl && !test_ddr)
	{
		vart_ml_log(LOG_INFO, "Not testing DDR, use %s --test-ddr if needed.\n", argv[0]);
		return vart_ml_error::SUCCESS;
	}

	if (test_ctrl)
	{
		if (test_all_ip)
			if (npu_get_nb_ip() == 1)
			{
				vart_ml_log(LOG_INFO, "Control bus access performance measurements:\n");
				test_ctrlbus_perf(0);
			}
			else
				for (size_t i = 0; i < npu_get_nb_ip(); i++)
				{
					vart_ml_log(LOG_INFO, "Control bus access performance measurements for IP %zu:\n", i);
					test_ctrlbus_perf(i);
				}
		else
		{
			uint32_t ip_idx = parse_uint(argv[1]);
			if (ip_idx >= npu_get_nb_ip())
				return vart_ml_log_err_msg(vart_ml_error::TOOLS_BAD_USAGE,
				                           "IP index is too high, there is only %u IPs available\n",
				                           npu_get_nb_ip());
			vart_ml_log(LOG_INFO, "Control bus access performance measurements:\n");
			test_ctrlbus_perf(ip_idx);
		}
	}

	if (!test_ddr)
	{
		vart_ml_log(LOG_INFO, "Not testing DDR, use %s --test-ddr if needed.\n", argv[0]);
		return vart_ml_error::SUCCESS;
	}

	if (npu_is_xrt_en())
		for (size_t i = 0; i < npu_get_nbddrs(); i++)
			npu_malloc(npu_get_extmemlen(i), i);

	bool testOk = true;
	if (test_extddrs() == vart_ml_error::SUCCESS)
		vart_ml_log(LOG_INFO, "VART DDR: OK, tested %d addresses per DDR\n", NB_DDR_TESTS);
	else
		testOk = false;

	if (npu_get_nbddrs() != 1 and fpgaFamily != VERSAL)
		vart_ml_log(LOG_INFO,
		            "The DDR debug module only works on single-DDR on UltraScale, skipping its tests.\n");
	else if (arch != FpgaArchitecture::AIEML_V1C)
	{
		if (npu_is_embedded())
		{
			auto test_ddr_debug = [&](uint32_t ip_idx) {
				if (!is_debug_srv_ddr_en(ip_idx))
				{
					vart_ml_log(
					    LOG_INFO, "The DDR debug module is absent for IP %u, skipping its tests.\n", ip_idx);
					return;
				}

				for (size_t n = 0; n < npu_get_nbddrs(); n++)
				{
					vart_ml_log(LOG_INFO, "Simple DDR debug module tests for IP %u DDR %zu ...\n", ip_idx, n);
					if (ddr_debug_module_quickchecks(ip_idx, n) == vart_ml_error::SUCCESS)
						vart_ml_log(LOG_INFO, "DDR debug module for IP %u DDR %zu: OK\n", ip_idx, n);
					else
						testOk = false;

					vart_ml_log(LOG_INFO, "Testing VART's DDR reads for IP %u DDR %zu ...\n", ip_idx, n);
					if (npu_test_reads(ip_idx, n) == vart_ml_error::SUCCESS)
						vart_ml_log(LOG_INFO, "VART's DDR reads for IP %u DDR %zu: OK\n", ip_idx, n);
					else
						testOk = false;

					vart_ml_log(LOG_INFO, "Testing VART's DDR writes for IP %u DDR %zu ...\n", ip_idx, n);
					if (npu_test_writes(ip_idx, n) == vart_ml_error::SUCCESS)
						vart_ml_log(LOG_INFO, "VART's DDR writes for IP %u DDR %zu: OK\n", ip_idx, n);
					else
						testOk = false;
				}
			};

			if (test_all_ip)
				for (size_t i = 0; i < npu_get_nb_ip(); i++)
					test_ddr_debug(i);
			else
			{
				uint32_t ip_idx = parse_uint(argv[1]);
				if (ip_idx >= npu_get_nb_ip())
					return vart_ml_log_err_msg(vart_ml_error::TOOLS_BAD_USAGE,
					                           "IP index is too high, there is only %u IPs available\n",
					                           npu_get_nb_ip());

				test_ddr_debug(ip_idx);
			}
		}
	}

	vart_ml_log(LOG_INFO, "Test wrapping ...\n");
	if (npu_testwrapping() == vart_ml_error::SUCCESS)
		vart_ml_log(LOG_INFO, "No wrapping detected\n");
	else
		testOk = false;

	if (!testOk)
		return vart_ml_error::TEST_FAILURE;

	return vart_ml_error::SUCCESS;
}

static int get_config(uint32_t ip_idx, std::ostringstream* configs)
{
	int err = vart_ml_error::SUCCESS;

	err += npu_get_fpgafamily(&fpgaFamily);

	err += npu_get_architecture(&arch);

	*configs << "board name           : " << npu_get_boardname(ip_idx) << std::endl;
	*configs << "boards               : " << 1 << std::endl;

	if (arch == AIEML_V1C)
	{
		*configs << "AIE                  : " << npu_get_nbcolumns(ip_idx) * npu_get_nbaiepercolumn(ip_idx)
		         << " = " << npu_get_nbcolumns(ip_idx) << "x" << npu_get_nbaiepercolumn(ip_idx) << std::endl;
	}
	else
	{
		*configs << "systems              : " << npu_get_nbsystems(ip_idx) << std::endl;
		*configs << "cores                : " << npu_get_nbcores(ip_idx) << std::endl;
		*configs << "nces                 : " << npu_get_nbnces(ip_idx) << std::endl;
	}

	*configs << "nominal frequency    : " << npu_get_runningfreq(ip_idx) << std::endl;

	if (!is_mmcm_en(ip_idx))
		*configs << "run frequency        : " << npu_get_runningfreq(ip_idx) << std::endl;
	else
	{
		*configs << "run frequency        : ";

		float freq;
		if (npu_read_mmcm_freq(ip_idx, &freq) != vart_ml_error::SUCCESS)
			err++;
		else
			*configs << (uint)freq;
		*configs << std::endl;
	}

	std::ostringstream cores;
	std::string        rv;
	for (size_t s = 0; s < npu_get_nbsystems(ip_idx); s++)
		for (size_t c = 0; c < npu_get_nbcores(ip_idx); c++)
			cores << "1,";
	rv = cores.str();
	if (!rv.empty())
		rv.pop_back();

	if (arch != AIEML_V1C)
	{
		*configs << "enabled cores        : " << rv << std::endl;
		*configs << "split core           : " << is_splitCore_en(ip_idx) << std::endl;
	}

	*configs << "fpga family          : ";
	if (fpgaFamily == VERSAL)
		*configs << "VERSAL";
	else if (fpgaFamily == ZYNQ)
		*configs << "ZYNQ";
	*configs << std::endl;

	*configs << "backend              : ";
	if (arch == V1)
		*configs << "US";
	else if (arch == V2)
		*configs << "AIE";
	else if (arch == AIEML_V1C)
		*configs << "AIEML_V1C";

	return err;
}

int config(int argc, const char** argv)
{
	int err = npu_connect_peripherals();
	if (err)
		return err;

	if (argc == 1)
	{
		for (size_t i = 0; i < npu_get_nb_ip(); i++)
		{
			err = npu_connect_ip(i);
			if (err)
				return err;

			std::ostringstream configs;

			err = get_config(i, &configs);
			if (err)
				return err;

			vart_ml_log(LOG_INFO, "Configuration IP %zu:\n%s\n\n", i, configs.str().c_str());
		}
	}
	else if (argc == 2)
	{
		uint32_t ip_idx = parse_uint(argv[1]);
		if (ip_idx >= npu_get_nb_ip())
			return vart_ml_log_err_msg(vart_ml_error::TOOLS_BAD_USAGE,
			                           "IP index is too high, there is only %u IPs available\n",
			                           npu_get_nb_ip());

		err = npu_connect_ip(ip_idx);
		if (err)
			return err;

		std::ostringstream configs;

		err = get_config(ip_idx, &configs);
		if (err)
			return err;

		vart_ml_log(LOG_INFO, "%s\n", configs.str().c_str());
	}
	else
		return vart_ml_log_err_msg(
		    vart_ml_error::TOOLS_BAD_USAGE, "Usage: vart_ml_tools %s [ip_idx].\n", argv[0]);

	return vart_ml_error::SUCCESS;
}

int fpgaStatus(int argc, const char** argv)
{
	int err;

	if (argc == 1)
	{
		for (size_t i = 0; i < npu_get_nb_ip(); i++)
		{
			err = npu_connect_ip(i);
			if (err)
				return err;

			uint32_t timestamp;
			npu_read_cfg(TIMESTAMP_OFFSET, &timestamp, 1, i);

			vart_ml_log(LOG_INFO, "\tTimestamp");

			if (npu_get_nb_ip() != 1)
				vart_ml_log(LOG_INFO, " IP %zu", i);

			vart_ml_log(LOG_INFO, ": 0x%08x (", timestamp);

			err = print_fpga_info_path(timestamp);
			if (err)
				return err;

			vart_ml_log(LOG_INFO, ")\n");

			npu_print_fpga_info(i);

			vart_ml_log(LOG_INFO, "\n\n");
		}
	}
	else if (argc == 2)
	{
		uint32_t ip_idx = parse_uint(argv[1]);
		if (ip_idx >= npu_get_nb_ip())
			return vart_ml_log_err_msg(vart_ml_error::TOOLS_BAD_USAGE,
			                           "IP index is too high, there is only %u IPs available\n",
			                           npu_get_nb_ip());

		err = npu_connect_ip(ip_idx);
		if (err)
			return err;

		uint32_t timestamp;
		npu_read_cfg(TIMESTAMP_OFFSET, &timestamp, 1, ip_idx);

		vart_ml_log(LOG_INFO, "\tTimestamp");

		if (npu_get_nb_ip() != 1)
			vart_ml_log(LOG_INFO, " IP %zu", ip_idx);

		vart_ml_log(LOG_INFO, ": 0x%08x (", timestamp);

		int err = print_fpga_info_path(timestamp);
		if (err)
			return err;

		vart_ml_log(LOG_INFO, ")\n");

		npu_print_fpga_info(ip_idx);
	}
	else
		return vart_ml_log_err_msg(
		    vart_ml_error::TOOLS_BAD_USAGE, "Usage: vart_ml_tools %s [ip_idx].\n", argv[0]);

	return vart_ml_error::SUCCESS;
}

int print_resource_info(int argc, const char** argv)
{
	(void)argc;
	(void)argv;

	int err = npu_connect_peripherals();
	if (err)
		return err;

	for (size_t i = 0; i < npu_get_nb_ip(); i++)
	{
		err = npu_connect_ip(i);
		if (err)
			return err;
	}

	npu_print_resource_info();

	return vart_ml_error::SUCCESS;
}
