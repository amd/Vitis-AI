/**
 * @file ddr_debug.cpp
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

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iomanip>

#include "ddr_debug.h"
#include "io/io.h"
#include "utils/log.h"
#include "utils/npu_reg.h"
#include "utils/parsing_combinators.h"
#include "utils/shell.h"

/* HW sets this error bit (DDR_CTRL, reg 0x88, bit3) when the requested full
 * physical address does not map to any present DDR (out-of-range / rejected).
 * bit0=command, bit1=start, bit2=done are the existing DDR_CTRL bits.
 */
#define DDR_OUT_OF_RANGE_MASK (1 << 3)

/* Direction is 0 for write, 1 for read */
static int ddr_debug_start_access(uint32_t ip_idx, uint32_t direction)
{
	uint32_t x = (direction & DDR_COMMAND_MASK) | DDR_START_MASK;
	npu_write_cfg(DDR_CTRL, &x, 1, ip_idx);

	uint32_t timeout = 0;
	do
	{
		npu_read_cfg(DDR_CTRL, &x, 1, ip_idx);
		timeout++;
	} while ((x & DDR_STATUS_MASK) == 0 && timeout < DDR_TIMEOUT);

	if (timeout >= DDR_TIMEOUT)
		return vart_ml_log_err_msg(vart_ml_error::DEVICE_DDR_READ_TIMEOUT,
		                           "Timeout: the DDR debug module is not working.\n");

	/* Read the status register once more and check the out_of_range error bit.
	 * If HW rejected the request, the full physical address did not map to any
	 * present DDR: fail this transaction rather than treating it as a pass.
	 */
	npu_read_cfg(DDR_CTRL, &x, 1, ip_idx);
	if (x & DDR_OUT_OF_RANGE_MASK)
		return vart_ml_log_err_msg(vart_ml_error::DEVICE_BAD_ADDR_ALIGNMENT,
		                           "The DDR debug module rejected the request: address is "
		                           "out-of-range (does not map to any present DDR).\n");

	return vart_ml_error::SUCCESS;
}

int ddr_debug_read(uint32_t ip_idx, uint64_t offset, uint32_t* buf)
{
	if (offset % 16 != 0)
		return vart_ml_log_err_msg(vart_ml_error::DEVICE_BAD_ADDR_ALIGNMENT,
		                           "The DDR debug module requires offsets that are aligned to 512 bits.\n");

	/* Write the full 64-bit physical address: reg 0x80 = ddr_addr[31:0],
	 * reg 0x84 = ddr_addr[63:32]. HW uses ddr_addr[41:40] to select the DDR.
	 */
	uint32_t addr[2] = { (uint32_t)(offset & 0xffffffffull), (uint32_t)(offset >> 32) };
	npu_write_cfg(DDR_ADDR, addr, 2, ip_idx);
	int err = ddr_debug_start_access(ip_idx, 1);
	if (err)
		return err;

	npu_read_cfg(DDR_RD_DATA, buf, 64 / sizeof(uint32_t), ip_idx);

	return vart_ml_error::SUCCESS;
}

int ddr_debug_write(uint32_t ip_idx, uint64_t offset, uint32_t* buf)
{
	if (offset % 16 != 0)
		return vart_ml_log_err_msg(vart_ml_error::DEVICE_BAD_ADDR_ALIGNMENT,
		                           "The DDR debug module requires offsets that are aligned to 512 bits\n.");

	/* Write the full 64-bit physical address: reg 0x80 = ddr_addr[31:0],
	 * reg 0x84 = ddr_addr[63:32]. HW uses ddr_addr[41:40] to select the DDR.
	 */
	uint32_t addr[2] = { (uint32_t)(offset & 0xffffffffull), (uint32_t)(offset >> 32) };
	npu_write_cfg(DDR_ADDR, addr, 2, ip_idx);
	npu_write_cfg(DDR_WR_DATA, buf, 64 / sizeof(uint32_t), ip_idx);

	return ddr_debug_start_access(ip_idx, 0);
}

int ddr_debug(int argc, const char** argv)
{
	uint32_t           buf[16];
	uint64_t           off;
	std::stringstream  val;
	std::ostringstream ddr;

	uint32_t ip_idx = parse_uint(argv[1]);
	if (ip_idx >= npu_get_nb_ip())
		return vart_ml_log_err_msg(vart_ml_error::TOOLS_BAD_USAGE,
		                           "IP index is too high, there is only %u IPs available\n",
		                           npu_get_nb_ip());

	int err = npu_connect_ip(ip_idx);
	if (err)
		return err;

	if (!is_debug_srv_ddr_en(ip_idx))
		return vart_ml_log_err_msg(vart_ml_error::DEVICE_UNSUPPORTED_FEATURE,
		                           "The DDR debug module is absent from this bitstream.\n");

	switch (argc)
	{
	case 3:
		off = parse_uint(argv[2]);
		err = ddr_debug_read(ip_idx, off, (uint32_t*)&buf);
		if (err)
			return err;

		ddr << std::hex << "0x";
		for (int i = 0; i < 16; i++)
			ddr << std::setfill('0') << std::setw(8) << buf[i];
		ddr << std::dec << std::endl;
		vart_ml_log(LOG_INFO, "%s", ddr.str().c_str());
		break;
	case 4:
		if (strlen(argv[3]) <= 2 || argv[3][0] != '0' || argv[3][1] != 'x')
			return vart_ml_log_err_msg(vart_ml_error::DEVICE_UNEXPECTED_ARG_VALUE,
			                           "This expects an hexadecimal value, with leading 0x\n");

		argv[3] += 2;

		val << std::setfill('0') << std::setw(128) << argv[3];
		read_uint512_t_hex(val, buf);

		off = parse_uint(argv[2]);
		err = ddr_debug_write(ip_idx, off, (uint32_t*)&buf);
		if (err)
			return err;

		break;
	default:
		return vart_ml_log_err_msg(vart_ml_error::TOOLS_BAD_USAGE,
		                           "Usage: vart_ml_tools %s <ip_idx> <addr> [<value to write>].\n",
		                           argv[0]);
	}

	return vart_ml_error::SUCCESS;
}
