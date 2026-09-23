/**
 * @file embedded_api_implem_xrt.cpp
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

#include <assert.h>
#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
#include <sstream>
#include <vector>

#ifdef NPU_XRT_ENABLE
#include <xrt/experimental/xrt_ip.h>
#include <xrt/experimental/xrt_xclbin.h>
#include <xrt/xrt_bo.h>
#include <xrt/xrt_device.h>
#include <xrt/xrt_hw_context.h>
#endif

#include "utils/fpga_info.h"
#include "utils/log.h"
#include "utils/shell.h"
#include "xrt.h"

#define FAMILYPATH              "/proc/device-tree/model"
#define DEFAULT_IP_NAME         "npu"
#define DEFAULT_XCLBIN_LOCATION "/run/media/mmcblk0p1/x_plus_ml.xclbin"
#define INTERRUPT_TIMEOUT       5000 // In ms

#define STRINGTOUL(x) (parse_uint(x))

#ifdef NPU_XRT_ENABLE

using namespace std;

constexpr uint64_t MC_AIE_BASE_ADDR[] = { 0x000800000000ULL, 0x00c000000000ULL, 0x010000000000ULL,
	                                      0x050000000000ULL, 0x058000000000ULL, 0x060000000000ULL,
	                                      0x068000000000ULL, 0x070000000000ULL, 0x078000000000ULL };

static FpgaFamily            l_family;
static enum FpgaArchitecture l_arch;
static std::mutex            xrt_mutex;
static bool                  is_peripheral_ready = false;
static bool                  is_arch_set         = false;

typedef struct
{
	uint32_t                           timestamp;
	std::string                        kernel_name;
	xrt::ip*                           ip_handle;
	xrt::ip::interrupt*                ip_interrupt;
	bool                               is_pp;
	std::map<std::string, std::string> fpga_info;
	int32_t                            connect_count;
} ip_handle;

typedef struct
{
	xrt::device*                            device;
	xrt::hw_context*                        hw_context;
	std::vector<ip_handle>                  ip;
	std::vector<uint32_t>                   mem_bnk_idxs;
	std::vector<std::map<uint64_t, size_t>> bo_addr_map; /* per bank: physical_addr -> size */
	xrt::xclbin*                            xclbin_handle;
	std::map<void*, xrt::bo*>               bo_map;
	std::map<void*, xrt::bo*>               sub_bo_map;
	std::map<xrt::bo*, size_t>              bo_ddr_map;
} XrtHandle;

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

static int get_npu_memory_bnk_indexes(void* handle, std::vector<uint32_t>& mem_indexes)
{
	XrtHandle* xrt_handle = (XrtHandle*)handle;

	std::vector<xrt::xclbin::mem> mem_lists = xrt_handle->xclbin_handle->get_mems();

	const char* ddr_mapping_info;
	if (get_user_config("xrt.ddr_mapping", &ddr_mapping_info) == vart_ml_error::SUCCESS
	    && ddr_mapping_info != NULL)
	{
		std::istringstream ss(ddr_mapping_info);
		std::string        token;
		while (std::getline(ss, token, ':'))
		{
			try
			{
				mem_indexes.push_back(static_cast<uint32_t>(std::stoul(token)));
			}
			catch (const std::exception&)
			{
				return vart_ml_log_err_msg(vart_ml_error::CONFIG_UNEXPECTED_ARG_VALUE,
				                           "Invalid xrt.ddr_mapping value \"%s\": expected format "
				                           "\"mem_idx_0:mem_idx_1:mem_idx_2:...\" "
				                           "where each mem_idx is an integer.\n",
				                           ddr_mapping_info);
			}
		}
		return vart_ml_error::SUCCESS;
	}

	std::map<uint64_t, std::pair<uint32_t, uint64_t>> mem_per_base; // base_addr -> (index, size_kb)

	for (auto& mem : mem_lists)
	{
		/*
		 * We know that, in the XCLBin file, the region reserved for linux has a base address of 0x0 and then
		 * the remain DDR regions coming next. Thus, we always skip this region.
		 */
		if (mem.get_base_address() == UINT64_MAX || mem.get_base_address() == 0 || mem.get_used() == false)
			continue;

		/*
		 * The memory list returned by XCLBin get_mem() call contains about a lot of entries but we are only
		 * interested in the DDR memories.
		 */
		uint64_t base_addr = mem.get_base_address();

		if (l_family == VERSAL)
		{
			bool match = false;
			for (auto& mc_base_addr : MC_AIE_BASE_ADDR)
				if ((base_addr & mc_base_addr) == mc_base_addr)
				{
					match = true;
					break;
				}
			if (!match)
				continue;
		}

		uint64_t size_kb = mem.get_size_kb();
		auto     it      = mem_per_base.find(base_addr);

		if (it == mem_per_base.end() || size_kb > it->second.second)
			mem_per_base[base_addr] = { mem.get_index(), size_kb };
	}

	for (auto& [base_addr, entry] : mem_per_base)
		mem_indexes.push_back(entry.first);

	return vart_ml_error::SUCCESS;
}

static xrt::bo* get_bo_or_sub(XrtHandle* xrt_handle, void* ddr_vaddr)
{
	std::map<void*, xrt::bo*>::iterator bo_itr;

	if ((bo_itr = xrt_handle->bo_map.find(ddr_vaddr)) != xrt_handle->bo_map.end())
		return bo_itr->second;

	if ((bo_itr = xrt_handle->sub_bo_map.find(ddr_vaddr)) != xrt_handle->sub_bo_map.end())
		return bo_itr->second;

	return nullptr;
}

/* Retrieve all IP infos and store it in handle. Disconnect from IP after that. */
int create_ip_context(void** handle)
{
	XrtHandle*  xrt_handle = new XrtHandle;
	char*       xclbin_loc = getenv("NPU_XCLBIN_PATH");
	std::string xclbin_path;

	// Set family to dummy value
	l_family = FpgaFamily::FPGA_FAMILY_COUNT;

	/*
	 * If fpga family was specified by user via env var, try to use it.
	 */
	const char* family_info;
	if (get_user_config("fpga.family", &family_info) == vart_ml_error::SUCCESS && family_info != NULL)
	{
		for (int i = 0; i < static_cast<int>(FpgaFamily::FPGA_FAMILY_COUNT); i++)
		{
			FpgaFamily f = static_cast<FpgaFamily>(i);

			if (strcmp(family_info, stringFromFpgaFamily(f)) == 0)
			{
				l_family = f;
				break;
			}
		}

		if (l_family == FpgaFamily::FPGA_FAMILY_COUNT)
			return vart_ml_log_err_msg(
			    vart_ml_error::CONFIG_UNEXPECTED_FPGA_FAMILY, "Unknown FPGA family %s.\n", family_info);
	}

	/* Fall back to device tree to get fpga family if not specified by user. */
	if (l_family == FpgaFamily::FPGA_FAMILY_COUNT)
	{
		int err = get_devicetree_fpgafamily(&l_family);
		if (err)
			return err;
	}

	if (!xclbin_loc)
	{
		xclbin_path.assign(DEFAULT_XCLBIN_LOCATION);
	}
	else
	{
		xclbin_path.assign(xclbin_loc);
	}

	/* Right now in VEK 280 case, its always 0th device */
	xrt_handle->device = new xrt::device(0);

	/* Check if the npuip_top_wrapper kernel is present in the xclbin
	 * before creating its instance. */
	xrt_handle->xclbin_handle = new xrt::xclbin(xclbin_path);
	if (xrt_handle->xclbin_handle->get_kernels().empty())
	{
		*handle = NULL;
		delete xrt_handle->xclbin_handle;
		delete xrt_handle->device;
		delete xrt_handle;
		return vart_ml_log_err_msg(XCLBIN_FILE_ERROR,
		                           "\"npuip_top_wrapper\" kernel/IP is not present in the xclbin\n");
	}

	xrt::uuid uuid;
	try
	{
		uuid = xrt_handle->device->register_xclbin(*xrt_handle->xclbin_handle);
	}
	catch (std::exception& ex)
	{
		delete xrt_handle->xclbin_handle;
		delete xrt_handle->device;
		delete xrt_handle;
		*handle = NULL;
		return vart_ml_log_err_msg(XCLBIN_FILE_ERROR, "Failed to load xclbin, reason : %s\n", ex.what());
	}

	/* Create hw context in exclusive mode to be able to manage IP interrupts. */
	xrt_handle->hw_context =
	    new xrt::hw_context(*xrt_handle->device, uuid, xrt::hw_context::access_mode::exclusive);

	/* Select the root name used to look for the IPs */
	std::string name(DEFAULT_IP_NAME);
	const char* info;
	if (get_user_config("ip.name", &info) == vart_ml_error::SUCCESS && info != NULL)
		name = info;

	/* Create the XRT IP instances for VART ML. */
	for (auto& kernel : xrt_handle->xclbin_handle->get_kernels())
	{
		if (kernel.get_name().find(name) != std::string::npos
		    || kernel.get_name().find("pp") != std::string::npos)
		{
			ip_handle tmp;
			tmp.kernel_name = kernel.get_name();

			try
			{
				tmp.ip_handle = new xrt::ip(*xrt_handle->hw_context, tmp.kernel_name);

				/* Get the timestamp of the ip */
				tmp.timestamp = tmp.ip_handle->read_register(0);
			}
			catch (const std::exception& e)
			{
				std::cerr << "[VART] " << e.what() << '\n';
				std::cerr << "[VART] Skip connection attempt to IP " << tmp.kernel_name << '\n';

				tmp.timestamp = 0;
				tmp.ip_handle = NULL;
			}

			if (kernel.get_name().find("pp") != std::string::npos)
				tmp.is_pp = true;
			else
				tmp.is_pp = false;

			/* Disconnect IP for now */
			if (tmp.ip_handle != NULL)
			{
				delete tmp.ip_handle;
				tmp.ip_handle = NULL;
			}

			tmp.connect_count = 0;
			xrt_handle->ip.push_back(tmp);
		}
	}

	/* check for ip_handle */
	*handle = xrt_handle;

	return vart_ml_error::SUCCESS;
}

static void disconnect_ip_helper(ip_handle& ip)
{
	if (ip.ip_handle)
		delete ip.ip_handle;

	if (ip.ip_interrupt)
		delete ip.ip_interrupt;
}

int disconnect_ip(void* handle, uint32_t ip_idx)
{
	uint32_t nb_ip = get_nb_ip(handle);
	if (ip_idx >= nb_ip)
		return vart_ml_log_err_msg(vart_ml_error::CONFIG_UNEXPECTED_ARG_VALUE,
		                           "IP index (%u) exceeds IP count (%u)\n",
		                           ip_idx,
		                           nb_ip);

	XrtHandle* xrt_handle = (XrtHandle*)handle;
	auto&      ip         = xrt_handle->ip[ip_idx];

	if (ip.connect_count <= 0)
		return vart_ml_log_err_msg(vart_ml_error::CONFIG_UNEXPECTED_ARG_VALUE,
		                           "Unexpected IP connection counter (%d). Disconnect operation failed.\n",
		                           ip.connect_count);

	if (--ip.connect_count == 0)
		disconnect_ip_helper(ip);

	return vart_ml_error::SUCCESS;
}

static int parse_ip_fpga_info(void* handle, uint32_t ip_idx)
{
	uint32_t nb_ip = get_nb_ip(handle);
	if (ip_idx >= nb_ip)
		return vart_ml_log_err_msg(
		    CONFIG_UNEXPECTED_ARG_VALUE, "IP index (%u) exceeds IP count (%u)\n", ip_idx, nb_ip);

	XrtHandle* xrt_handle = (XrtHandle*)handle;
	auto&      ip         = xrt_handle->ip[ip_idx];

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
	XrtHandle* xrt_handle = (XrtHandle*)handle;
	auto&      ip         = xrt_handle->ip[ip_idx];
	int        err;

	std::lock_guard<std::mutex> connect_ip_mutex_lock(xrt_mutex);

	uint32_t nb_ip = get_nb_ip(handle);
	if (ip_idx >= nb_ip)
	{
		err = vart_ml_log_err_msg(
		    CONFIG_UNEXPECTED_ARG_VALUE, "IP index (%u) exceeds IP count (%u)\n", ip_idx, nb_ip);
		goto cleanup;
	}

	if (ip.fpga_info.empty())
	{
		err = parse_ip_fpga_info(handle, ip_idx);
		if (err)
			goto cleanup;
	}

	if (++ip.connect_count > 1)
		return vart_ml_error::SUCCESS;

	ip.ip_handle = new xrt::ip(*xrt_handle->hw_context, ip.kernel_name);

	// Plug interrupt
	try
	{
		ip.ip_interrupt = new xrt::ip::interrupt(ip.ip_handle->create_interrupt_notify());
	}
	catch (std::exception& ex)
	{
		err = vart_ml_log_err_msg(
		    XRT_INTERRUPT_ERROR, "Failed to create interrupt notifier, reason : %s\n", ex.what());
		goto cleanup;
	}

	return vart_ml_error::SUCCESS;

cleanup:
	delete xrt_handle->device;

	for (auto& ip : xrt_handle->ip)
		delete ip.ip_handle;

	delete xrt_handle;
	handle = NULL;

	return err;
}

int connect_peripherals(void* handle)
{
	XrtHandle* xrt_handle = (XrtHandle*)handle;
	int        err;

	if (!is_peripheral_ready)
	{
		if (xrt_handle->mem_bnk_idxs.empty())
		{
			if (get_npu_memory_bnk_indexes(xrt_handle, xrt_handle->mem_bnk_idxs))
			{
				err = vart_ml_log_err_msg(XCLBIN_FILE_ERROR,
				                          "Failed to get memory bank indexes for creating XRT BOs\n");
				goto cleanup;
			}
			xrt_handle->bo_addr_map.resize(xrt_handle->mem_bnk_idxs.size());
		}

		is_peripheral_ready = true;
	}

	return vart_ml_error::SUCCESS;

cleanup:
	delete xrt_handle->device;

	for (auto& ip : xrt_handle->ip)
		delete ip.ip_handle;

	delete xrt_handle;
	handle = NULL;

	return err;
}

int get_ip_from_timestamp(void* handle, uint32_t timestamp, uint32_t* ip_idx)
{
	XrtHandle* xrt_handle = (XrtHandle*)handle;

	for (size_t i = 0; i < xrt_handle->ip.size(); i++)
		if (timestamp == xrt_handle->ip[i].timestamp)
		{
			*ip_idx = i;
			return vart_ml_error::SUCCESS;
		}

	return vart_ml_log_err_msg(
	    vart_ml_error::CONFIG_TIMESTAMP_MATCH_NOT_FOUND,
	    "Failed to find an IP which timestamp matches 0x%08x. IP is either busy or non-existent.\n",
	    timestamp);
}

int get_timestamp_from_ip(void* handle, uint32_t ip_idx, uint32_t* timestamp)
{
	XrtHandle* xrt_handle = (XrtHandle*)handle;

	if (ip_idx >= xrt_handle->ip.size())
		return vart_ml_log_err_msg(vart_ml_error::DEVICE_UNEXPECTED_ARG_VALUE,
		                           "Requested IP index %u exceeds IP count (%zu).\n",
		                           ip_idx,
		                           xrt_handle->ip.size());

	*timestamp = xrt_handle->ip[ip_idx].timestamp;

	return vart_ml_error::SUCCESS;
}

int get_ip(void* handle, bool is_pp, uint32_t* ip_idx)
{
	XrtHandle* xrt_handle = (XrtHandle*)handle;

	if (get_nb_ip(handle) > 1 || get_nb_pp(handle) > 1)
		return vart_ml_log_err_msg(
		    vart_ml_error::DEVICE_UNEXPECTED_ARG_VALUE,
		    "There is more than one %s IP in the device list. Unable to make selection.\n",
		    is_pp ? "PP" : "NPU");

	for (size_t i = 0; i < xrt_handle->ip.size(); i++)
		if (is_pp == xrt_handle->ip[i].is_pp)
		{
			*ip_idx = i;
			return vart_ml_error::SUCCESS;
		}

	return vart_ml_log_err_msg(vart_ml_error::DEVICE_UNSUPPORTED_FEATURE,
	                           "Failed to find %s IP in the device list.\n",
	                           is_pp ? "a PP" : "an NPU");
}

uint32_t get_nb_ip(void* handle)
{
	size_t nb_ip = 0;

	XrtHandle* xrt_handle = (XrtHandle*)handle;
	for (auto& ip : xrt_handle->ip)
		if (!ip.is_pp)
			nb_ip++;

	return nb_ip;
}

uint32_t get_nb_pp(void* handle)
{
	size_t nb_pp = 0;

	XrtHandle* xrt_handle = (XrtHandle*)handle;
	for (auto& ip : xrt_handle->ip)
		if (ip.is_pp)
			nb_pp++;

	return nb_pp;
}

int get_ip_is_pp(void* handle, uint32_t ip_idx, bool* is_pp)
{
	XrtHandle* xrt_handle = (XrtHandle*)handle;

	if (ip_idx >= xrt_handle->ip.size())
		return vart_ml_log_err_msg(vart_ml_error::CONFIG_UNEXPECTED_ARG_VALUE,
		                           "IP index (%u) exceeds IP count (%zu)\n",
		                           ip_idx,
		                           xrt_handle->ip.size());

	*is_pp = xrt_handle->ip[ip_idx].is_pp;
	return vart_ml_error::SUCCESS;
}

uint32_t read_register(void* handle, uint32_t offset, uint32_t ip_idx)
{
	XrtHandle* xrt_handle = (XrtHandle*)handle;
	assert(ip_idx < xrt_handle->ip.size());

	xrt::ip* ip = xrt_handle->ip[ip_idx].ip_handle;
	assert(ip != NULL);

	/* Offset here is the uint32_t word offset, XRT expects byte offset,
	 * where it will divide it by sizeof(uint32_t).
	 * Hence while sending to XRT, we are multiplying by size of uint32_t
	 */
	return ip->read_register(offset * sizeof(uint32_t));
}

void write_register(void* handle, uint32_t offset, uint32_t data, uint32_t ip_idx)
{
	XrtHandle* xrt_handle = (XrtHandle*)handle;
	assert(ip_idx < xrt_handle->ip.size());

	xrt::ip* ip = xrt_handle->ip[ip_idx].ip_handle;
	assert(ip != NULL);

	/* Offset here is the uint32_t word offset, XRT expects byte offset,
	 * where it will divide it by sizeof(uint32_t).
	 * Hence while sending to XRT, we are multiplying by size of uint32_t
	 */
	ip->write_register((offset * sizeof(uint32_t)), data);
}

int wait_interrupt(void* handle, uint32_t ip_idx)
{
	XrtHandle* xrt_handle = (XrtHandle*)handle;

	/* No need to clear the interrupt as its edge triggered. */
	std::cv_status status =
	    xrt_handle->ip[ip_idx].ip_interrupt->wait(std::chrono::milliseconds(INTERRUPT_TIMEOUT));

	if (status == std::cv_status::timeout)
		return -1;

	return vart_ml_error::SUCCESS;
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

	addr->ddr_vaddr      = static_cast<xrt::bo*>(ddr_malloc(handle, ddr, size))->map();
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
	addr->ddr_vaddr =
	    static_cast<xrt::bo*>(ddr_malloc_sub(handle, parent_addr->ddr_vaddr, offset, size))->map();
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

// First-fit scan: returns true if a contiguous gap >= size exists in DDR bank ddrIdx.
static bool xrt_has_gap(XrtHandle* xrt_handle, size_t ddrIdx, size_t size)
{
	std::vector<xrt::xclbin::mem> mem_lists = xrt_handle->xclbin_handle->get_mems();
	const xrt::xclbin::mem&       bank      = mem_lists[xrt_handle->mem_bnk_idxs[ddrIdx]];
	uint64_t                      bank_base = bank.get_base_address();
	uint64_t                      bank_end  = bank_base + bank.get_size_kb() * 1024;
	uint64_t                      candidate = bank_base;

	for (const auto& entry : xrt_handle->bo_addr_map[ddrIdx])
	{
		if (candidate + size <= entry.first)
			return true;
		candidate = entry.first + entry.second;
	}
	return candidate + size <= bank_end;
}

void* ddr_malloc(void* handle, size_t ddrIdx, size_t size)
{
	XrtHandle*   xrt_handle = (XrtHandle*)handle;
	xrt::device* device     = xrt_handle->device;
	xrt::bo*     bo_handle  = NULL;

	std::lock_guard<std::mutex> malloc_mutex_lock(xrt_mutex);

	if (!xrt_has_gap(xrt_handle, ddrIdx, size))
	{
		vart_ml_log(LOG_ERR,
		            "Error: Not enough space available in DDR %zu to allocate a buffer of %zu bytes.\n",
		            ddrIdx,
		            size);
		return NULL;
	}

	try
	{
		bo_handle = new xrt::bo(*device, size, xrt_handle->mem_bnk_idxs[ddrIdx]);
	}
	catch (std::exception& ex)
	{
		vart_ml_log(LOG_ERR, "Failed to allocate BO of size : %zu reason : %s\n", size, ex.what());
		return NULL;
	}

	// XRT silently allocates in another bank when the requested one is full instead of returning an error.
	// Verify the BO actually landed in the bank we asked for.
	std::vector<xrt::xclbin::mem> mem_lists = xrt_handle->xclbin_handle->get_mems();
	const xrt::xclbin::mem&       bank      = mem_lists[xrt_handle->mem_bnk_idxs[ddrIdx]];
	uint64_t                      bank_base = bank.get_base_address();
	uint64_t                      bank_end  = bank_base + bank.get_size_kb() * 1024;
	if (bo_handle->address() < bank_base || bo_handle->address() + size > bank_end)
	{
		vart_ml_log(LOG_ERR,
		            "Error: BO at 0x%lx of size %zu is outside DDR %zu [0x%lx, 0x%lx).\n",
		            bo_handle->address(),
		            size,
		            ddrIdx,
		            bank_base,
		            bank_end);
		delete bo_handle;
		return NULL;
	}

	xrt_handle->bo_addr_map[ddrIdx][bo_handle->address()] = size;
	xrt_handle->bo_map[bo_handle->map()]                  = bo_handle;
	xrt_handle->bo_ddr_map[bo_handle]                     = ddrIdx;

	return (void*)bo_handle;
}

void* ddr_malloc_sub(void* handle, void* parent_vaddr, size_t offset, size_t size)
{
	XrtHandle* xrt_handle = (XrtHandle*)handle;

	std::lock_guard<std::mutex> malloc_mutex_lock(xrt_mutex);

	std::map<void*, xrt::bo*>::iterator bo_itr = xrt_handle->bo_map.find(parent_vaddr);

	if (bo_itr == xrt_handle->bo_map.end())
	{
		vart_ml_log(LOG_ERR, "Parent vaddr not found in bo_map.\n");
		return NULL;
	}

	xrt::bo* parent_bo = bo_itr->second;

	xrt::bo* bo_handle = NULL;

	try
	{
		bo_handle = new xrt::bo(*parent_bo, size, offset);
	}
	catch (std::exception& ex)
	{
		vart_ml_log(LOG_ERR, "Failed to allocate sub BO of size: %zu reason: %s\n", size, ex.what());
		return NULL;
	}

	void* mapped = bo_handle->map();

	// Map sub buffers in a separate map as a sub buffer with offset 0 will overwrite its parent entry.
	xrt_handle->sub_bo_map[mapped] = bo_handle;

	return (void*)bo_handle;
}

void ddr_free(void* handle, void* ddr_vaddr)
{
	XrtHandle* xrt_handle = (XrtHandle*)handle;

	std::lock_guard<std::mutex> free_mutex_lock(xrt_mutex);

	std::map<void*, xrt::bo*>::iterator bo_itr;

	// Search ddr_vaddr in sub_bo_map first as free is always called for sub bo first.
	if ((bo_itr = xrt_handle->sub_bo_map.find(ddr_vaddr)) != xrt_handle->sub_bo_map.end())
	{
		delete bo_itr->second;

		xrt_handle->sub_bo_map.erase(bo_itr);
	}

	else if ((bo_itr = xrt_handle->bo_map.find(ddr_vaddr)) != xrt_handle->bo_map.end())
	{
		auto d_itr = xrt_handle->bo_ddr_map.find(bo_itr->second);
		if (d_itr != xrt_handle->bo_ddr_map.end())
		{
			xrt_handle->bo_addr_map[d_itr->second].erase(bo_itr->second->address());
			xrt_handle->bo_ddr_map.erase(d_itr);
		}

		delete bo_itr->second;

		xrt_handle->bo_map.erase(bo_itr);
	}
}

void ddr_sync_to_device(void* handle, void* buffer)
{
	(void)handle;
	xrt::bo* bo = static_cast<xrt::bo*>(buffer);
	bo->sync(XCL_BO_SYNC_BO_TO_DEVICE);
}

void ddr_sync_from_device(void* handle, void* buffer)
{
	(void)handle;
	xrt::bo* bo = static_cast<xrt::bo*>(buffer);
	bo->sync(XCL_BO_SYNC_BO_FROM_DEVICE);
}

int ddr_export_buffer(void* handle, void* buffer)
{
	(void)handle;
	xrt::bo* bo = static_cast<xrt::bo*>(buffer);
	return bo->export_buffer();
}

uint64_t get_phy_addr(void* handle, void* ddr_vaddr)
{
	xrt::bo* bo_handle = get_bo_or_sub((XrtHandle*)handle, ddr_vaddr);
	if (bo_handle != nullptr)
		return bo_handle->address();

	xrt::bo* user_bo_ptr = static_cast<xrt::bo*>(ddr_vaddr);

	return user_bo_ptr->address();
}

void* get_vaddr(void* handle, void* ddr_vaddr)
{
	if (ddr_vaddr == nullptr)
		return ddr_vaddr;

	xrt::bo* bo_handle = get_bo_or_sub((XrtHandle*)handle, ddr_vaddr);
	if (bo_handle == nullptr)
	{
		xrt::bo* user_bo_ptr = static_cast<xrt::bo*>(ddr_vaddr);

		return user_bo_ptr->map();
	}

	return ddr_vaddr;
}

void* get_base_vaddr(void* handle, size_t ddrIdx)
{
	XrtHandle* xrt_handle = (XrtHandle*)handle;

	size_t memBase = get_extmemBaseAddr(xrt_handle, ddrIdx);

	for (auto& sub_bo : xrt_handle->sub_bo_map)
		if (sub_bo.second->address() == memBase)
			return sub_bo.second->map();

	for (auto& bo : xrt_handle->bo_map)
		if (bo.second->address() == memBase)
			return bo.second->map();

	return NULL;
}

int write_ddr(void* handle, struct addr addr, const uint8_t* buf, uint32_t size)
{
	XrtHandle* xrt_handle = (XrtHandle*)handle;

	try
	{
		xrt::bo* bo_handle = get_bo_or_sub(xrt_handle, addr.ddr_vaddr);
		bo_handle->write(buf, size, addr.offset);
	}
	catch (std::exception& ex)
	{
		return vart_ml_log_err_msg(DEVICE_DDR_RW_FAILURE, "Failed to write BO, reason : %s\n", ex.what());
	}

	return vart_ml_error::SUCCESS;
}

int read_ddr(void* handle, struct addr addr, uint8_t* buf, uint32_t size)
{
	XrtHandle* xrt_handle = (XrtHandle*)handle;

	try
	{
		xrt::bo* bo_handle = get_bo_or_sub(xrt_handle, addr.ddr_vaddr);
		bo_handle->read(buf, size, addr.offset);
	}
	catch (std::exception& ex)
	{
		return vart_ml_log_err_msg(DEVICE_DDR_RW_FAILURE, "Failed to read BO, reason : %s\n", ex.what());
	}

	return vart_ml_error::SUCCESS;
}

int destroy_ip_context(void* handle)
{
	XrtHandle* xrt_handle = (XrtHandle*)handle;

	int err = vart_ml_error::SUCCESS;

	for (size_t ip_idx = 0; ip_idx < xrt_handle->ip.size(); ip_idx++)
		disconnect_ip_helper(xrt_handle->ip[ip_idx]);

	// Free all allocated ddr blocks
	for (std::map<void*, xrt::bo*>::iterator bo_itr = xrt_handle->bo_map.begin();
	     bo_itr != xrt_handle->bo_map.end();
	     ++bo_itr)
	{
		delete bo_itr->second;
	}

	if (xrt_handle && xrt_handle->hw_context)
		delete xrt_handle->hw_context;

	if (xrt_handle && xrt_handle->device)
		delete xrt_handle->device;

	if (xrt_handle)
		delete xrt_handle;

	return err;
}

void print_fpga_info(void* handle, uint32_t ip_idx)
{
	XrtHandle* xrt_handle = (XrtHandle*)handle;
	auto&      fpga_info  = xrt_handle->ip[ip_idx].fpga_info;

	if (fpga_info.empty())
		throw std::runtime_error(vart_ml_error::exception_message(vart_ml_error::CONFIG_MISSING_FPGA_INFO));

	return print_fpga_info(fpga_info);
}

unsigned int get_ip_fpga_info_attribute(void* handle, uint32_t ip_idx, char* attr)
{
	XrtHandle* xrt_handle = (XrtHandle*)handle;
	auto&      fpga_info  = xrt_handle->ip[ip_idx].fpga_info;

	if (fpga_info.empty())
		throw std::runtime_error(vart_ml_error::exception_message(vart_ml_error::CONFIG_MISSING_FPGA_INFO));

	std::string str(attr);
	return STRINGTOUL(fpga_info[str]);
}

char* get_boardname(void* handle, uint32_t ip_idx)
{
	XrtHandle* xrt_handle = (XrtHandle*)handle;
	auto&      fpga_info  = xrt_handle->ip[ip_idx].fpga_info;

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

	XrtHandle* xrt_handle = (XrtHandle*)handle;

	std::vector<xrt::xclbin::mem> mem_lists = xrt_handle->xclbin_handle->get_mems();

	return mem_lists[xrt_handle->mem_bnk_idxs[extmemIdx]].get_size_kb() * 1024;
}

size_t get_ddr_free_bytes(void* handle, size_t ddrIdx)
{
	if (!is_peripheral_ready)
		throw std::runtime_error(vart_ml_error::exception_message(vart_ml_error::CONFIG_MISSING_PERIPHERALS));

	XrtHandle*                    xrt_handle = (XrtHandle*)handle;
	std::vector<xrt::xclbin::mem> mem_lists  = xrt_handle->xclbin_handle->get_mems();
	const xrt::xclbin::mem&       bank       = mem_lists[xrt_handle->mem_bnk_idxs[ddrIdx]];
	uint64_t                      bank_base  = bank.get_base_address();
	uint64_t                      bank_end   = bank_base + bank.get_size_kb() * 1024;

	std::lock_guard<std::mutex>       lock(xrt_mutex);
	const std::map<uint64_t, size_t>& addr_map   = xrt_handle->bo_addr_map[ddrIdx];
	size_t                            used_bytes = 0;

	for (const auto& entry : addr_map)
		used_bytes += entry.second;

	return (bank_end - bank_base) - used_bytes;
}

size_t get_nbddrs(void* handle)
{
	if (!is_peripheral_ready)
		throw std::runtime_error(vart_ml_error::exception_message(vart_ml_error::CONFIG_MISSING_PERIPHERALS));

	return ((XrtHandle*)handle)->mem_bnk_idxs.size();
}

size_t get_nbextmems(void* handle)
{
	if (!is_peripheral_ready)
		throw std::runtime_error(vart_ml_error::exception_message(vart_ml_error::CONFIG_MISSING_PERIPHERALS));

	return ((XrtHandle*)handle)->mem_bnk_idxs.size();
}

size_t get_extmemBaseAddr(void* handle, size_t extmemIdx)
{
	if (!is_peripheral_ready)
		throw std::runtime_error(vart_ml_error::exception_message(vart_ml_error::CONFIG_MISSING_PERIPHERALS));

	XrtHandle* xrt_handle = (XrtHandle*)handle;

	std::vector<xrt::xclbin::mem> mem_lists = xrt_handle->xclbin_handle->get_mems();

	return mem_lists[xrt_handle->mem_bnk_idxs[extmemIdx]].get_base_address();
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
	XrtHandle* xrt_handle = (XrtHandle*)handle;

	// Print top border
	printf("╔═══════════════════════════════════════════════════════════════════════════════════════╗\n");

	// Print mode title
	printf("║%-87s║\n", "                                     XRT MODE");

	// Print separator after title
	printf("╠══════════════════════╦════════════════════╦════════════════════╦══════════════════════╣\n");

	// Print table header
	printf("║ %-20s ║ %-18s ║ %-18s ║ %-20s ║\n", "NAME", "ADDRESS", "SIZE", "ATTRIBUTE");

	// Print separator
	printf("╠══════════════════════╬════════════════════╬════════════════════╬══════════════════════╣\n");

	// Print DDR resources
	size_t nb_ddrs = get_nbddrs(handle);
	for (size_t i = 0; i < nb_ddrs; i++)
	{
		char name[32];
		snprintf(name, sizeof(name), "ddr_%zu", i);
		printf("║ %-20s ║ 0x%016zx ║ 0x%016zx ║ %-20s ║\n",
		       name,
		       get_extmemBaseAddr(handle, i),
		       get_extmemlen(handle, i),
		       "-");
	}

	// Print IP resources
	size_t ip_count = 0;
	size_t pp_count = 0;
	for (size_t i = 0; i < xrt_handle->ip.size(); i++)
	{
		char node_name[32];
		if (xrt_handle->ip[i].is_pp)
		{
			snprintf(node_name, sizeof(node_name), "pp_%zu", pp_count);
			pp_count++;
		}
		else
		{
			snprintf(node_name, sizeof(node_name), "ip_%zu", ip_count);
			ip_count++;
		}

		// Print IP/PP header
		printf("║ %-20s ║ %-18s ║ %-18s ║ %-20s ║\n", node_name, "-", "-", "-");

		// Print kernel_name
		printf("║   %-18s ║ %-18s ║ %-18s ║ %-20s ║\n",
		       "kernel_name",
		       "-",
		       "-",
		       xrt_handle->ip[i].kernel_name.c_str());

		// Print timestamp
		char timestamp_str[32];
		snprintf(timestamp_str, sizeof(timestamp_str), "0x%08x", xrt_handle->ip[i].timestamp);
		printf("║   %-18s ║ %-18s ║ %-18s ║ %-20s ║\n", "timestamp", "-", "-", timestamp_str);

		// Print ip_interrupt status
		printf("║   %-18s ║ %-18s ║ %-18s ║ %-20s ║\n",
		       "ip_interrupt",
		       "-",
		       "-",
		       xrt_handle->ip[i].ip_interrupt != NULL ? "enabled" : "-");
	}

	// Print bottom border
	printf("╚══════════════════════╩════════════════════╩════════════════════╩══════════════════════╝\n");
}

bool xrt_is_available(void) { return true; }

#else

int get_ip_from_timestamp(void* handle, uint32_t timestamp, uint32_t* ip_idx)
{
	(void)handle;
	(void)timestamp;
	(void)ip_idx;
	return vart_ml_error::SUCCESS;
}

int get_ip(void* handle, bool is_pp, uint32_t* ip_idx)
{
	(void)handle;
	(void)is_pp;
	(void)ip_idx;
	return vart_ml_error::SUCCESS;
}

uint32_t get_nb_ip(void* handle)
{
	(void)handle;
	return 1;
}

uint32_t get_nb_pp(void* handle)
{
	(void)handle;
	return 0;
}

int get_ip_is_pp(void* handle, uint32_t ip_idx, bool* is_pp)
{
	(void)handle;
	(void)ip_idx;
	*is_pp = false;
	return vart_ml_error::SUCCESS;
}

uint32_t read_register(void* handle, uint32_t offset, uint32_t ip_idx)
{
	(void)handle;
	(void)offset;
	(void)ip_idx;
	return 0;
}

void write_register(void* handle, uint32_t offset, uint32_t data, uint32_t ip_idx)
{
	(void)handle;
	(void)offset;
	(void)data;
	(void)ip_idx;
}

int wait_interrupt(void* handle, uint32_t ip_idx)
{
	(void)handle;
	(void)ip_idx;
	return vart_ml_error::SUCCESS;
}

int get_npu_memory_bnk_indexes(void* handle, uint32_t* mem_indexes, size_t nbddrs)
{
	(void)handle;
	(void)mem_indexes;
	(void)nbddrs;
	return vart_ml_error::SUCCESS;
}

int ddr_allocate(void* handle, uint64_t size, uint32_t ddr, struct addr* addr)
{
	(void)handle;
	(void)size;
	(void)ddr;
	(void)addr;
	return vart_ml_error::SUCCESS;
}

void* ddr_malloc(void* handle, size_t ddrIdx, size_t size)
{
	(void)handle;
	(void)ddrIdx;
	(void)size;
	return NULL;
}

void* ddr_malloc_sub(void* handle, void* parent_vaddr, size_t offset, size_t size)
{
	(void)handle;
	(void)size;
	return (uint8_t*)parent_vaddr + offset;
}

void ddr_free(void* handle, void* ddr_vaddr)
{
	(void)handle;
	(void)ddr_vaddr;
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

uint64_t get_phy_addr(void* handle, void* ddr_vaddr)
{
	(void)handle;
	(void)ddr_vaddr;
	return vart_ml_error::SUCCESS;
}

void* get_vaddr(void* handle, void* ddr_vaddr)
{
	(void)handle;
	return ddr_vaddr;
}

void* get_base_vaddr(void* handle, size_t ddrIdx)
{
	(void)handle;
	(void)ddrIdx;
	return NULL;
}

int write_ddr(void* handle, void* vaddr, uint64_t offset, const uint8_t* buf, uint32_t size)
{
	(void)handle;
	(void)vaddr;
	(void)offset;
	(void)buf;
	(void)size;
	return vart_ml_error::SUCCESS;
}

int read_ddr(void* handle, void* vaddr, uint64_t offset, uint8_t* buf, uint32_t size)
{
	(void)handle;
	(void)vaddr;
	(void)offset;
	(void)buf;
	(void)size;
	return vart_ml_error::SUCCESS;
}

int destroy_ip_context(void* handle)
{
	(void)handle;
	return vart_ml_error::SUCCESS;
}

int parse_fpga_info(void* handle, uint32_t ip_idx)
{
	(void)handle;
	(void)ip_idx;
	return 0;
}

void print_fpga_info(void* handle, uint32_t ip_idx)
{
	(void)handle;
	(void)ip_idx;
}

unsigned int get_ip_fpga_info_attribute(void* handle, uint32_t ip_idx, char* attr)
{
	(void)handle;
	(void)ip_idx;
	(void)attr;
	return 0;
}

char* get_boardname(void* handle, uint32_t ip_idx)
{
	(void)handle;
	(void)ip_idx;
	return nullptr;
}

size_t get_extmemlen(void* handle, size_t extmemIdx)
{
	(void)handle;
	(void)extmemIdx;
	return 0;
}

size_t get_ddr_free_bytes(void* handle, size_t ddrIdx)
{
	(void)handle;
	(void)ddrIdx;
	return 0;
}

size_t get_nbddrs(void* handle)
{
	(void)handle;
	return 0;
}

size_t get_nbextmems(void* handle)
{
	(void)handle;
	return 0;
}

size_t get_extmemBaseAddr(void* handle, size_t extmemIdx)
{
	(void)handle;
	(void)extmemIdx;
	return 0;
}

int get_fpgafamily(void* handle, enum FpgaFamily* family)
{
	(void)handle;
	(void)family;
	return vart_ml_error::SUCCESS;
}

int get_architecture(void* handle, enum FpgaArchitecture* arch)
{
	(void)handle;
	(void)arch;
	return vart_ml_error::SUCCESS;
}

void print_resource_info(void* handle) { (void)handle; }

bool xrt_is_available(void) { return false; }

#endif
