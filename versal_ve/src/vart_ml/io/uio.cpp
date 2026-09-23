/**
 * @file embedded_api_implem_uio.cpp
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

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <fcntl.h>
#include <filesystem>
#include <glob.h>
#include <libudev.h>
#include <map>
#include <mutex>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

#include "uio.h"
#include "utils/fpga_info.h"
#include "utils/log.h"
#include "utils/npu_reg.h"
#include "utils/shell.h"

#define DEFAULT_IP_NAME "npu"
#define FAMILYPATH      "/proc/device-tree/model"
#define NPUVERSION      2.0

#define UIO_OFF(i, pagesize) (i * pagesize)

#define STRINGTOUL(x) (parse_uint(x))

static std::string ddr_name;
static std::string noc_name;
static std::string memtiles_name;

static float            l_version;
static FpgaFamily       l_family;
static FpgaArchitecture l_arch;

static std::vector<std::string>   ips_name;
static std::map<std::string, int> uiofd;
static std::mutex                 uio_mutex;
static bool                       is_peripheral_ready = false;
static bool                       is_memtile_ready    = false;
static bool                       is_arch_set         = false;

typedef struct
{
	std::string name;
	size_t      addr;
	size_t      offset;
	size_t      size;
} uio_map;

typedef struct
{
	volatile void* base_vaddr;
	size_t         phy_addr;
	size_t         size;
} mem_desc;

/* M_AXI_HPM0_FPD is 128-bit, but CTRLBus is 32-bit. Supporting accesses not
   aligned to 128 bits would be too costly, so we simply drop the two least
   significant bits of the offset. Therefore, CTRLBus offsets have to be
   multiplied by 4, which is done implicitly by pointer arithmetic when
   indexing ctrlbus_base since it is an uint32_t*.
*/
typedef struct
{
	uint32_t                           timestamp;
	std::string                        kernel_name;
	mem_desc                           ctrlbus;
	bool                               is_pp;
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
	std::vector<mem_desc>            memtiles;
	mem_desc                         noc;
	std::vector<mem_desc>            ddr;
	std::vector<ip_handle>           ip;
	std::map<void*, ddr_coordinates> local_ddr_map;
	std::map<uint64_t, ddr_block>    ddr_map[MAXDDRS];
} UIOHandle;

static mem_desc devicetree_memtiles;

static float get_devicetree_version(float* version)
{
	std::string path = "/proc/device-tree/" + std::string(DEFAULT_IP_NAME);

	if (l_family == VERSAL)
		path += "_versal_devicetree_info/version";
	else
		path += "_zynqmp_devicetree_info/version";

	FILE* f;
	if ((f = fopen(path.c_str(), "r")) == NULL)
		return vart_ml_log_err_msg(FILE_ACCESS_OPEN_FAILURE, "Failed to open %s.\n", path.c_str());

	if (fscanf(f, "%f", &(*version)) <= 0)
	{
		fclose(f);
		return vart_ml_log_err_msg(DEVICE_ARG_NOT_FOUND_IN_DEV_TREE,
		                           "Failed to retrieve version from device tree.\n");
	}

	fclose(f);

	return SUCCESS;
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

static int open_all_npu_uio(void) // std::map<std::string, int>& uiofd)
{
	std::string devpath = "/dev/";

	std::string name(DEFAULT_IP_NAME);
	if (l_family == VERSAL)
		name += "_versal";
	else
		name += "_zynqmp";

	std::string pp_name("pp");
	if (l_family == VERSAL)
		pp_name += "_versal";
	else
		pp_name += "_zynqmp";

	std::vector<std::string> uio_list;
	for (const auto& map : std::filesystem::directory_iterator(devpath))
	{
		if (map.path().string().find(name) != std::string::npos)
			uio_list.push_back(map.path().string().substr(devpath.size()));

		if (map.path().string().find(pp_name) != std::string::npos)
			uio_list.push_back(map.path().string().substr(devpath.size()));
	}

	for (auto& uio : uio_list)
		if ((uiofd[uio] = open((devpath + uio).c_str(), O_RDWR, O_SYNC)) == -1)
			return vart_ml_log_err_msg(
			    FILE_ACCESS_OPEN_FAILURE, "Failed to open %s\n", (devpath + uio).c_str());

	return SUCCESS;
}

static int get_devicetree_uio_maps(int fd, std::string keyword, std::vector<uio_map>& uio_maps)
{
	struct stat statbuf;
	if (fstat(fd, &statbuf))
		return vart_ml_log_err_msg(FILE_ACCESS_REFERENCE_FAILURE, "fstat failure\n");

	struct udev* udev = udev_new();
	if (udev == NULL)
		return vart_ml_log_err_msg(SYSTEM_ERROR_UDEV_CREATION_FAILURE, "udev_new() failed.\n");

	struct udev_device* dev = udev_device_new_from_devnum(udev, 'c', statbuf.st_rdev);
	if (dev == NULL)
		return vart_ml_log_err_msg(SYSTEM_ERROR_UDEV_CREATION_FAILURE,
		                           "udev_device_new_from_devnum() failure for %s.\n",
		                           keyword.c_str());

	const char* syspath_cstr = udev_device_get_syspath(dev);
	if (syspath_cstr == NULL)
		return vart_ml_log_err_msg(SYSTEM_ERROR_UDEV_CREATION_FAILURE,
		                           "udev_device_get_syspath() failure for %s.\n",
		                           keyword.c_str());

	std::string syspath = syspath_cstr;

	std::vector<std::string> map_list;
	for (const auto& map : std::filesystem::directory_iterator(syspath + "/maps"))
		map_list.push_back(map.path().string().substr(syspath.size() + 1));

	std::sort(map_list.begin(), map_list.end());

	for (auto& m : map_list)
	{
		uio_map tmp;

		tmp.name = udev_device_get_sysattr_value(dev, std::string(m + "/name").c_str());
		if (tmp.name.empty())
			return vart_ml_log_err_msg(SYSTEM_ERROR_UDEV_GET_ATTR_FAILURE,
			                           "udev_device_get_sysattr_value() failure.\n");

		if (tmp.name.find(keyword) == std::string::npos)
			continue;

		const char* addrattr = udev_device_get_sysattr_value(dev, std::string(m + "/addr").c_str());
		if (addrattr == NULL)
			return vart_ml_log_err_msg(SYSTEM_ERROR_UDEV_GET_ATTR_FAILURE,
			                           "udev_device_get_sysattr_value() failure.\n");

		tmp.addr = parse_uint(addrattr);

		const char* offsetattr = udev_device_get_sysattr_value(dev, std::string(m + "/offset").c_str());
		if (offsetattr == NULL)
			return vart_ml_log_err_msg(SYSTEM_ERROR_UDEV_GET_ATTR_FAILURE,
			                           "udev_device_get_sysattr_value() failure.\n");

		tmp.offset = parse_uint(offsetattr);

		const char* sizeattr = udev_device_get_sysattr_value(dev, std::string(m + "/size").c_str());
		if (sizeattr == NULL)
			return vart_ml_log_err_msg(SYSTEM_ERROR_UDEV_GET_ATTR_FAILURE,
			                           "udev_device_get_sysattr_value() failure.\n");

		tmp.size = parse_uint(sizeattr);

		uio_maps.push_back(tmp);
	}

	udev_device_unref(dev);
	udev_unref(udev);

	return SUCCESS;
}

int create_ip_context(void** handle)
{
	std::unique_ptr<UIOHandle> uio_handle(new UIOHandle);

	int err = get_devicetree_fpgafamily(&l_family);
	if (err)
		return err;

	err = get_devicetree_version(&l_version);
	if (err)
		return err;

	if ((int)l_version != (int)NPUVERSION)
		return vart_ml_log_err_msg(
		    vart_ml_error::SYSTEM_ERROR_VART_ML_VERSION,
		    "VART ML version (%.1f) and device-tree version (%.1f) are too far apart.\n",
		    NPUVERSION,
		    l_version);
	else if (l_version != (float)NPUVERSION)
		vart_ml_log(LOG_WARN,
		            "VART ML version (%.1f) and device-tree version (%.1f) are different but we can still "
		            "proceed\n",
		            NPUVERSION,
		            l_version);

	err = open_all_npu_uio();
	if (err)
		return err;

	/* Select the root name used to look for the IPs */
	std::string ip_name(DEFAULT_IP_NAME);
	if (l_family == VERSAL)
		ip_name += "_versal";
	else
		ip_name += "_zynqmp";

	std::string pp_name("pp");
	if (l_family == VERSAL)
		pp_name += "_versal";
	else
		pp_name += "_zynqmp";

	ddr_name      = ip_name + "_ddr";
	noc_name      = ip_name + "_noc";
	memtiles_name = ip_name + "_memtiles";
	ips_name.clear();
	for (auto& uio : uiofd)
		if (uio.first != ddr_name && uio.first != noc_name && uio.first != memtiles_name)
			ips_name.push_back(uio.first);

	const long pagesize = sysconf(_SC_PAGESIZE);
	if (pagesize == -1)
		return vart_ml_log_err(vart_ml_error::SYSTEM_CONFIG_BAD_PAGE_SIZE);

	for (auto& name : ips_name)
	{
		std::vector<uio_map> ctrlbus_map;

		bool pp_bus = name.find(pp_name) != std::string::npos;

		err = get_devicetree_uio_maps(uiofd[name], (pp_bus) ? "pp_bus" : "ctrlbus", ctrlbus_map);
		if (err)
			return err;

		if (ctrlbus_map.size() > 1)
			return vart_ml_log_err_msg(vart_ml_error::DEVICE_UNEXPECTED_ARG_VALUE,
			                           "Too many %s maps for IP %s.\n",
			                           (pp_bus) ? "pp_bus" : "ctrlbus",
			                           name.c_str());

		ip_handle ip;
		ip.kernel_name      = name;
		ip.ctrlbus.phy_addr = ctrlbus_map[0].addr;
		ip.ctrlbus.size     = ctrlbus_map[0].size;

		ip.ctrlbus.base_vaddr = (volatile void*)mmap(
		    NULL, ip.ctrlbus.size, PROT_READ | PROT_WRITE, MAP_SHARED, uiofd[name], UIO_OFF(0, pagesize));

		if (ip.ctrlbus.base_vaddr == MAP_FAILED)
			return vart_ml_log_err_msg(vart_ml_error::SYSTEM_ERROR_MMAP_FAILURE,
			                           "Failed to mmap %s.\n",
			                           (pp_bus) ? "pp_bus" : "ctrlbus");

		ip.timestamp     = ((volatile uint32_t*)(ip.ctrlbus.base_vaddr))[0];
		ip.is_pp         = pp_bus;
		ip.connect_count = 0;

		uio_handle->ip.push_back(std::move(ip));
	}

	*handle = uio_handle.release();

	return vart_ml_error::SUCCESS;
}

static int disconnect_ip_helper(ip_handle& ip)
{
	if (munmap((void*)ip.ctrlbus.base_vaddr, ip.ctrlbus.size))
		return vart_ml_log_err_msg(vart_ml_error::SYSTEM_ERROR_UNMAP_FAILURE, "munmap CtrlBus.\n");

	auto uio = uiofd.find(ip.kernel_name);
	if (uio == uiofd.end())
		return vart_ml_log_err_msg(vart_ml_error::FILE_ACCESS_NOT_FOUND,
		                           "Failed to find hook to /dev/%s in uio file descriptor list\n",
		                           ip.kernel_name.c_str());

	if (close(uio->second))
		return vart_ml_log_err_msg(
		    vart_ml_error::FILE_ACCESS_CLOSE_FAILURE, "Failed to close /dev/%s\n", uio->first.c_str());

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

	UIOHandle* uio_handle = (UIOHandle*)handle;
	auto&      ip         = uio_handle->ip[ip_idx];

	if (ip.connect_count <= 0)
		return vart_ml_log_err_msg(vart_ml_error::CONFIG_UNEXPECTED_ARG_VALUE,
		                           "Unexpected IP connection counter (%d). Disconnect operation failed.\n",
		                           ip.connect_count);

	--ip.connect_count;

	return vart_ml_error::SUCCESS;
}

static bool refine_memtiles_layout(void* handle)
{
	UIOHandle* uio_handle = (UIOHandle*)handle;

	for (auto& ip : uio_handle->ip)
	{
		auto& fpga_info = ip.fpga_info;
		if (!fpga_info.empty() && fpga_info.contains("noc.memtileBaseAddresses")
		    && fpga_info.contains("noc.memtileSize"))
		{
			std::vector<size_t> memtiles_addr = convertToVector(fpga_info["noc.memtileBaseAddresses"]);
			std::vector<size_t> memtiles_size = convertToVector(fpga_info["noc.memtileSize"]);

			if (memtiles_addr.size() != memtiles_size.size())
			{
				vart_ml_log(LOG_WARN,
				            "Fpga info of IP %s (0x%x) malformed: memtiles base address "
				            "count and memtile "
				            "size count do not match. Continuing anyways.\n",
				            ip.kernel_name.c_str(),
				            ip.timestamp);
				continue;
			}

			uio_handle->memtiles.resize(memtiles_addr.size());

			for (size_t i = 0; i < memtiles_addr.size(); i++)
			{
				uio_handle->memtiles[i].phy_addr = memtiles_addr[i];
				uio_handle->memtiles[i].size     = memtiles_size[i] * sizeof(uint32_t);

				size_t offset                      = memtiles_addr[i] - devicetree_memtiles.phy_addr;
				uio_handle->memtiles[i].base_vaddr = (uint8_t*)(devicetree_memtiles.base_vaddr) + offset;
			}

			return true;
		}
	}

	return false;
}

static int setup_memtiles(void* handle)
{
	if (is_memtile_ready || l_arch != FpgaArchitecture::AIEML_V1C)
		return vart_ml_error::SUCCESS;

	auto it = uiofd.find(memtiles_name);
	if (it == uiofd.end())
	{
		vart_ml_log(LOG_WARN, "No memtiles UIO device found, skip memtile memory mapping\n");
		return vart_ml_error::SUCCESS;
	}

	int                  memtiles_fd = it->second;
	std::vector<uio_map> memtile_map;

	int err = get_devicetree_uio_maps(memtiles_fd, "memtiles", memtile_map);
	if (err || memtile_map.empty())
	{
		vart_ml_log(LOG_WARN, "Failed to get MemTiles length, skip memtile memory mapping\n");
		return vart_ml_error::SUCCESS;
	}

	devicetree_memtiles.phy_addr = memtile_map[0].addr;
	devicetree_memtiles.size     = memtile_map[0].size;
	/*
	 * The memtile area is allocated as a single block for now.
	 * fpga_info is required to have a fine-grained view of all the
	 * memtile chunks.
	 */
	devicetree_memtiles.base_vaddr = (volatile void*)mmap(
	    NULL, devicetree_memtiles.size, PROT_READ | PROT_WRITE, MAP_SHARED, memtiles_fd, 0);
	if (devicetree_memtiles.base_vaddr == MAP_FAILED)
	{
		vart_ml_log(LOG_WARN, "Failed to mmap MemTiles, skip memtile memory mapping\n");
		return vart_ml_error::SUCCESS;
	}

	is_memtile_ready = refine_memtiles_layout(handle);
	return vart_ml_error::SUCCESS;
}

static int parse_ip_fpga_info(void* handle, uint32_t ip_idx)
{
	UIOHandle* uio_handle = (UIOHandle*)handle;

	auto& ip = uio_handle->ip[ip_idx];

	if (!ip.is_pp)
	{
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
		if (is_peripheral_ready)
		{
			if (family != l_family)
				return vart_ml_log_err_msg(
				    vart_ml_error::CONFIG_UNEXPECTED_FPGA_FAMILY,
				    "FPGA INFO family %s (general.family) differs from device tree family %s.\n",
				    family_info.c_str(),
				    stringFromFpgaFamily(l_family));
		}

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
	}

	return vart_ml_error::SUCCESS;
}

int connect_ip(void* handle, uint32_t ip_idx)
{
	auto& ip = ((UIOHandle*)handle)->ip[ip_idx];

	if (ip.fpga_info.empty())
	{
		int err = parse_ip_fpga_info(handle, ip_idx);
		if (err)
			return err;
	}

	int err = setup_memtiles(handle);
	if (err)
		return err;

	ip.connect_count++;

	return vart_ml_error::SUCCESS;
}

int connect_peripherals(void* handle)
{
	UIOHandle* uio_handle = (UIOHandle*)handle;

	// Mmap resources (ddr, noc, memtile) if not yet done.
	if (!is_peripheral_ready)
	{
		const long pagesize = sysconf(_SC_PAGESIZE);
		if (pagesize == -1)
			return vart_ml_log_err(vart_ml_error::SYSTEM_CONFIG_BAD_PAGE_SIZE);

		std::vector<uio_map> ddr_map;
		std::vector<uio_map> noc_map;

		// Discover DDR layout
		int err = get_devicetree_uio_maps(uiofd[ddr_name], "ddr", ddr_map);
		if (err)
			return err;

		for (size_t i = 0; i < ddr_map.size(); i++)
		{
			mem_desc ddr_desc;
			ddr_desc.size       = ddr_map[i].size;
			ddr_desc.phy_addr   = ddr_map[i].addr;
			ddr_desc.base_vaddr = (volatile void*)mmap(NULL,
			                                           ddr_map[i].size,
			                                           PROT_READ | PROT_WRITE,
			                                           MAP_SHARED,
			                                           uiofd[ddr_name],
			                                           UIO_OFF(i, pagesize));
			if (ddr_desc.base_vaddr == MAP_FAILED)
				return vart_ml_log_err_msg(
				    vart_ml_error::SYSTEM_ERROR_MMAP_FAILURE, "Failed to mmap DDR %zu.\n", i);

			uio_handle->ddr.push_back(std::move(ddr_desc));
		}

		// Discover NOC layout
		err = get_devicetree_fpgafamily(&l_family);
		if (err)
			return err;

		/* NOC is only mapped on Versal */
		if (l_family == VERSAL)
		{
			err = get_devicetree_uio_maps(uiofd[noc_name], "noc", noc_map);
			if (err)
				return err;

			uio_handle->noc.size       = noc_map[0].size;
			uio_handle->noc.phy_addr   = noc_map[0].addr;
			uio_handle->noc.base_vaddr = (volatile void*)mmap(NULL,
			                                                  noc_map[0].size,
			                                                  PROT_READ | PROT_WRITE,
			                                                  MAP_SHARED,
			                                                  uiofd[noc_name],
			                                                  UIO_OFF(0, pagesize));
			if (uio_handle->noc.base_vaddr == MAP_FAILED)
				vart_ml_log_err_msg(vart_ml_error::SYSTEM_ERROR_MMAP_FAILURE,
				                    "Failed to mmap NOC, NOC access will not be possible.\n");
		}

		is_peripheral_ready = true;
	}

	return vart_ml_error::SUCCESS;
}

int get_ip_from_timestamp(void* handle, uint32_t timestamp, uint32_t* ip_idx)
{
	UIOHandle* uio_handle = (UIOHandle*)handle;

	for (size_t i = 0; i < uio_handle->ip.size(); i++)
		if (timestamp == uio_handle->ip[i].timestamp)
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
	UIOHandle* uio_handle = (UIOHandle*)handle;

	if (ip_idx >= uio_handle->ip.size())
		return vart_ml_log_err_msg(vart_ml_error::DEVICE_UNEXPECTED_ARG_VALUE,
		                           "Requested IP index %u exceeds IP count (%zu).\n",
		                           ip_idx,
		                           uio_handle->ip.size());

	*timestamp = uio_handle->ip[ip_idx].timestamp;

	return vart_ml_error::SUCCESS;
}

int get_ip(void* handle, bool is_pp, uint32_t* ip_idx)
{
	UIOHandle* uio_handle = (UIOHandle*)handle;

	if (get_nb_ip(handle) > 1 || get_nb_pp(handle) > 1)
		return vart_ml_log_err_msg(
		    vart_ml_error::DEVICE_UNEXPECTED_ARG_VALUE,
		    "There is more than one %s IP in the device list. Unable to make selection.\n",
		    is_pp ? "PP" : "NPU");

	for (size_t i = 0; i < uio_handle->ip.size(); i++)
		if (is_pp == uio_handle->ip[i].is_pp)
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

	UIOHandle* uio_handle = (UIOHandle*)handle;
	for (auto& ip : uio_handle->ip)
		if (!ip.is_pp)
			nb_ip++;

	return nb_ip;
}

uint32_t get_nb_pp(void* handle)
{
	size_t nb_pp = 0;

	UIOHandle* uio_handle = (UIOHandle*)handle;
	for (auto& ip : uio_handle->ip)
		if (ip.is_pp)
			nb_pp++;

	return nb_pp;
}

int get_ip_is_pp(void* handle, uint32_t ip_idx, bool* is_pp)
{
	UIOHandle* uio_handle = (UIOHandle*)handle;

	if (ip_idx >= uio_handle->ip.size())
		return vart_ml_log_err_msg(vart_ml_error::CONFIG_UNEXPECTED_ARG_VALUE,
		                           "IP index (%u) exceeds IP count (%zu)\n",
		                           ip_idx,
		                           uio_handle->ip.size());

	*is_pp = uio_handle->ip[ip_idx].is_pp;
	return vart_ml_error::SUCCESS;
}

uint32_t read_register(void* handle, uint32_t offset, uint32_t ip_idx)
{
	UIOHandle* uio_handle = (UIOHandle*)handle;

	return ((volatile uint32_t*)(uio_handle->ip[ip_idx].ctrlbus.base_vaddr))[offset];
}

void write_register(void* handle, uint32_t offset, uint32_t data, uint32_t ip_idx)
{
	UIOHandle* uio_handle = (UIOHandle*)handle;

	((volatile uint32_t*)(uio_handle->ip[ip_idx].ctrlbus.base_vaddr))[offset] = data;
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
		if (size > 64)
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

	/* Memory access needs to be 64 bytes aligned if bigger than 64 bytes*/
	size_t start = addr.phy_addr % 64;

	if (start == 0 && size % 64 == 0)
	{
		memcpy((uint8_t*)addr.ddr_vaddr + addr.offset, buf, size);
	}
	else if (start + size <= 64)
	{
		uint8_t tmp[64];

		if (start != 0)
			memcpy(tmp, (uint8_t*)addr.ddr_vaddr + addr.offset - start, 64);

		memcpy(tmp + start, buf, size);
		memcpy((uint8_t*)addr.ddr_vaddr + addr.offset - start, tmp, 64);
	}
	else
	{
		uint8_t tmp[64];

		size_t written = 0;
		if (start != 0)
		{
			memcpy(tmp, (uint8_t*)addr.ddr_vaddr + addr.offset - start, 64);
			memcpy(tmp + start, buf, 64 - start);
			memcpy((uint8_t*)addr.ddr_vaddr + addr.offset, tmp, 64);

			written = 64 - start;
		}

		size_t r = (size - written) % 64;
		if (r != 0)
		{
			if (size - written > 64)
				memcpy((uint8_t*)addr.ddr_vaddr + addr.offset + written, buf + written, size - written - r);

			memcpy(tmp, (uint8_t*)addr.ddr_vaddr + addr.offset + size - r, 64);
			memcpy(tmp, buf + size - r, r);
			memcpy((uint8_t*)addr.ddr_vaddr + addr.offset + size - r, tmp, 64);
		}
		else
			memcpy((uint8_t*)addr.ddr_vaddr + addr.offset + written, buf + written, size - written);
	}

	return vart_ml_error::SUCCESS;
}

void read_noc(void* handle, uint64_t offset, uint32_t* buf, uint32_t wordsize)
{
	UIOHandle* uio_handle = (UIOHandle*)handle;

	uint32_t* src = (uint32_t*)((uint8_t*)uio_handle->noc.base_vaddr + offset);
	for (size_t i = 0; i < wordsize; i++)
		*buf++ = *src++;
}

void write_noc(void* handle, uint64_t offset, const uint32_t* buf, uint32_t wordsize)
{
	UIOHandle* uio_handle = (UIOHandle*)handle;

	uint32_t* dest = (uint32_t*)((uint8_t*)uio_handle->noc.base_vaddr + offset);
	for (size_t i = 0; i < wordsize; i++)
		*dest++ = *buf++;
}

uint64_t get_phy_addr(void* handle, void* ddr_vaddr)
{
	UIOHandle* uio_handle = (UIOHandle*)handle;

	std::map<void*, ddr_coordinates>::iterator local_ddr_itr = uio_handle->local_ddr_map.find(ddr_vaddr);

	if (local_ddr_itr == uio_handle->local_ddr_map.end())
		return 0;

	size_t   ddr_idx = local_ddr_itr->second.ddr_idx;
	uint32_t offset  = local_ddr_itr->second.offset;

	std::map<uint64_t, ddr_block>::iterator ddr_itr = uio_handle->ddr_map[ddr_idx].find(offset);

	if (ddr_itr != uio_handle->ddr_map[ddr_idx].end())
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
	UIOHandle* uio_handle = (UIOHandle*)handle;

	if (ddrIdx < uio_handle->ddr.size())
		return (void*)uio_handle->ddr[ddrIdx].base_vaddr;

	ddrIdx -= uio_handle->ddr.size();
	return (void*)uio_handle->memtiles[ddrIdx].base_vaddr;
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
static uint64_t ddr_find_free_offset(UIOHandle* uio_handle, size_t ddrIdx, size_t size)
{
	uint64_t candidate = 0;

	for (const auto& entry : uio_handle->ddr_map[ddrIdx])
	{
		if (candidate + size <= entry.first)
			return candidate;
		candidate = entry.first + entry.second.size;
	}

	if (candidate + size <= get_extmemlen(uio_handle, ddrIdx))
		return candidate;

	return UINT64_MAX;
}

void* ddr_malloc(void* handle, size_t ddrIdx, size_t size)
{
	UIOHandle* uio_handle = (UIOHandle*)handle;

	std::lock_guard<std::mutex> malloc_mutex_lock(uio_mutex);

	uint64_t offset = ddr_find_free_offset(uio_handle, ddrIdx, size);

	if (offset == UINT64_MAX)
	{
		vart_ml_log(LOG_ERR,
		            "Error: Not enough space available in DDR %zu to allocate a buffer of %zu bytes."
		            " (DDR total: %zu bytes)\n",
		            ddrIdx,
		            size,
		            (size_t)get_extmemlen(uio_handle, ddrIdx));
		return NULL;
	}

	void* vaddr = (uint8_t*)uio_handle->ddr[ddrIdx].base_vaddr + offset;

	ddr_coordinates new_coordinates;

	new_coordinates.ddr_idx = ddrIdx;
	new_coordinates.offset  = offset;

	uio_handle->local_ddr_map[vaddr] = new_coordinates;

	ddr_block new_block;

	new_block.phy_addr = get_extmemBaseAddr(uio_handle, ddrIdx) + offset;
	new_block.size     = (size + (1 << 8) - 1) & ~((1 << 8) - 1); // 256 byte aligned
	new_block.pid      = getpid();

	uio_handle->ddr_map[ddrIdx][offset] = new_block;

	return vaddr;
}

void* ddr_malloc_sub(void* handle, void* parent_vaddr, size_t offset, size_t size)
{
	UIOHandle* uio_handle = (UIOHandle*)handle;

	std::lock_guard<std::mutex> malloc_mutex_lock(uio_mutex);

	std::map<void*, ddr_coordinates>::iterator parent_itr = uio_handle->local_ddr_map.find(parent_vaddr);

	if (parent_itr == uio_handle->local_ddr_map.end())
	{
		vart_ml_log(LOG_ERR, "Parent vaddr not found in local_ddr_map.\n");
		return NULL;
	}

	size_t ddr_idx    = parent_itr->second.ddr_idx;
	size_t parent_off = parent_itr->second.offset;

	std::map<uint64_t, ddr_block>::iterator parent_block_itr = uio_handle->ddr_map[ddr_idx].find(parent_off);
	if (parent_block_itr == uio_handle->ddr_map[ddr_idx].end())
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

	uio_handle->local_ddr_map[sub_vaddr] = new_coordinates;

	// Only insert a new ddr_map entry when it doesn't collide with an existing one.
	// When offset==0, parent_off+offset==parent_off is already present; reuse it.
	if (!uio_handle->ddr_map[ddr_idx].contains(parent_off + offset))
	{
		ddr_block new_block;
		new_block.phy_addr = get_extmemBaseAddr(uio_handle, ddr_idx) + parent_off + offset;
		new_block.size     = (size + (1 << 8) - 1) & ~((1 << 8) - 1);
		new_block.pid      = getpid();

		uio_handle->ddr_map[ddr_idx][parent_off + offset] = new_block;
	}

	return sub_vaddr;
}

int ddr_free(void* handle, void* ddr_vaddr)
{
	UIOHandle* uio_handle = (UIOHandle*)handle;

	std::lock_guard<std::mutex> free_mutex_lock(uio_mutex);

	std::map<void*, ddr_coordinates>::iterator local_ddr_itr = uio_handle->local_ddr_map.find(ddr_vaddr);

	if (local_ddr_itr == uio_handle->local_ddr_map.end())
		return vart_ml_error::SUCCESS;

	size_t   ddr_idx = local_ddr_itr->second.ddr_idx;
	uint32_t offset  = local_ddr_itr->second.offset;

	std::map<uint64_t, ddr_block>::iterator ddr_itr = uio_handle->ddr_map[ddr_idx].find(offset);

	if (ddr_itr != uio_handle->ddr_map[ddr_idx].end())
		uio_handle->ddr_map[ddr_idx].erase(ddr_itr);

	uio_handle->local_ddr_map.erase(local_ddr_itr);

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
	UIOHandle* uio_handle = (UIOHandle*)handle;

	int err = vart_ml_error::SUCCESS;

	for (size_t ip_idx = 0; ip_idx < uio_handle->ip.size(); ip_idx++)
		err += disconnect_ip_helper(uio_handle->ip[ip_idx]);

	// Free all allocated ddr blocks
	std::map<void*, ddr_coordinates>::iterator local_ddr_itr = uio_handle->local_ddr_map.begin();

	while (local_ddr_itr != uio_handle->local_ddr_map.end())
	{
		size_t   ddr_idx = local_ddr_itr->second.ddr_idx;
		uint32_t offset  = local_ddr_itr->second.offset;

		std::map<uint64_t, ddr_block>::iterator ddr_itr = uio_handle->ddr_map[ddr_idx].find(offset);

		if (ddr_itr != uio_handle->ddr_map[ddr_idx].end())
			uio_handle->ddr_map[ddr_idx].erase(ddr_itr);

		local_ddr_itr = uio_handle->local_ddr_map.erase(local_ddr_itr);
	}

	for (size_t i = 0; i < get_nbddrs(uio_handle); i++)
		if (munmap((void*)uio_handle->ddr[i].base_vaddr, uio_handle->ddr[i].size))
			err += vart_ml_log_err_msg(vart_ml_error::SYSTEM_ERROR_UNMAP_FAILURE, "munmap VART ML DDR.\n");

	if (munmap((void*)uio_handle->noc.base_vaddr, uio_handle->noc.size))
		err += vart_ml_log_err_msg(vart_ml_error::SYSTEM_ERROR_UNMAP_FAILURE, "munmap NOC.\n");

	if (munmap((void*)devicetree_memtiles.base_vaddr, devicetree_memtiles.size))
		err += vart_ml_log_err_msg(vart_ml_error::SYSTEM_ERROR_UNMAP_FAILURE, "munmap MEMTILES.\n");

	/* Close DDR, NOC, and memtile UIO file descriptors so the kernel releases their CMA pages. */
	for (const auto& name : { ddr_name, noc_name, memtiles_name })
	{
		auto it = uiofd.find(name);
		if (it == uiofd.end())
			continue;
		if (close(it->second))
			err += vart_ml_log_err_msg(
			    vart_ml_error::FILE_ACCESS_CLOSE_FAILURE, "Failed to close /dev/%s\n", name.c_str());
		uiofd.erase(it);
	}

	if (uio_handle)
		delete uio_handle;

	ips_name.clear();
	is_peripheral_ready = false;
	is_memtile_ready    = false;
	is_arch_set         = false;

	return err;
}

void print_fpga_info(void* handle, uint32_t ip_idx)
{
	UIOHandle* uio_handle = (UIOHandle*)handle;
	auto&      fpga_info  = uio_handle->ip[ip_idx].fpga_info;

	if (fpga_info.empty())
		throw std::runtime_error(vart_ml_error::exception_message(vart_ml_error::CONFIG_MISSING_FPGA_INFO));

	return print_fpga_info(fpga_info);
}

unsigned int get_ip_fpga_info_attribute(void* handle, uint32_t ip_idx, char* attr)
{
	UIOHandle* uio_handle = (UIOHandle*)handle;
	auto&      fpga_info  = uio_handle->ip[ip_idx].fpga_info;

	if (fpga_info.empty())
		throw std::runtime_error(vart_ml_error::exception_message(vart_ml_error::CONFIG_MISSING_FPGA_INFO));

	std::string str(attr);
	return STRINGTOUL(fpga_info[str]);
}

char* get_boardname(void* handle, uint32_t ip_idx)
{
	UIOHandle* uio_handle = (UIOHandle*)handle;
	auto&      fpga_info  = uio_handle->ip[ip_idx].fpga_info;

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

	UIOHandle* uio_handle = (UIOHandle*)handle;

	if (extmemIdx < uio_handle->ddr.size())
		return uio_handle->ddr[extmemIdx].size;

	extmemIdx -= uio_handle->ddr.size();
	return uio_handle->memtiles[extmemIdx].size;
}

size_t get_ddr_free_bytes(void* handle, size_t ddrIdx)
{
	if (!is_peripheral_ready)
		throw std::runtime_error(vart_ml_error::exception_message(vart_ml_error::CONFIG_MISSING_PERIPHERALS));

	UIOHandle* uio_handle = (UIOHandle*)handle;

	std::lock_guard<std::mutex> lock(uio_mutex);

	size_t   total      = get_extmemlen(handle, ddrIdx);
	size_t   free_bytes = 0;
	uint64_t cursor     = 0;

	for (const auto& entry : uio_handle->ddr_map[ddrIdx])
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

	return ((UIOHandle*)handle)->noc.size;
}

size_t get_nbddrs(void* handle)
{
	if (!is_peripheral_ready)
		throw std::runtime_error(vart_ml_error::exception_message(vart_ml_error::CONFIG_MISSING_PERIPHERALS));

	return ((UIOHandle*)handle)->ddr.size();
}

size_t get_nbextmems(void* handle)
{
	if (!is_peripheral_ready)
		throw std::runtime_error(vart_ml_error::exception_message(vart_ml_error::CONFIG_MISSING_PERIPHERALS));

	UIOHandle* uio_handle = (UIOHandle*)handle;

	return uio_handle->ddr.size() + uio_handle->memtiles.size();
}

size_t get_extmemBaseAddr(void* handle, size_t extmemIdx)
{
	if (!is_peripheral_ready)
		throw std::runtime_error(vart_ml_error::exception_message(vart_ml_error::CONFIG_MISSING_PERIPHERALS));

	UIOHandle* uio_handle = (UIOHandle*)handle;

	if (extmemIdx < uio_handle->ddr.size())
		return (size_t)uio_handle->ddr[extmemIdx].phy_addr;

	extmemIdx -= uio_handle->ddr.size();
	return (size_t)uio_handle->memtiles[extmemIdx].phy_addr;
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
	UIOHandle* uio_handle = (UIOHandle*)handle;

	// Print top border
	printf("╔═══════════════════════════════════════════════════════════════════════════════════════╗\n");

	// Print mode title
	printf("║%-87s║\n", "                                     UIO MODE");

	// Print separator after title
	printf("╠══════════════════════╦════════════════════╦════════════════════╦══════════════════════╣\n");

	// Print table header
	printf("║ %-20s ║ %-18s ║ %-18s ║ %-20s ║\n", "NAME", "ADDRESS", "SIZE", "ATTRIBUTE");

	// Print separator
	printf("╠══════════════════════╬════════════════════╬════════════════════╬══════════════════════╣\n");

	// Print DDR resources
	for (size_t i = 0; i < uio_handle->ddr.size(); i++)
	{
		char name[32];
		snprintf(name, sizeof(name), "ddr_%zu", i);
		printf("║ %-20s ║ 0x%016zx ║ 0x%016zx ║ %-20s ║\n",
		       name,
		       uio_handle->ddr[i].phy_addr,
		       uio_handle->ddr[i].size,
		       "-");
	}

	// Print memtiles resources
	for (size_t i = 0; i < uio_handle->memtiles.size(); i++)
	{
		char name[32];
		snprintf(name, sizeof(name), "memtiles_%zu", i);
		printf("║ %-20s ║ 0x%016zx ║ 0x%016zx ║ %-20s ║\n",
		       name,
		       uio_handle->memtiles[i].phy_addr,
		       uio_handle->memtiles[i].size,
		       "-");
	}

	// Print NOC resource
	if (uio_handle->noc.size > 0)
	{
		printf("║ %-20s ║ 0x%016zx ║ 0x%016zx ║ %-20s ║\n",
		       "noc",
		       uio_handle->noc.phy_addr,
		       uio_handle->noc.size,
		       "-");
	}

	// Print IP resources
	size_t ip_count = 0;
	size_t pp_count = 0;
	for (size_t i = 0; i < uio_handle->ip.size(); i++)
	{
		char node_name[32];
		if (uio_handle->ip[i].is_pp)
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
		       uio_handle->ip[i].kernel_name.c_str());

		// Print timestamp
		char timestamp_str[32];
		snprintf(timestamp_str, sizeof(timestamp_str), "0x%08x", uio_handle->ip[i].timestamp);
		printf("║   %-18s ║ %-18s ║ %-18s ║ %-20s ║\n", "timestamp", "-", "-", timestamp_str);

		// Print ctrlbus
		printf("║   %-18s ║ 0x%016zx ║ 0x%016zx ║ %-20s ║\n",
		       "ctrlbus",
		       uio_handle->ip[i].ctrlbus.phy_addr,
		       uio_handle->ip[i].ctrlbus.size,
		       "-");
	}

	// Print bottom border
	printf("╚══════════════════════╩════════════════════╩════════════════════╩══════════════════════╝\n");
}
