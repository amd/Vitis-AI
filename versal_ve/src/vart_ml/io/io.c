/**
 * @file embedded_api_implem.c
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
#include <dlfcn.h>
#include <fcntl.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "io.h"
#include "utils/fpga_info.h"
#include "utils/log.h"
#include "utils/npu_reg.h"

enum connection_status
{
	UNCONNECTED,
	XRT,
	UIO,
	DEVMEM,
	PCI
};

struct npu_device
{
	int (*create_ip_context)(void** handle);
	int (*disconnect_ip)(void* handle, uint32_t ip_idx);
	int (*connect_ip)(void* handle, uint32_t ip_idx);
	int (*connect_peripherals)(void* handle);
	int (*destroy_ip_context)(void* handle);
	int (*get_ip_from_timestamp)(void* handle, uint32_t timestamp, uint32_t* ip_idx);
	int (*get_timestamp_from_ip)(void* handle, uint32_t ip_idx, uint32_t* timestamp);
	int (*get_ip)(void* handle, bool is_pp, uint32_t* ip_idx);
	uint32_t (*get_nb_ip)(void* handle);
	uint32_t (*get_nb_pp)(void* handle);
	int (*get_ip_is_pp)(void* handle, uint32_t ip_idx, bool* is_pp);
	uint32_t (*read_register)(void* handle, uint32_t offset, uint32_t ip_idx);
	void (*write_register)(void* handle, uint32_t offset, uint32_t data, uint32_t ip_idx);
	int (*read_ddr)(void* handle, struct addr addr, uint8_t* buf, uint32_t size);
	int (*write_ddr)(void* handle, struct addr addr, const uint8_t* buf, uint32_t size);
	void (*read_noc)(void* handle, uint64_t offset, uint32_t* buf, uint32_t wordsize);
	void (*write_noc)(void* handle, uint64_t offset, const uint32_t* buf, uint32_t wordsize);
	uint64_t (*get_phy_addr)(void* handle, void* ddr_vaddr);
	void* (*get_vaddr)(void* handle, void* ddr_vaddr);
	void* (*get_base_vaddr)(void* handle, size_t ddrIdx);
	int (*ddr_allocate)(void* handle, uint64_t size, uint32_t ddr, struct addr* addr);
	int (*ddr_allocate_sub)(void*        handle,
	                        void*        parent_addr,
	                        uint64_t     offset,
	                        uint64_t     size,
	                        struct addr* addr);
	void* (*ddr_malloc)(void* handle, size_t ddrIdx, size_t size);
	void* (*ddr_malloc_sub)(void* handle, void* parent_vaddr, size_t offset, size_t size);
	void (*ddr_free)(void* handle, void* ddr_vaddr);
	void (*ddr_sync_to_device)(void* handle, void* buffer);
	void (*ddr_sync_from_device)(void* handle, void* buffer);
	int (*ddr_export_buffer)(void* handle, void* buffer);
	int (*wait_interrupt)(void* handle, uint32_t ip_idx);
	unsigned int (*get_ip_fpga_info_attribute)(void* handle, uint32_t ip_idx, char* attr);
	char* (*get_boardname)(void* handle, uint32_t ip_idx);
	size_t (*get_extmemlen)(void* handle, uint32_t extmemIdx);
	size_t (*get_ddr_free_bytes)(void* handle, size_t ddrIdx);
	size_t (*get_noclen)(void* handle);
	size_t (*get_nbddrs)(void* handle);
	size_t (*get_nbextmems)(void* handle);
	size_t (*get_extmemBaseAddr)(void* handle, uint32_t extmemIdx);
	int (*get_fpgafamily)(void* handle, enum FpgaFamily* family);
	int (*get_architecture)(void* handle, enum FpgaArchitecture* arch);
	void (*print_fpga_info)(void* handle, uint32_t ip_idx);
	void (*print_resource_info)(void* handle);
};

static inline bool is_embedded_platform(void)
{
	void* pci = dlopen("libpci.so", RTLD_LAZY);
	if (pci)
	{
		dlclose(pci);
		return false;
	}
	return true;
}

static inline bool is_xrt_available(void)
{
	void* xrt_lib = dlopen("libxrt.so", RTLD_LAZY);
	if (!xrt_lib)
		return false;

	bool (*xrt_is_available)(void) = dlsym(xrt_lib, "xrt_is_available");
	bool available                 = xrt_is_available && xrt_is_available();
	dlclose(xrt_lib);
	return available;
}

static inline const char* statusToString(enum connection_status status)
{
	switch (status)
	{
	case UNCONNECTED:
		return "UNCONNECTED";
	case XRT:
		return "XRT";
	case UIO:
		return "UIO";
	case DEVMEM:
		return "DEVMEM";
	case PCI:
		return "PCI";
	}

	return "INVALID";
}

static struct npu_device*     device;
static enum connection_status connection_status;
static pthread_mutex_t        connect_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t*       ip_lock;
static enum FpgaArchitecture  arch;

/* Handles for XRT/UIO/DEVMEM calls  */
static void* io_handle  = NULL;
static void* lib_handle = NULL;

static void npu_init_ip_mutex(void)
{
	ip_lock = (pthread_mutex_t*)malloc(npu_get_nb_ip() * sizeof(pthread_mutex_t));
	for (size_t i = 0; i < npu_get_nb_ip(); i++)
		pthread_mutex_init(&ip_lock[i], NULL);
}

static struct npu_device* npu_init_device(void* io_lib)
{
	struct npu_device* npu_device = (struct npu_device*)malloc(sizeof(struct npu_device));

	npu_device->create_ip_context          = dlsym(io_lib, "create_ip_context");
	npu_device->disconnect_ip              = dlsym(io_lib, "disconnect_ip");
	npu_device->connect_ip                 = dlsym(io_lib, "connect_ip");
	npu_device->connect_peripherals        = dlsym(io_lib, "connect_peripherals");
	npu_device->destroy_ip_context         = dlsym(io_lib, "destroy_ip_context");
	npu_device->get_ip_from_timestamp      = dlsym(io_lib, "get_ip_from_timestamp");
	npu_device->get_timestamp_from_ip      = dlsym(io_lib, "get_timestamp_from_ip");
	npu_device->get_ip                     = dlsym(io_lib, "get_ip");
	npu_device->get_nb_ip                  = dlsym(io_lib, "get_nb_ip");
	npu_device->get_nb_pp                  = dlsym(io_lib, "get_nb_pp");
	npu_device->get_ip_is_pp               = dlsym(io_lib, "get_ip_is_pp");
	npu_device->read_register              = dlsym(io_lib, "read_register");
	npu_device->write_register             = dlsym(io_lib, "write_register");
	npu_device->read_ddr                   = dlsym(io_lib, "read_ddr");
	npu_device->write_ddr                  = dlsym(io_lib, "write_ddr");
	npu_device->read_noc                   = dlsym(io_lib, "read_noc");
	npu_device->write_noc                  = dlsym(io_lib, "write_noc");
	npu_device->get_phy_addr               = dlsym(io_lib, "get_phy_addr");
	npu_device->get_vaddr                  = dlsym(io_lib, "get_vaddr");
	npu_device->get_base_vaddr             = dlsym(io_lib, "get_base_vaddr");
	npu_device->ddr_allocate               = dlsym(io_lib, "ddr_allocate");
	npu_device->ddr_allocate_sub           = dlsym(io_lib, "ddr_allocate_sub");
	npu_device->ddr_malloc                 = dlsym(io_lib, "ddr_malloc");
	npu_device->ddr_malloc_sub             = dlsym(io_lib, "ddr_malloc_sub");
	npu_device->ddr_free                   = dlsym(io_lib, "ddr_free");
	npu_device->ddr_sync_to_device         = dlsym(io_lib, "ddr_sync_to_device");
	npu_device->ddr_sync_from_device       = dlsym(io_lib, "ddr_sync_from_device");
	npu_device->ddr_export_buffer          = dlsym(io_lib, "ddr_export_buffer");
	npu_device->wait_interrupt             = dlsym(io_lib, "wait_interrupt");
	npu_device->get_ip_fpga_info_attribute = dlsym(io_lib, "get_ip_fpga_info_attribute");
	npu_device->get_boardname              = dlsym(io_lib, "get_boardname");
	npu_device->get_extmemlen              = dlsym(io_lib, "get_extmemlen");
	npu_device->get_ddr_free_bytes         = dlsym(io_lib, "get_ddr_free_bytes");
	npu_device->get_noclen                 = dlsym(io_lib, "get_noclen");
	npu_device->get_nbddrs                 = dlsym(io_lib, "get_nbddrs");
	npu_device->get_nbextmems              = dlsym(io_lib, "get_nbextmems");
	npu_device->get_extmemBaseAddr         = dlsym(io_lib, "get_extmemBaseAddr");
	npu_device->get_fpgafamily             = dlsym(io_lib, "get_fpgafamily");
	npu_device->get_architecture           = dlsym(io_lib, "get_architecture");
	npu_device->print_fpga_info            = dlsym(io_lib, "print_fpga_info");
	npu_device->print_resource_info        = dlsym(io_lib, "print_resource_info");

	return npu_device;
}

static int npu_destroy_ip_context(void)
{
	int err = device->destroy_ip_context(io_handle);
	dlclose(lib_handle);
	free(ip_lock);
	free(device);

	io_handle         = NULL;
	connection_status = UNCONNECTED;

	return err;
}

int npu_create_ip_context(void)
{
	int                    err = SUCCESS;
	enum connection_status mode;

	pthread_mutex_lock(&connect_lock);

	if (is_embedded_platform())
	{
		if (is_xrt_available() && !check_user_config("xrt.disable"))
			mode = XRT;
		else
			mode = UIO;

		if (check_user_config("uio.disable"))
			mode = DEVMEM;
	}
	else
	{
		mode = PCI;
	}

	/* If already connected with the correct mode, then skip. Else, disconnect. */
	if (mode == connection_status)
	{
		vart_ml_log(LOG_WARN,
		            "Warning: VART ML is already connected using %s. Ignoring this call.\n",
		            statusToString(connection_status));
		goto clean;
	}
	else if (connection_status != UNCONNECTED)
	{
		err = npu_destroy_ip_context();
		if (err)
			goto clean;
	}

	if (mode == XRT)
	{
		if ((lib_handle = dlopen("libxrt.so", RTLD_LAZY)) == NULL)
		{
			vart_ml_log(
			    LOG_ERR, "Failed to load XRT VART ML library: %s => Falling back to UIO.\n", dlerror());
			mode = UIO;
		}
		else
		{
			device = npu_init_device(lib_handle);

			if (device->create_ip_context(&io_handle) != SUCCESS)
			{
				vart_ml_log(LOG_ERR, "Failed to create XRT VART ML context, falling back to UIO.\n");
				dlclose(lib_handle);
				mode = UIO;
			}
			else
				connection_status = mode;
		}
	}

	if (mode == UIO)
	{
		if ((lib_handle = dlopen("libuio.so", RTLD_LAZY)) == NULL)
		{
			vart_ml_log(
			    LOG_ERR, "Failed to load UIO VART ML library: %s => Falling back to /dev/mem\n", dlerror());
			mode = DEVMEM;
		}
		else
		{
			device = npu_init_device(lib_handle);

			if (device->create_ip_context(&io_handle) != SUCCESS)
			{
				vart_ml_log(LOG_ERR, "Failed to create UIO VART ML context, falling back to /dev/mem\n");
				dlclose(lib_handle);
				mode = DEVMEM;
			}
			else
				connection_status = mode;
		}
	}

	if (mode == DEVMEM)
	{
		if ((lib_handle = dlopen("libdevmem.so", RTLD_LAZY)) == NULL)
		{
			err = vart_ml_log_err_msg(LIBRARY_IO_MISSING, "%s.\n", dlerror());
			goto clean;
		}

		device = npu_init_device(lib_handle);

		if ((err = device->create_ip_context(&io_handle)) != SUCCESS)
		{
			vart_ml_log(LOG_ERR, "Failed to create DEVMEM VART ML context. Abort.\n");
			dlclose(lib_handle);
			goto clean;
		}
		else
			connection_status = mode;
	}

	if (mode == PCI)
	{
		if ((lib_handle = dlopen("libpci.so", RTLD_LAZY)) == NULL)
		{
			err = vart_ml_log_err_msg(LIBRARY_IO_MISSING, "%s.\n", dlerror());
			goto clean;
		}

		device = npu_init_device(lib_handle);

		if (device->create_ip_context(&io_handle) != SUCCESS)
		{
			vart_ml_log(LOG_ERR, "Failed to create PCIe VART ML context. Abort.\n");
			dlclose(lib_handle);
			goto clean;
		}
		else
			connection_status = mode;
	}

	npu_init_ip_mutex();

	/*
	 * TODO: replace SW mutex with HW mutex to enable multi-process execution.
	 */
	/*
	for (size_t i = 0; i < npu_get_nb_ip(); i++)
	    npu_release_inference_mutex(i);
	*/

clean:
	pthread_mutex_unlock(&connect_lock);

	return err;
}

int npu_disconnect_ip(uint32_t ip_idx) { return device->disconnect_ip(io_handle, ip_idx); }

int npu_connect_ip(uint32_t ip_idx)
{
	int err = device->connect_ip(io_handle, ip_idx);
	if (err)
		return err;

	bool is_pp;
	err = device->get_ip_is_pp(io_handle, ip_idx, &is_pp);
	if (err)
		return err;

	if (is_pp)
		return SUCCESS;

	return device->get_architecture(io_handle, &arch);
}

int npu_connect_peripherals(void) { return device->connect_peripherals(io_handle); }

int npu_get_ip_from_timestamp(uint32_t timestamp, uint32_t* ip_idx)
{
	return device->get_ip_from_timestamp(io_handle, timestamp, ip_idx);
}

int npu_get_timestamp_from_ip(uint32_t ip_idx, uint32_t* timestamp)
{
	return device->get_timestamp_from_ip(io_handle, ip_idx, timestamp);
}

int npu_get_ip(bool is_pp, uint32_t* ip_idx) { return device->get_ip(io_handle, is_pp, ip_idx); }

uint32_t npu_get_nb_ip(void) { return device->get_nb_ip(io_handle); }

uint32_t npu_get_nb_pp(void) { return device->get_nb_pp(io_handle); }

void npu_read_cfg(uint64_t offset, uint32_t* buf, uint32_t wordsize, uint32_t ip_idx)
{
	for (size_t i = 0; i < wordsize; i++)
		*buf++ = device->read_register(io_handle, (offset + 4 * i), ip_idx);
}

void npu_write_cfg(uint64_t offset, const uint32_t* buf, uint32_t wordsize, uint32_t ip_idx)
{
	for (size_t i = 0; i < wordsize; i++)
		device->write_register(io_handle, (offset + 4 * i), *buf++, ip_idx);
}

/* VART ML's DDR doesn't like accesses that are not self-aligned. Normal PS DDR
 * accesses don't have this constraint, which is why memcpy will sometimes
 * use 64-bit transfers on offsets that are not aligned to 64 bits.
 */
int npu_read_ddr(struct addr addr, uint8_t* buf, uint32_t size)
{
	return device->read_ddr(io_handle, addr, buf, size);
}

int npu_write_ddr(struct addr addr, const uint8_t* buf, uint32_t size)
{
	return device->write_ddr(io_handle, addr, buf, size);
}

void npu_read_noc(uint64_t offset, uint32_t* buf, uint32_t wordsize)
{
	device->read_noc(io_handle, offset, buf, wordsize);
}

void npu_write_noc(uint64_t offset, const uint32_t* buf, uint32_t wordsize)
{
	device->write_noc(io_handle, offset, buf, wordsize);
}

size_t npu_get_ddr_idx_from_phy(uint64_t phy_addr)
{
	for (size_t i = 0; i < npu_get_nbddrs(); i++)
	{
		size_t base = npu_get_extmemBaseAddr(i);
		if (base <= phy_addr && phy_addr < base + npu_get_extmemlen(i))
			return i;
	}
	return 0;
}

uint64_t npu_get_nbuff_conf_val_from_phy(uint64_t phy_addr)
{
	if (arch != AIEML_V1C)
	{
		/* Search only DDR banks (not memtiles) since NBUFF entries always hold DDR addresses. */
		size_t ddr_idx = npu_get_ddr_idx_from_phy(phy_addr);
		size_t base    = npu_get_extmemBaseAddr(ddr_idx);
		return (phy_addr - base) >> 6;
	}

	return phy_addr;
}

static uint64_t npu_get_nbuff_conf_val_from_idx_and_offset(uint8_t extmem_idx, uint64_t offset)
{
	if (arch != AIEML_V1C)
		return offset >> 6;

	return npu_get_extmemBaseAddr(extmem_idx) + offset;
}

struct addr npu_get_addr_from_phy_addr(uint64_t phy_addr)
{
	struct addr addr;
	// default in case of error
	addr.ddr_vaddr      = NULL;
	addr.offset         = 0;
	addr.phy_addr       = 0;
	addr.nbuff_conf_val = 0;

	for (size_t i = 0; i < npu_get_nbextmems(); i++)
	{
		size_t memBase = npu_get_extmemBaseAddr(i);
		if (memBase <= phy_addr && phy_addr < memBase + npu_get_extmemlen(i))
		{
			addr.ddr_vaddr      = device->get_base_vaddr(io_handle, i);
			addr.offset         = phy_addr - memBase;
			addr.phy_addr       = phy_addr;
			addr.nbuff_conf_val = npu_get_nbuff_conf_val_from_idx_and_offset(i, addr.offset);

			break;
		}
	}

	return addr;
}

int npu_get_ddr_idx_from_phy_addr(uint64_t phy_addr, uint8_t* idx)
{
	for (size_t i = 0; i < npu_get_nbextmems(); i++)
	{
		size_t memBase = npu_get_extmemBaseAddr(i);
		if (memBase <= phy_addr && phy_addr < memBase + npu_get_extmemlen(i))
		{
			*idx = i;
			return SUCCESS;
		}
	}

	return DEVICE_UNEXPECTED_ARG_VALUE;
}

struct addr npu_get_addr_from_idx_and_offset(uint8_t extmem_idx, uint64_t offset)
{
	struct addr addr;
	addr.ddr_vaddr = device->get_base_vaddr(io_handle, extmem_idx);
	addr.offset    = offset;

	addr.phy_addr = npu_get_extmemBaseAddr(extmem_idx) + offset;

	addr.nbuff_conf_val = npu_get_nbuff_conf_val_from_idx_and_offset(extmem_idx, offset);

	return addr;
}

uint64_t npu_get_phy_addr_from_ddr_vaddr(const void* ddr_vaddr)
{
	return device->get_phy_addr(io_handle, (void*)ddr_vaddr);
}

void* npu_get_ddr_vaddr_from_vaddr(void* ddr_vaddr) { return device->get_vaddr(io_handle, ddr_vaddr); }

uint64_t npu_get_phy_addr_from_snapshot_addr(uint64_t snap_addr)
{
	size_t phy_addr = snap_addr;

	if (arch != AIEML_V1C)
	{
		const size_t DDR_OFFSET = npu_get_extddroffset();
		const size_t DDR_INDEX  = phy_addr / DDR_OFFSET;
		/* Offset we get here is in the scale of 48GB address space.
		 * Convert it to the offset in respective DDR
		 */
		phy_addr = npu_get_extmemBaseAddr(DDR_INDEX) + (phy_addr & (DDR_OFFSET - 1));
	}

	return phy_addr;
}

int npu_allocate_memory(uint64_t size, uint32_t ddr, struct addr* addr)
{
	if (ddr >= npu_get_nbddrs())
		return vart_ml_log_err_msg(DEVICE_BAD_DDR_INDEX,
		                           "Failed to allocate memory in DDR %u: the system has %zu DDRs only.\n",
		                           ddr,
		                           npu_get_nbddrs());

	return device->ddr_allocate(io_handle, size, ddr, addr);
}

int npu_allocate_sub(struct addr* parent_addr, uint64_t offset, uint64_t size, struct addr* addr)
{
	return device->ddr_allocate_sub(io_handle, parent_addr, offset, size, addr);
}

void* npu_malloc(uint64_t size, uint8_t ddr)
{
	if (ddr >= npu_get_nbddrs())
	{
		vart_ml_log(LOG_ERR,
		            "Failed to allocate memory in DDR %u: the system has %zu DDRs only.\n",
		            ddr,
		            npu_get_nbddrs());
		return NULL;
	}

	return device->ddr_malloc(io_handle, ddr, size);
}

void* npu_malloc_sub(void* parent_vaddr, size_t offset, size_t size)
{
	return device->ddr_malloc_sub(io_handle, parent_vaddr, offset, size);
}

void npu_free(void* user_ptr) { device->ddr_free(io_handle, user_ptr); }

void npu_sync_buffer_to_device(void* buffer) { device->ddr_sync_to_device(io_handle, buffer); }

void npu_sync_buffer_from_device(void* buffer) { device->ddr_sync_from_device(io_handle, buffer); }

int npu_export_buffer(void* buffer) { return device->ddr_export_buffer(io_handle, buffer); }

int npu_wait_interrupt(uint32_t ip_idx) { return device->wait_interrupt(io_handle, ip_idx); }

int npu_is_interrupt_en(bool* is_en)
{
	if (connection_status == XRT)
		*is_en = !check_user_config("polling.enable");
	else
		*is_en = false;

	return SUCCESS;
}

bool npu_is_xrt_en(void) { return connection_status == XRT; }
bool npu_is_devmem_en(void) { return connection_status == DEVMEM; }
bool npu_is_embedded(void) { return connection_status != PCI; }

int npu_get_mutex(uint64_t offset, uint32_t ip_idx)
{
	/* If architecture is NOT V1C, do nothing. */
	if (arch != AIEML_V1C)
		return SUCCESS;

	/* Attempt CTRLBus mutex acquisition */
	uint32_t mutex_reg;
	npu_read_cfg(VERSAL_MCTRL_MEM_NPU_MUTEX_BASEADDR + offset, &mutex_reg, 1, ip_idx);

	if (mutex_reg != 0)
		return DEVICE_MUTEX_ACQUISITION_FAILURE;
	return SUCCESS;
}

void npu_release_mutex(uint64_t offset, uint32_t ip_idx)
{
	if (arch != AIEML_V1C)
		return;

	const uint32_t mutex_reg = 0;
	npu_write_cfg(VERSAL_MCTRL_MEM_NPU_MUTEX_BASEADDR + offset, &mutex_reg, 1, ip_idx);
}

void npu_get_inference_mutex(uint32_t ip_idx)
{
	/*
	 * TODO: replace SW mutex with HW mutex to enable multi-process execution.
	 */
	/*
	while (npu_get_mutex(VERSAL_MCTRL_NPU_INFERENCE_MUTEX_OFFSET, ip_idx) != SUCCESS)
	    usleep(100);
	*/
	pthread_mutex_lock(&ip_lock[ip_idx]);
}

void npu_release_inference_mutex(uint32_t ip_idx)
{
	/*
	 * TODO: replace SW mutex with HW mutex to enable multi-process execution.
	 */
	/*
	npu_release_mutex(VERSAL_MCTRL_NPU_INFERENCE_MUTEX_OFFSET, ip_idx);
	*/
	pthread_mutex_unlock(&ip_lock[ip_idx]);
}

int npu_cleanup_inference_operation(uint32_t ip_idx, int err)
{
	npu_release_inference_mutex(ip_idx);
	return err;
}

/* FPGA INFO ACCESSORS */

void npu_print_fpga_info(uint32_t ip_idx) { return device->print_fpga_info(io_handle, ip_idx); }

unsigned int npu_get_inputfreq(uint32_t ip_idx)
{
	return device->get_ip_fpga_info_attribute(io_handle, ip_idx, "clocks.clk4xInput");
}

unsigned int npu_get_runningfreq(uint32_t ip_idx)
{
	return device->get_ip_fpga_info_attribute(io_handle, ip_idx, "clocks.clk4x");
}

unsigned int npu_get_ctrlbusfreq(uint32_t ip_idx)
{
	return device->get_ip_fpga_info_attribute(io_handle, ip_idx, "clocks.clkServiceBus");
}

unsigned int npu_get_mntrclkcoef(uint32_t ip_idx)
{
	return device->get_ip_fpga_info_attribute(io_handle, ip_idx, "clocks.frequencyMeterCoef");
}

size_t npu_get_nbcores(uint32_t ip_idx)
{
	return device->get_ip_fpga_info_attribute(io_handle, ip_idx, "fpga.cores");
}
size_t npu_get_nbsystems(uint32_t ip_idx)
{
	return device->get_ip_fpga_info_attribute(io_handle, ip_idx, "fpga.systems");
}
size_t npu_get_nbnces(uint32_t ip_idx)
{
	return device->get_ip_fpga_info_attribute(io_handle, ip_idx, "fpga.nces");
}

size_t npu_get_nbcolumns(uint32_t ip_idx)
{
	return device->get_ip_fpga_info_attribute(io_handle, ip_idx, "mapping.nbColumns");
}
size_t npu_get_nbaiepercolumn(uint32_t ip_idx)
{
	return device->get_ip_fpga_info_attribute(io_handle, ip_idx, "mapping.nbAiePerColumn");
}

bool is_tempmonitor_en(uint32_t ip_idx)
{
	return device->get_ip_fpga_info_attribute(io_handle, ip_idx, "serviceBus.monitoringTemperature");
}

bool is_assert_en(uint32_t ip_idx)
{
	return device->get_ip_fpga_info_attribute(io_handle, ip_idx, "serviceBus.assert");
}

bool is_license_en(uint32_t ip_idx)
{
	return device->get_ip_fpga_info_attribute(io_handle, ip_idx, "serviceBus.license");
}

bool is_splitCore_en(uint32_t ip_idx)
{
	return device->get_ip_fpga_info_attribute(io_handle, ip_idx, "functions.splitCore");
}

bool is_mmcm_en(uint32_t ip_idx)
{
	return device->get_ip_fpga_info_attribute(io_handle, ip_idx, "serviceBus.clockDynReconfig");
}

bool is_debug_srv_ddr_en(uint32_t ip_idx)
{
	return device->get_ip_fpga_info_attribute(io_handle, ip_idx, "serviceBus.debugDdr");
}

const char* npu_get_boardname(uint32_t ip_idx) { return device->get_boardname(io_handle, ip_idx); }

size_t npu_get_extmemlen(size_t extmemIdx) { return device->get_extmemlen(io_handle, extmemIdx); }

size_t npu_get_ddr_free_bytes(size_t ddrIdx) { return device->get_ddr_free_bytes(io_handle, ddrIdx); }

size_t npu_get_noclen(void) { return device->get_noclen(io_handle); }

size_t npu_get_nbddrs() { return device->get_nbddrs(io_handle); }

size_t npu_get_nbextmems() { return device->get_nbextmems(io_handle); }

size_t npu_get_extmemBaseAddr(size_t extmemIdx) { return device->get_extmemBaseAddr(io_handle, extmemIdx); }

/* Only meaningful on Versal */
// sw::dutils::externalMemory_offset is hardcoded to 16 * 1024 * 1024 * 1024
size_t npu_get_extddroffset() { return (unsigned long long)16 * 1024 * 1024 * 1024; }

uint32_t npu_get_aligned_size(uint32_t size) { return (size + (1 << 8) - 1) & ~((1 << 8) - 1); }

int npu_get_fpgafamily(enum FpgaFamily* family) { return device->get_fpgafamily(io_handle, family); }

int npu_get_architecture(enum FpgaArchitecture* arch) { return device->get_architecture(io_handle, arch); }

void npu_print_resource_info(void) { return device->print_resource_info(io_handle); }

/*
 * Note: it is the user's responsibility to remove the inline keyword during
 * debug.
 */
inline void vart_ml_vprintf(const char* fmt, va_list args) { vprintf(fmt, args); }
