/**
 * @file embedded_api_implem_devmem.cpp
 *
 * @copyright Copyright 2025 Advanced Micro Devices Inc.
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

#include <assert.h>
#include <cstdint>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
#include <string>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

#include "devmem.h"
#include "utils/fpga_info.h"
#include "utils/log.h"
#include "utils/npu_reg.h"
#include "utils/shell.h"

#define DEFAULT_IP_NAME "npu"
#define FAMILYPATH      "/proc/device-tree/model"

#define STRINGTOUL(x) (parse_uint(x))

static FpgaFamily       l_family;
static FpgaArchitecture l_arch;

static std::mutex devmem_mutex;
static int        npufd;
static bool       is_peripheral_ready = false;
static bool       is_arch_set         = false;

/* M_AXI_HPM0_FPD is 128-bit, but CTRLBus is 32-bit. Supporting accesses not
   aligned to 128 bits would be too costly, so we simply drop the two least
   significant bits of the offset. Therefore, CTRLBus offsets have to be
   multiplied by 4, which is done implicitly by pointer arithmetic when
   indexing ctrlbus_base since it is an uint32_t*.
*/

typedef struct
{
	volatile void* base_vaddr;
	size_t         phy_addr;
	size_t         size;
} mem_desc;

typedef struct
{
	uint32_t                           timestamp;
	std::string                        kernel_name;
	mem_desc                           ctrlbus;
	std::map<std::string, std::string> fpga_info;
	int32_t                            connect_count;
} ip_handle;

typedef struct
{
	size_t   ddr_idx;
	uint64_t offset;
} ddr_coordinates;

typedef struct
{
	uint64_t pid;
	uint64_t phy_addr;
	uint32_t size;
} ddr_block;

typedef struct
{
	mem_desc                         noc;
	std::vector<mem_desc>            ddr;
	std::vector<ip_handle>           ip;
	std::map<void*, ddr_coordinates> local_ddr_map;
	std::map<uint64_t, ddr_block>    ddr_map[MAXDDRS];
} DEVMEMHandle;

static std::uint32_t get_cell(const std::vector<std::uint8_t>& reg, size_t byte_offset)
{
	if (byte_offset + 4 > reg.size())
		throw std::runtime_error("Out of range access to device node reg attribute");
	return (std::uint32_t(reg[byte_offset + 0]) << 24) | (std::uint32_t(reg[byte_offset + 1]) << 16)
	       | (std::uint32_t(reg[byte_offset + 2]) << 8) | (std::uint32_t(reg[byte_offset + 3]) << 0);
}

static int get_devicetree_node_reg_cells(std::string path, std::uint64_t& addr, std::uint64_t& size)
{
	std::ifstream file(path + "/reg", std::ios::binary);
	if (!file)
		throw std::runtime_error("open failed: " + path);

	std::vector<std::uint8_t> reg_buffer((std::istreambuf_iterator<char>(file)),
	                                     std::istreambuf_iterator<char>());
	file.close();

	if (reg_buffer.size() != 16)
		throw std::runtime_error("Unexpected reg size: " + std::to_string(reg_buffer.size()));

	// Interpret as 2 address cells + 2 size cells (common on ARM64 buses)
	addr = (std::uint64_t(get_cell(reg_buffer, 0)) << 32) | get_cell(reg_buffer, 4);
	size = (std::uint64_t(get_cell(reg_buffer, 8)) << 32) | get_cell(reg_buffer, 12);

	return vart_ml_error::SUCCESS;
}

static bool starts_with(const std::string& s, const std::string& prefix)
{
	return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

static bool ends_with(const std::string& s, const std::string& suffix)
{
	return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

static int get_devicetree_fpgafamily(FpgaFamily* family)
{
	*family = ZYNQ;

	FILE* f;
	if ((f = fopen(FAMILYPATH, "r")) == NULL)
		return vart_ml_log_err_msg(FILE_ACCESS_OPEN_FAILURE, "Failed to open " FAMILYPATH ".\n");

	char s[64];
	if (fgets(s, sizeof(s), f) == NULL)
	{
		fclose(f);
		return vart_ml_log_err_msg(DEVICE_ARG_NOT_FOUND_IN_DEV_TREE,
		                           "Failed to retrieve fpga family from device tree.\n");
	}
	fclose(f);

	for (size_t i = 0; i < strlen(s); i++)
	{
		if (strncmp(s + i, "Versal", sizeof("Versal") - 1) == 0)
		{
			*family = VERSAL;
			return SUCCESS;
		}
		else if (strncmp(s + i, "ZynqMP", sizeof("ZynqMP") - 1) == 0)
		{
			*family = ZYNQ;
			return SUCCESS;
		}
	}

	return vart_ml_log_err_msg(CONFIG_UNEXPECTED_FPGA_FAMILY, "Unknown FPGA family.\n");
}

static int get_devicetree_npu_dev_node_attributes(DEVMEMHandle* devmem_handle)
{
	std::string devpath    = "/proc/device-tree";
	std::string symbolpath = "/proc/device-tree/__symbols__";

	/* key = filename, value = path */
	/* Note: using a map ensures scanning in key's alphabetic order */
	std::map<std::string, std::string> dev_nodes;

	/* Look for NPU IPs and noc */
	std::string prefix(DEFAULT_IP_NAME);
	if (l_family == VERSAL)
		prefix += "_versal";
	else
		prefix += "_zynqmp";

	for (const auto& entry : std::filesystem::directory_iterator(symbolpath))
		if (starts_with(entry.path().filename().string(), prefix))
		{
			const auto filename  = entry.path().filename().string();
			const auto separator = filename.rfind('_');

			/*
			 * Check that the suffix following the underscore is non-empty and
			 * it is either an IP id or "noc"
			 */
			if ((separator != std::string::npos) && (separator + 1 < filename.size()))
			{
				const std::string_view suffix{ filename.data() + separator + 1,
					                           filename.size() - (separator + 1) };

				/* Check if suffix is all digits */
				bool is_num = true;
				for (unsigned char ch : suffix)
					if (ch < '0' || ch > '9')
					{
						is_num = false;
						break;
					}

				/* If suffix is not numerical nor "noc", skip */
				if (!is_num && suffix != "noc")
					continue;

				/* Extract referenced file from symbol */
				std::ifstream file(entry.path());
				std::string   line;
				std::getline(file, line);

				/* Remove trailing null terminator character */
				line.pop_back();

				dev_nodes[filename] = devpath + line;
			}
		}

	/* Open file and extract dev node address and size */
	for (const auto& [filename, path] : dev_nodes)
	{
		if (ends_with(path, "noc"))
		{
			int err =
			    get_devicetree_node_reg_cells(path, devmem_handle->noc.phy_addr, devmem_handle->noc.size);
			if (err)
				return err;
		}
		else
		{
			ip_handle ip;
			ip.kernel_name = filename;
			int err        = get_devicetree_node_reg_cells(path, ip.ctrlbus.phy_addr, ip.ctrlbus.size);
			if (err)
				return err;

			devmem_handle->ip.push_back(std::move(ip));
		}
	}

	return vart_ml_error::SUCCESS;
}

static int get_devicetree_ddr_attributes(DEVMEMHandle* devmem_handle)
{
	/*
	 * Access reserved memory nodes for VART ML in all N DDRs(nbddrs) from below locations on filesystem
	 *
	 * Below entries for VEK280 where there are 3 DDRs
	 * /proc/device-tree/reserved-memory/ddr0-npu_<family>@0/reg
	 * /proc/device-tree/reserved-memory/ddr1-npu_<family>@0/reg
	 * /proc/device-tree/reserved-memory/ddr2-npu_<family>@0/reg
	 *
	 */

	/* Look for DDR */
	std::filesystem::path ddrpath = "/proc/device-tree/reserved-memory";
	std::string           prefix  = "ddr";
	std::string           suffix  = "-npu_";

	std::vector<std::string> ddr_nodes;

	if (l_family == VERSAL)
		suffix += "versal@0";
	else
		suffix += "zynqmp@0";

	/* Look for DDR nodes in device tree */
	for (const auto& entry : std::filesystem::directory_iterator(ddrpath))
	{
		const std::string filename = entry.path().filename().string();

		/* DDR node found, extract ddr attributes */
		if (starts_with(filename, prefix) && ends_with(filename, suffix))
			ddr_nodes.push_back(entry.path().string());
	}

	if (ddr_nodes.empty())
		return vart_ml_log_err_msg(vart_ml_error::DEVICE_BAD_DDR_INDEX,
		                           "No DDR node found in device tree.\n");

	/* Read ddr attributes from reg file */
	for (auto& path : ddr_nodes)
	{
		mem_desc ddr_desc;

		int err = get_devicetree_node_reg_cells(path, ddr_desc.phy_addr, ddr_desc.size);
		if (err)
			return err;

		devmem_handle->ddr.push_back(std::move(ddr_desc));
	}

	return vart_ml_error::SUCCESS;
}

int create_ip_context(void** handle)
{
	DEVMEMHandle* devmem_handle = new DEVMEMHandle;

	vart_ml_log(LOG_WARN,
	            "Warning: when using PS DDR for VART ML, using /dev/mem will result in memory corruption "
	            "unless the area VART ML uses is reserved with a reserved-memory device tree node or with "
	            "the mem/memmap kernel parameters.\n");

	int err = get_devicetree_fpgafamily(&l_family);
	if (err)
		return err;

	err = get_devicetree_npu_dev_node_attributes(devmem_handle);
	if (err)
		return err;

	err = get_devicetree_ddr_attributes(devmem_handle);
	if (err)
		return err;

	if ((npufd = open("/dev/mem", O_RDWR, O_SYNC)) == -1)
		return vart_ml_log_err_msg(FILE_ACCESS_OPEN_FAILURE, "Failed to open /dev/mem.\n");

	for (auto& ip : devmem_handle->ip)
	{
		ip.ctrlbus.base_vaddr = (volatile void*)mmap(
		    NULL, ip.ctrlbus.size, PROT_READ | PROT_WRITE, MAP_SHARED, npufd, ip.ctrlbus.phy_addr);
		if (ip.ctrlbus.base_vaddr == MAP_FAILED)
		{
			err = vart_ml_log_err_msg(SYSTEM_ERROR_MMAP_FAILURE, "Failed to mmap ctrlbus.\n");
			goto cleanup;
		}

		ip.timestamp     = ((uint32_t*)(ip.ctrlbus.base_vaddr))[0];
		ip.connect_count = 0;
	}

	*handle = devmem_handle;

	return vart_ml_error::SUCCESS;

cleanup:
	close(npufd);
	return err;
}

static int disconnect_ip_helper(ip_handle& ip)
{
	if (munmap((void*)ip.ctrlbus.base_vaddr, ip.ctrlbus.size))
		return vart_ml_log_err_msg(vart_ml_error::SYSTEM_ERROR_UNMAP_FAILURE, "munmap CtrlBus.\n");

	return vart_ml_error::SUCCESS;
}

int disconnect_ip(void* handle, uint32_t ip_idx)
{
	uint32_t nb_ip = get_nb_ip(handle);
	if (ip_idx >= nb_ip)
		return vart_ml_log_err_msg(vart_ml_error::CONFIG_UNEXPECTED_ARG_VALUE,
		                           "IP index (%u) exceeds IP count (%u)\n",
		                           ip_idx,
		                           nb_ip);

	DEVMEMHandle* devmem_handle = (DEVMEMHandle*)handle;
	auto&         ip            = devmem_handle->ip[ip_idx];

	if (ip.connect_count <= 0)
		return vart_ml_log_err_msg(vart_ml_error::CONFIG_UNEXPECTED_ARG_VALUE,
		                           "Unexpected IP connection counter (%d). Disconnect operation failed.\n",
		                           ip.connect_count);

	--ip.connect_count;

	return vart_ml_error::SUCCESS;
}

static int parse_ip_fpga_info(void* handle, uint32_t ip_idx)
{
	DEVMEMHandle* devmem_handle = (DEVMEMHandle*)handle;

	auto& ip = devmem_handle->ip[ip_idx];

	int err = parse_fpga_info(ip.timestamp, ip.fpga_info);
	if (err)
		return err;

	auto& fpga_info = ip.fpga_info;

	enum FpgaFamily family;
	std::string     family_info = fpga_info["general.family"];

	if (family_info == "VERSAL")
		family = VERSAL;
	else if ((family_info == "ULTRASCALE" || family_info == "USCALE" || family_info == "USCALE_PLUS"))
		family = ZYNQ;
	else
		return vart_ml_log_err_msg(vart_ml_error::CONFIG_UNEXPECTED_FPGA_FAMILY,
		                           "Unknown FPGA family %s (general.family).\n",
		                           family_info.c_str());

	// Check match between device tree and fpga info family.
	if (is_peripheral_ready && (family != l_family))
		return vart_ml_log_err_msg(
		    vart_ml_error::CONFIG_UNEXPECTED_FPGA_FAMILY,
		    "FPGA INFO family %s (general.family) differs from device tree family %s.\n",
		    family_info.c_str(),
		    stringFromFpgaFamily(l_family));

	// Set architecture
	if (!is_arch_set)
	{
		std::string arch_info = fpga_info["general.architecture"];
		if (arch_info != "V1" && arch_info != "V2" && arch_info != "AIEML_V1C")
			return vart_ml_log_err_msg(vart_ml_error::CONFIG_UNEXPECTED_ARCH_FAMILY,
			                           "Unknown FPGA architecture %s (general.architecture).\n",
			                           arch_info.c_str());

		l_arch      = stringToArch(arch_info.c_str());
		is_arch_set = true;
	}

	return vart_ml_error::SUCCESS;
}

int connect_ip(void* handle, uint32_t ip_idx)
{
	auto& ip = ((DEVMEMHandle*)handle)->ip[ip_idx];

	if (ip.fpga_info.empty())
	{
		int err = parse_ip_fpga_info(handle, ip_idx);
		if (err)
			return err;
	}

	ip.connect_count++;

	return vart_ml_error::SUCCESS;
}

int connect_peripherals(void* handle)
{
	DEVMEMHandle* devmem_handle = (DEVMEMHandle*)handle;

	// Mmap resources (ddr, noc, memtile) if not yet done.
	if (!is_peripheral_ready)
	{
		for (size_t i = 0; i < devmem_handle->ddr.size(); i++)
		{
			devmem_handle->ddr[i].base_vaddr = (volatile void*)mmap(NULL,
			                                                        devmem_handle->ddr[i].size,
			                                                        PROT_READ | PROT_WRITE,
			                                                        MAP_SHARED,
			                                                        npufd,
			                                                        devmem_handle->ddr[i].phy_addr);
			if (devmem_handle->ddr[i].base_vaddr == MAP_FAILED)
				return vart_ml_log_err_msg(SYSTEM_ERROR_MMAP_FAILURE, "Failed to mmap DDR %zu.\n", i);
		}

		if (l_family == VERSAL)
		{
			devmem_handle->noc.base_vaddr = (volatile void*)mmap(NULL,
			                                                     devmem_handle->noc.size,
			                                                     PROT_READ | PROT_WRITE,
			                                                     MAP_SHARED,
			                                                     npufd,
			                                                     devmem_handle->noc.phy_addr);
			if (devmem_handle->noc.base_vaddr == MAP_FAILED)
				return vart_ml_log_err_msg(SYSTEM_ERROR_MMAP_FAILURE, "Failed to mmap NOC.\n");
		}

		is_peripheral_ready = true;
	}

	return vart_ml_error::SUCCESS;
}

int get_ip_from_timestamp(void* handle, uint32_t timestamp, uint32_t* ip_idx)
{
	DEVMEMHandle* devmem_handle = (DEVMEMHandle*)handle;

	for (size_t i = 0; i < devmem_handle->ip.size(); i++)
		if (timestamp == devmem_handle->ip[i].timestamp)
		{
			*ip_idx = i;
			return vart_ml_error::SUCCESS;
		}

	return vart_ml_log_err_msg(vart_ml_error::CONFIG_TIMESTAMP_MATCH_NOT_FOUND,
	                           "Failed to find an IP which timestamp matches 0x%08x.\n",
	                           timestamp);
}

int get_timestamp_from_ip(void* handle, uint32_t ip_idx, uint32_t* timestamp)
{
	DEVMEMHandle* devmem_handle = (DEVMEMHandle*)handle;

	if (ip_idx >= devmem_handle->ip.size())
		return vart_ml_log_err_msg(vart_ml_error::DEVICE_UNEXPECTED_ARG_VALUE,
		                           "Requested IP index %u exceeds IP count (%zu).\n",
		                           ip_idx,
		                           devmem_handle->ip.size());

	*timestamp = devmem_handle->ip[ip_idx].timestamp;

	return vart_ml_error::SUCCESS;
}

int get_ip(void* handle, bool is_pp, uint32_t* ip_idx)
{
	if (is_pp)
		return vart_ml_log_err_msg(vart_ml_error::DEVICE_UNSUPPORTED_FEATURE,
		                           "Failed to find a PP IP in the device list.\n");

	if (get_nb_ip(handle) > 1)
		return vart_ml_log_err_msg(
		    vart_ml_error::DEVICE_UNEXPECTED_ARG_VALUE,
		    "There is more than one NPU IP in the device list. Unable to make selection.\n");

	*ip_idx = 0;
	return vart_ml_error::SUCCESS;
}

uint32_t get_nb_ip(void* handle)
{
	DEVMEMHandle* devmem_handle = (DEVMEMHandle*)handle;
	return devmem_handle->ip.size();
}

uint32_t get_nb_pp(void* handle)
{
	(void)handle;
	return 0;
}

int get_ip_is_pp(void* handle, uint32_t ip_idx, bool* is_pp)
{
	DEVMEMHandle* devmem_handle = (DEVMEMHandle*)handle;
	if (ip_idx >= devmem_handle->ip.size())
		return vart_ml_log_err_msg(vart_ml_error::CONFIG_UNEXPECTED_ARG_VALUE,
		                           "IP index (%u) exceeds IP count (%zu)\n",
		                           ip_idx,
		                           devmem_handle->ip.size());
	*is_pp = false;
	return vart_ml_error::SUCCESS;
}

uint32_t read_register(void* handle, uint32_t offset, uint32_t ip_idx)
{
	DEVMEMHandle* devmem_handle = (DEVMEMHandle*)handle;

	return ((uint32_t*)(devmem_handle->ip[ip_idx].ctrlbus.base_vaddr))[offset];
}

void write_register(void* handle, uint32_t offset, uint32_t data, uint32_t ip_idx)
{
	DEVMEMHandle* devmem_handle = (DEVMEMHandle*)handle;

	((uint32_t*)(devmem_handle->ip[ip_idx].ctrlbus.base_vaddr))[offset] = data;
}

/* VART ML's DDR doesn't like accesses that are not self-aligned. Normal PS DDR
 * accesses don't have this constraint, which is why memcpy will sometimes
 * use 64-bit transfers on offsets that are not aligned to 64 bits.
 */
int read_ddr(void* handle, struct addr addr, uint8_t* buf, uint32_t size)
{
	/* Memory access needs to be 64 bytes aligned */
	size_t r = size % 64;
	if (r != 0)
	{
		uint8_t tmp[64];
		if (size - r > 0)
		{
			/* Buf and offset are not aligned, can't use memcpy */
			if ((size_t)buf % sizeof(uint64_t) != 0)
				for (size_t i = 0; i < size; i++)
					buf[i] = ((uint8_t*)addr.ddr_vaddr)[addr.offset + i];
			else
			{
				memcpy(buf, (uint8_t*)addr.ddr_vaddr + addr.offset, size - r);
				memcpy(tmp, (uint8_t*)addr.ddr_vaddr + addr.offset + size - r, 64);
				if (get_phy_addr(handle, (void*)buf))
					memcpy(buf + size - r, tmp, 64);
				else
					memcpy(buf + size - r, tmp, r);
			}
		}
		else
		{
			memcpy(tmp, (uint8_t*)addr.ddr_vaddr + addr.offset - (addr.offset % 64), 64);
			memcpy(buf, tmp + (addr.offset % 64), r);
		}
	}
	else
		memcpy(buf, (uint8_t*)addr.ddr_vaddr + addr.offset, size);

	return vart_ml_error::SUCCESS;
}

int write_ddr(void* handle, struct addr addr, const uint8_t* buf, uint32_t size)
{
	(void)handle;
	/* Memory access needs to be 64 bytes aligned */
	size_t r = size % 64;
	if (r != 0 && size > 64)
		memcpy((uint8_t*)addr.ddr_vaddr + addr.offset, buf, size - r + 64);
	else
		memcpy((uint8_t*)addr.ddr_vaddr + addr.offset, buf, size);

	return vart_ml_error::SUCCESS;
}

void read_noc(void* handle, uint64_t offset, uint32_t* buf, uint32_t wordsize)
{
	DEVMEMHandle* devmem_handle = (DEVMEMHandle*)handle;

	uint32_t* src = (uint32_t*)(devmem_handle->noc.base_vaddr) + offset;
	for (size_t i = 0; i < wordsize; i++)
		*buf++ = *src++;
}

void write_noc(void* handle, uint64_t offset, const uint32_t* buf, uint32_t wordsize)
{
	DEVMEMHandle* devmem_handle = (DEVMEMHandle*)handle;

	uint32_t* dest = (uint32_t*)(devmem_handle->noc.base_vaddr) + offset;
	for (size_t i = 0; i < wordsize; i++)
		*dest++ = *buf++;
}

uint64_t get_phy_addr(void* handle, void* ddr_vaddr)
{
	DEVMEMHandle* devmem_handle = (DEVMEMHandle*)handle;

	std::map<void*, ddr_coordinates>::iterator local_ddr_itr = devmem_handle->local_ddr_map.find(ddr_vaddr);

	if (local_ddr_itr == devmem_handle->local_ddr_map.end())
		return 0;

	size_t   ddr_idx = local_ddr_itr->second.ddr_idx;
	uint64_t offset  = local_ddr_itr->second.offset;

	std::map<uint64_t, ddr_block>::iterator ddr_itr = devmem_handle->ddr_map[ddr_idx].find(offset);

	if (ddr_itr != devmem_handle->ddr_map[ddr_idx].end())
		return ddr_itr->second.phy_addr;

	return 0;
}

void* get_vaddr(void* handle, void* ddr_vaddr)
{
	(void)handle;
	return ddr_vaddr;
}

void* get_base_vaddr(void* handle, size_t ddrIdx)
{
	DEVMEMHandle* devmem_handle = (DEVMEMHandle*)handle;

	return (void*)devmem_handle->ddr[ddrIdx].base_vaddr;
}

static uint64_t get_nbuff_conf_val(void* handle, uint64_t phy_addr)
{
	if (!is_arch_set)
		throw std::runtime_error(vart_ml_error::exception_message(vart_ml_error::CONFIG_MISSING_FPGA_INFO));

	if (l_arch != AIEML_V1C)
	{
		for (size_t i = 0; i < get_nbddrs(handle); i++)
		{
			size_t base = get_extmemBaseAddr(handle, i);
			size_t len  = get_extmemlen(handle, i);
			if (base <= phy_addr && phy_addr < base + len)
				return (phy_addr - base) >> 6;
		}
		return (phy_addr - get_extmemBaseAddr(handle, 0)) >> 6;
	}

	return phy_addr;
}

int ddr_allocate(void* handle, uint64_t size, uint32_t ddr, struct addr* addr)
{
	if (!is_arch_set)
		throw std::runtime_error(vart_ml_error::exception_message(vart_ml_error::CONFIG_MISSING_FPGA_INFO));

	addr->ddr_vaddr      = ddr_malloc(handle, ddr, size);
	addr->offset         = 0;
	addr->nbuff_conf_val = 0;

	if (addr->ddr_vaddr == NULL)
	{
		addr->phy_addr = 0;
		return vart_ml_error::SYSTEM_ERROR_MEM_ALLOC_FAILURE;
	}
	else
	{
		addr->phy_addr       = get_phy_addr(handle, addr->ddr_vaddr);
		addr->nbuff_conf_val = get_nbuff_conf_val(handle, addr->phy_addr);
	}

	return vart_ml_error::SUCCESS;
}

int
ddr_allocate_sub(void* handle, struct addr* parent_addr, uint64_t offset, uint64_t size, struct addr* addr)
{
	addr->ddr_vaddr      = ddr_malloc_sub(handle, parent_addr->ddr_vaddr, offset, size);
	addr->offset         = 0;
	addr->nbuff_conf_val = 0;

	if (addr->ddr_vaddr == NULL)
	{
		addr->phy_addr = 0;
		return vart_ml_error::SYSTEM_ERROR_MEM_ALLOC_FAILURE;
	}
	else
	{
		addr->phy_addr       = get_phy_addr(handle, addr->ddr_vaddr);
		addr->nbuff_conf_val = get_nbuff_conf_val(handle, addr->phy_addr);
	}

	return vart_ml_error::SUCCESS;
}

// First-fit scan: returns the offset of the first gap in ddr_map[ddrIdx] large enough for size,
// or UINT64_MAX if none found.
static uint64_t ddr_find_free_offset(DEVMEMHandle* devmem_handle, size_t ddrIdx, size_t size)
{
	uint64_t candidate = 0;

	for (const auto& entry : devmem_handle->ddr_map[ddrIdx])
	{
		if (candidate + size <= entry.first)
			return candidate;
		candidate = entry.first + entry.second.size;
	}

	if (candidate + size <= get_extmemlen(devmem_handle, ddrIdx))
		return candidate;

	return UINT64_MAX;
}

void* ddr_malloc(void* handle, size_t ddrIdx, size_t size)
{
	DEVMEMHandle* devmem_handle = (DEVMEMHandle*)handle;

	std::lock_guard<std::mutex> malloc_mutex_lock(devmem_mutex);

	uint64_t offset = ddr_find_free_offset(devmem_handle, ddrIdx, size);

	if (offset == UINT64_MAX)
	{
		vart_ml_log(LOG_ERR,
		            "Error: Not enough space available in DDR %zu to allocate a buffer of %zu bytes.\n",
		            ddrIdx,
		            size);
		return NULL;
	}

	void* vaddr = (uint8_t*)devmem_handle->ddr[ddrIdx].base_vaddr + offset;

	ddr_coordinates new_coordinates;

	new_coordinates.ddr_idx = ddrIdx;
	new_coordinates.offset  = offset;

	devmem_handle->local_ddr_map[vaddr] = new_coordinates;

	ddr_block new_block;

	new_block.phy_addr = get_extmemBaseAddr(handle, ddrIdx) + offset;
	new_block.size     = (size + (1 << 8) - 1) & ~((1 << 8) - 1); // 256 byte aligned
	new_block.pid      = getpid();

	devmem_handle->ddr_map[ddrIdx][offset] = new_block;

	return vaddr;
}

void* ddr_malloc_sub(void* handle, void* parent_vaddr, size_t offset, size_t size)
{
	DEVMEMHandle* devmem_handle = (DEVMEMHandle*)handle;

	std::lock_guard<std::mutex> malloc_mutex_lock(devmem_mutex);

	std::map<void*, ddr_coordinates>::iterator parent_itr = devmem_handle->local_ddr_map.find(parent_vaddr);

	if (parent_itr == devmem_handle->local_ddr_map.end())
	{
		vart_ml_log(LOG_ERR, "Parent vaddr not found in local_ddr_map.\n");
		return NULL;
	}

	size_t ddr_idx    = parent_itr->second.ddr_idx;
	size_t parent_off = parent_itr->second.offset;

	std::map<uint64_t, ddr_block>::iterator parent_block_itr =
	    devmem_handle->ddr_map[ddr_idx].find(parent_off);
	if (parent_block_itr == devmem_handle->ddr_map[ddr_idx].end())
	{
		vart_ml_log(LOG_ERR, "Parent buffer not found in ddr_map.\n");
		return NULL;
	}

	if (offset + size > parent_block_itr->second.size)
	{
		vart_ml_log(LOG_ERR, "Sub-buffer [offset=%zu, size=%zu] overflows parent buffer.\n", offset, size);
		return NULL;
	}

	void* sub_vaddr = (uint8_t*)parent_vaddr + offset;

	ddr_coordinates new_coordinates;
	new_coordinates.ddr_idx = ddr_idx;
	new_coordinates.offset  = parent_off + offset;

	devmem_handle->local_ddr_map[sub_vaddr] = new_coordinates;

	// Only insert a new ddr_map entry when it doesn't collide with an existing one.
	// When offset==0, parent_off+offset==parent_off is already present; reuse it.
	if (!devmem_handle->ddr_map[ddr_idx].contains(parent_off + offset))
	{
		ddr_block new_block;
		new_block.phy_addr = get_extmemBaseAddr(handle, ddr_idx) + parent_off + offset;
		new_block.size     = (size + (1 << 8) - 1) & ~((1 << 8) - 1);
		new_block.pid      = getpid();

		devmem_handle->ddr_map[ddr_idx][parent_off + offset] = new_block;
	}

	return sub_vaddr;
}

int ddr_free(void* handle, void* ddr_vaddr)
{
	DEVMEMHandle* devmem_handle = (DEVMEMHandle*)handle;

	std::lock_guard<std::mutex> free_mutex_lock(devmem_mutex);

	std::map<void*, ddr_coordinates>::iterator local_ddr_itr = devmem_handle->local_ddr_map.find(ddr_vaddr);

	if (local_ddr_itr == devmem_handle->local_ddr_map.end())
		return vart_ml_error::SUCCESS;

	size_t   ddr_idx = local_ddr_itr->second.ddr_idx;
	uint64_t offset  = local_ddr_itr->second.offset;

	std::map<uint64_t, ddr_block>::iterator ddr_itr = devmem_handle->ddr_map[ddr_idx].find(offset);

	if (ddr_itr != devmem_handle->ddr_map[ddr_idx].end())
		devmem_handle->ddr_map[ddr_idx].erase(ddr_itr);

	devmem_handle->local_ddr_map.erase(local_ddr_itr);

	return vart_ml_error::SUCCESS;
}

void ddr_sync_to_device(void* handle, void* buffer)
{
	(void)handle;
	(void)buffer;
}

void ddr_sync_from_device(void* handle, void* buffer)
{
	(void)handle;
	(void)buffer;
}

int ddr_export_buffer(void* handle, void* buffer)
{
	(void)handle;
	(void)buffer;
	return -1;
}

int wait_interrupt(void* handle, uint32_t ip_idx)
{
	(void)handle;
	(void)ip_idx;
	return vart_ml_error::SUCCESS;
}

int destroy_ip_context(void* handle)
{
	DEVMEMHandle* devmem_handle = (DEVMEMHandle*)handle;

	int err = vart_ml_error::SUCCESS;

	for (size_t ip_idx = 0; ip_idx < devmem_handle->ip.size(); ip_idx++)
		err += disconnect_ip_helper(devmem_handle->ip[ip_idx]);

	// Free all allocated ddr blocks
	for (auto local_ddr_itr = devmem_handle->local_ddr_map.begin();
	     local_ddr_itr != devmem_handle->local_ddr_map.end();)
	{
		size_t   ddr_idx = local_ddr_itr->second.ddr_idx;
		uint64_t offset  = local_ddr_itr->second.offset;

		auto ddr_itr = devmem_handle->ddr_map[ddr_idx].find(offset);
		if (ddr_itr != devmem_handle->ddr_map[ddr_idx].end())
			devmem_handle->ddr_map[ddr_idx].erase(ddr_itr);

		local_ddr_itr = devmem_handle->local_ddr_map.erase(local_ddr_itr);
	}

	for (size_t i = 0; i < get_nbddrs(handle); i++)
		if (munmap((void*)devmem_handle->ddr[i].base_vaddr, get_extmemlen(handle, i)))
			return vart_ml_log_err_msg(SYSTEM_ERROR_UNMAP_FAILURE, "munmap VART ML DDR.\n");

	if (devmem_handle->noc.base_vaddr != NULL
	    && munmap((void*)devmem_handle->noc.base_vaddr, devmem_handle->noc.size))
		return vart_ml_log_err_msg(SYSTEM_ERROR_UNMAP_FAILURE, "munmap NOC.\n");

	if (close(npufd))
		return vart_ml_log_err_msg(FILE_ACCESS_CLOSE_FAILURE, "Can not close /dev/mem.\n");

	if (devmem_handle)
		delete devmem_handle;

	return err;
}

void print_fpga_info(void* handle, uint32_t ip_idx)
{
	DEVMEMHandle* devmem_handle = (DEVMEMHandle*)handle;
	auto&         fpga_info     = devmem_handle->ip[ip_idx].fpga_info;

	if (fpga_info.empty())
		throw std::runtime_error(vart_ml_error::exception_message(vart_ml_error::CONFIG_MISSING_FPGA_INFO));

	return print_fpga_info(fpga_info);
}

unsigned int get_ip_fpga_info_attribute(void* handle, uint32_t ip_idx, char* attr)
{
	DEVMEMHandle* devmem_handle = (DEVMEMHandle*)handle;
	auto&         fpga_info     = devmem_handle->ip[ip_idx].fpga_info;

	if (fpga_info.empty())
		throw std::runtime_error(vart_ml_error::exception_message(vart_ml_error::CONFIG_MISSING_FPGA_INFO));

	std::string str(attr);
	return STRINGTOUL(fpga_info[str]);
}

char* get_boardname(void* handle, uint32_t ip_idx)
{
	DEVMEMHandle* devmem_handle = (DEVMEMHandle*)handle;
	auto&         fpga_info     = devmem_handle->ip[ip_idx].fpga_info;

	if (fpga_info.empty())
		throw std::runtime_error(vart_ml_error::exception_message(vart_ml_error::CONFIG_MISSING_FPGA_INFO));

	static char boardname[64];
	std::snprintf(boardname, sizeof(boardname), "%s", fpga_info["general.boardName"].c_str());
	return boardname;
}

size_t get_extmemlen(void* handle, size_t extmemIdx)
{
	if (!is_peripheral_ready)
		throw std::runtime_error(vart_ml_error::exception_message(vart_ml_error::CONFIG_MISSING_PERIPHERALS));

	DEVMEMHandle* devmem_handle = (DEVMEMHandle*)handle;

	return devmem_handle->ddr[extmemIdx].size;
}

size_t get_ddr_free_bytes(void* handle, size_t ddrIdx)
{
	if (!is_peripheral_ready)
		throw std::runtime_error(vart_ml_error::exception_message(vart_ml_error::CONFIG_MISSING_PERIPHERALS));

	DEVMEMHandle* devmem_handle = (DEVMEMHandle*)handle;

	std::lock_guard<std::mutex> lock(devmem_mutex);

	size_t   total      = get_extmemlen(handle, ddrIdx);
	size_t   free_bytes = 0;
	uint64_t cursor     = 0;

	for (const auto& entry : devmem_handle->ddr_map[ddrIdx])
	{
		if (entry.first > cursor)
			free_bytes += entry.first - cursor;
		cursor = entry.first + entry.second.size;
	}
	if (total > cursor)
		free_bytes += total - cursor;

	return free_bytes;
}

size_t get_noclen(void* handle)
{
	if (!is_peripheral_ready)
		throw std::runtime_error(vart_ml_error::exception_message(vart_ml_error::CONFIG_MISSING_PERIPHERALS));

	return ((DEVMEMHandle*)handle)->noc.size;
}

size_t get_nbddrs(void* handle)
{
	if (!is_peripheral_ready)
		throw std::runtime_error(vart_ml_error::exception_message(vart_ml_error::CONFIG_MISSING_PERIPHERALS));

	DEVMEMHandle* devmem_handle = (DEVMEMHandle*)handle;

	return devmem_handle->ddr.size();
}

size_t get_nbextmems(void* handle)
{
	if (!is_peripheral_ready)
		throw std::runtime_error(vart_ml_error::exception_message(vart_ml_error::CONFIG_MISSING_PERIPHERALS));

	return get_nbddrs(handle);
}

size_t get_extmemBaseAddr(void* handle, size_t extmemIdx)
{
	if (!is_peripheral_ready)
		throw std::runtime_error(vart_ml_error::exception_message(vart_ml_error::CONFIG_MISSING_PERIPHERALS));

	DEVMEMHandle* devmem_handle = (DEVMEMHandle*)handle;

	return devmem_handle->ddr[extmemIdx].phy_addr;
}

int get_fpgafamily(void* handle, enum FpgaFamily* family)
{
	(void)handle;

	if (!is_peripheral_ready)
		return vart_ml_error::CONFIG_MISSING_PERIPHERALS;

	*family = l_family;
	return vart_ml_error::SUCCESS;
}

int get_architecture(void* handle, enum FpgaArchitecture* arch)
{
	(void)handle;

	if (!is_arch_set)
		return vart_ml_error::CONFIG_MISSING_FPGA_INFO;

	*arch = l_arch;

	return vart_ml_error::SUCCESS;
}

void print_resource_info(void* handle)
{
	DEVMEMHandle* devmem_handle = (DEVMEMHandle*)handle;

	// Print top border
	printf("╔═══════════════════════════════════════════════════════════════════════════════════════╗\n");

	// Print mode title
	printf("║%-87s║\n", "                                   DEVMEM MODE");

	// Print separator after title
	printf("╠══════════════════════╦════════════════════╦════════════════════╦══════════════════════╣\n");

	// Print table header
	printf("║ %-20s ║ %-18s ║ %-18s ║ %-20s ║\n", "NAME", "ADDRESS", "SIZE", "ATTRIBUTE");

	// Print separator
	printf("╠══════════════════════╬════════════════════╬════════════════════╬══════════════════════╣\n");

	// Print DDR resources
	for (size_t i = 0; i < devmem_handle->ddr.size(); i++)
	{
		char name[32];
		snprintf(name, sizeof(name), "ddr_%zu", i);
		printf("║ %-20s ║ 0x%016zx ║ 0x%016zx ║ %-20s ║\n",
		       name,
		       devmem_handle->ddr[i].phy_addr,
		       devmem_handle->ddr[i].size,
		       "-");
	}

	// Print NOC resource
	if (devmem_handle->noc.size > 0)
	{
		printf("║ %-20s ║ 0x%016zx ║ 0x%016zx ║ %-20s ║\n",
		       "noc",
		       devmem_handle->noc.phy_addr,
		       devmem_handle->noc.size,
		       "-");
	}

	// Print IP resources
	for (size_t i = 0; i < devmem_handle->ip.size(); i++)
	{
		char ip_name[32];
		snprintf(ip_name, sizeof(ip_name), "ip_%zu", i);

		// Print IP header
		printf("║ %-20s ║ %-18s ║ %-18s ║ %-20s ║\n", ip_name, "-", "-", "-");

		// Print kernel_name
		printf("║   %-18s ║ %-18s ║ %-18s ║ %-20s ║\n",
		       "kernel_name",
		       "-",
		       "-",
		       devmem_handle->ip[i].kernel_name.c_str());

		// Print timestamp
		char timestamp_str[32];
		snprintf(timestamp_str, sizeof(timestamp_str), "0x%08x", devmem_handle->ip[i].timestamp);
		printf("║   %-18s ║ %-18s ║ %-18s ║ %-20s ║\n", "timestamp", "-", "-", timestamp_str);

		// Print ctrlbus
		printf("║   %-18s ║ 0x%016zx ║ 0x%016zx ║ %-20s ║\n",
		       "ctrlbus",
		       devmem_handle->ip[i].ctrlbus.phy_addr,
		       devmem_handle->ip[i].ctrlbus.size,
		       "-");
	}

	// Print bottom border
	printf("╚══════════════════════╩════════════════════╩════════════════════╩══════════════════════╝\n");
}
