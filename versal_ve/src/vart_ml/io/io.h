/**
 * @file io.h
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

#ifndef IO_H
#define IO_H

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "utils/fpga_info.h"

#ifdef __cplusplus
extern "C"
{
#else
#endif

	/**
	 * @brief Structure representing a DDR address.
	 *
	 * This structure contains the pointer to the beginning of the mapped space in DDR, the physical address,
	 * and the offset in this virtual DDR.
	 *
	 */
	struct addr
	{
		void*    ddr_vaddr;
		uint64_t phy_addr;
		uint64_t offset;
		uint64_t nbuff_conf_val;
	};

	/**
	 * @brief Create the NPU IP execution context.
	 *
	 * Explore the device and extract NPU IP description information.
	 * In XRT, connecting to IPs is required to access IP information; once exploration is done, the
	 * connection is terminated.
	 *
	 * @return int The Vart ML error (vart_ml_error_id) code of the operation.
	 */
	int npu_create_ip_context(void);

	/**
	 * @brief Disconnect from IP.
	 *
	 * This function will remove connection with the target IP.
	 *
	 * @param ip_idx Index of the IP being accessed.
	 * @return int The Vart ML error (vart_ml_error_id) code of the operation.
	 */
	int npu_disconnect_ip(uint32_t ip_idx);

	/**
	 * @brief Establish connection to IP.
	 *
	 * This function will establish a connection with the target IP CTRLBUS.
	 *
	 * @param ip_idx Index of the IP being accessed.
	 * @return int The Vart ML error (vart_ml_error_id) code of the operation.
	 */
	int npu_connect_ip(uint32_t ip_idx);

	/**
	 * @brief Establish connection with platform peripherals.
	 *
	 * This function will establish a connection to access peripherals such as ddr, noc, memtiles, etc.
	 *
	 * @return int The Vart ML error (vart_ml_error_id) code of the operation.
	 */
	int npu_connect_peripherals(void);

	/**
	 * @brief Retrieve the IP index associated to a timestamp.
	 *
	 * @param timestamp Timestamp of a snapshot generated for one of the IP on the board.
	 * @param ip_idx Pointer to the IP index returned.
	 * @return int The Vart ML error (vart_ml_error_id) code of the operation.
	 */
	int npu_get_ip_from_timestamp(uint32_t timestamp, uint32_t* ip_idx);

	/**
	 * @brief Retrieve the timestamp associated to IP.
	 *
	 * @param handle Struct with connection information.
	 * @param ip_idx Index of the IP being accessed.
	 * @param timestamp Pointer to the timestamp returned.
	 * @return int The Vart ML error (vart_ml_error_id) code of the operation.
	 */
	int npu_get_timestamp_from_ip(uint32_t ip_idx, uint32_t* timestamp);

	/**
	 * @brief Retrieve the IP that verifies attribute is_pp. Returns an error if more that one IP exists.
	 *
	 * @param is_pp Flag used to specifie the nature of IP fetched.
	 * @param ip_idx Pointer to the IP index returned.
	 * @return int The Vart ML error (vart_ml_error_id) code of the operation.
	 */
	int npu_get_ip(bool is_pp, uint32_t* ip_idx);

	/**
	 * @brief Return the number of IPs present on the board.
	 *
	 * @return uint32_t Return the number of IPs.
	 */
	uint32_t npu_get_nb_ip(void);

	/**
	 * @brief Return the number of PP_engines present on the board.
	 *
	 * @return uint32_t Return the number of PP_engines.
	 */
	uint32_t npu_get_nb_pp(void);

	/**
	 * @name Memory access.
	 *
	 * Function to access (read or write) the different memory region of the system: Service Bus, DDR or NOC.
	 *
	 * @param offset Address in the accessed memory.
	 * @param buf Address of the data buffer.
	 * @param wordsize Number of word to access in memory.
	 * @param ip_idx index of the IP being accessed.
	 * @return int The Vart ML error (vart_ml_error_id) code of the operation.
	 */
	//@{
	void npu_read_cfg(uint64_t offset, uint32_t* buf, uint32_t wordsize, uint32_t ip_idx);
	void npu_write_cfg(uint64_t offset, const uint32_t* buf, uint32_t wordsize, uint32_t ip_idx);
	void npu_read_noc(uint64_t offset, uint32_t* buf, uint32_t wordsize);
	void npu_write_noc(uint64_t offset, const uint32_t* buf, uint32_t wordsize);
	//@}

	/**
	 * @name Memory access.
	 *
	 * Function to access (read or write) the different memory region of the system: Service Bus, DDR or NOC.
	 *
	 * @param addr Virtual address in the accessed memory.
	 * @param buf Address of the data buffer.
	 * @param size Size in bytes to access in memory.
	 * @return int The Vart ML error (vart_ml_error_id) code of the operation.
	 */
	//@{
	int npu_read_ddr(struct addr addr, uint8_t* buf, uint32_t size);
	int npu_write_ddr(struct addr addr, const uint8_t* buf, uint32_t size);
	//@}

	/**
	 * @brief Return the DDR bank index that contains the given physical address.
	 *
	 * @param phy_addr Physical address of the buffer.
	 * @return size_t DDR bank index (0 if not found).
	 */
	size_t npu_get_ddr_idx_from_phy(uint64_t phy_addr);

	/**
	 * @brief Convert DDR physical address (NOC offset) in nbuff configuration value.
	 *
	 * @param phy_addr Physical address of the allocated buffer.
	 * @return uint64_t Return the nbuff configuration value.
	 */
	uint64_t npu_get_nbuff_conf_val_from_phy(uint64_t phy_addr);

	/**
	 * @brief Convert DDR physical address (NOC offset) in DDR address.
	 *
	 * @param phy_addr Physical address of the allocated buffer.
	 * @return struct addr Return the DDR address.
	 */
	struct addr npu_get_addr_from_phy_addr(uint64_t phy_addr);

	/**
	 * @brief Retrieve DDR index (NOC offset) from DDR physical address.
	 *
	 * @param phy_addr Physical address of the allocated buffer.
	 * @param idx Index of the DDR where the physical address belongs.
	 * @return int The Vart ML error (vart_ml_error_id) code of the operation.
	 */
	int npu_get_ddr_idx_from_phy_addr(uint64_t phy_addr, uint8_t* idx);

	/**
	 * @brief Convert external memory (DDR and memtile) index and offset to virtual address.
	 *
	 * @param extmem_idx External memory index.
	 * @param offset Offset in the memory.
	 * @return struct addr Return the virtual address structure.
	 */
	struct addr npu_get_addr_from_idx_and_offset(uint8_t extmem_idx, uint64_t offset);

	/**
	 * @brief Convert DDR virtual address in Physical DDR address (NOC offset).
	 *
	 * @param ddr_vaddr Pointer to the mapped virtual buffer.
	 * @return uint64_t Return the physical DDR address.
	 */
	uint64_t npu_get_phy_addr_from_ddr_vaddr(const void* ddr_vaddr);

	/**
	 * @brief Convert virtual address pointer in DDR virtual address.
	 *
	 * @param ddr_vaddr Pointer to the mapped virtual buffer.
	 * @return void* Return the DDR virtual address.
	 */
	void* npu_get_ddr_vaddr_from_vaddr(void* ddr_vaddr);

	/**
	 * @brief Convert DDR virtual address read from snapshot in Physical DDR address (NOC offset).
	 *
	 * @param snap_addr Address read from the snapshot.
	 * @return uint64_t Return the physical DDR address.
	 */
	uint64_t npu_get_phy_addr_from_snapshot_addr(uint64_t snap_addr);

	/**
	 * @brief Allocate a buffer in DDR.
	 *
	 * @param size Size of the buffer.
	 * @param ddr Select the DDR in which the buffer will be allocated.
	 * @param addr DDR address of the allocated buffer.
	 * @return int The Vart ML error (vart_ml_error_id) code of the operation.
	 */
	int npu_allocate_memory(uint64_t size, uint32_t ddr, struct addr* addr);

	/**
	 * @brief Register a sub-buffer view of a parent DDR allocation.
	 *
	 * Does not allocate new memory. Registers parent_addr->vaddr+offset into the
	 * IO backend's address maps. The sub-buffer must be freed with npu_free.
	 *
	 * @param parent_addr DDR address returned by a prior npu_allocate_memory call.
	 * @param offset Byte offset into the parent buffer.
	 * @param size Size of the sub-buffer in bytes.
	 * @param addr DDR address of the registered sub buffer.
	 * @return int The Vart ML error (vart_ml_error_id) code of the operation.
	 */
	int npu_allocate_sub(struct addr* parent_addr, uint64_t offset, uint64_t size, struct addr* addr);

	/**
	 * @brief Allocate a buffer in DDR.
	 *
	 * @param size Size of the buffer.
	 * @param ddr Select the DDR in which the buffer will be allocated.
	 * @return Pointer to the mapped DDR buffer.
	 */
	void* npu_malloc(uint64_t size, uint8_t ddr);

	/**
	 * @brief Register a sub-buffer view of a parent DDR allocation.
	 *
	 * Does not allocate new memory. Registers parent_vaddr+offset into the
	 * IO backend's address maps so that npu_get_phy_addr_from_ddr_vaddr works
	 * on the returned pointer. The sub-buffer must be freed with npu_free.
	 *
	 * @param parent_vaddr Virtual address returned by a prior npu_malloc call.
	 * @param offset Byte offset into the parent buffer.
	 * @param size Size of the sub-buffer in bytes.
	 * @return Pointer to the sub-buffer virtual address, or NULL on failure.
	 */
	void* npu_malloc_sub(void* parent_vaddr, size_t offset, size_t size);

	/**
	 * @brief Free a previously mapped buffer.
	 *
	 * @param user_ptr Pointer to the mapped DDR buffer.
	 */
	void npu_free(void* user_ptr);

	/**
	 * @brief Synchronize a buffer to the device.
	 *
	 * @param buffer Pointer to the buffer to synchronize.
	 */
	void npu_sync_buffer_to_device(void* buffer);

	/**
	 * @brief Synchronize a buffer from the device.
	 *
	 * @param buffer Pointer to the buffer to synchronize.
	 */
	void npu_sync_buffer_from_device(void* buffer);

	/**
	 * @brief Export a buffer as a dma-buf file descriptor.
	 *
	 * @param buffer Pointer to the buffer to export.
	 * @return File descriptor on success, or -1 if not supported.
	 */
	int npu_export_buffer(void* buffer);

	/**
	 * @brief Wait for the end of inference interrupt.
	 *
	 * @param ip_idx index of the IP being accessed.
	 * @return int The Vart ML error (vart_ml_error_id) code of the operation.
	 */
	int npu_wait_interrupt(uint32_t ip_idx);

	/**
	 * @brief Check if interrupt is enable.
	 *
	 * @return int The Vart ML error (vart_ml_error_id) code of the operation.
	 */
	int npu_is_interrupt_en(bool* is_en);

	/**
	 * @brief Check if XRT is enable.
	 *
	 * @return Return true if the connection status is XRT.
	 */
	bool npu_is_xrt_en(void);

	/**
	 * @brief Check if DEVMEM is enabled.
	 *
	 * @return Return true if the connection status is DEVMEM.
	 */
	bool npu_is_devmem_en(void);

	/**
	 * @brief Return true if running on an embedded platform (UIO/DEVMEM/XRT), false on PCIe host.
	 */
	bool npu_is_embedded(void);

	/**
	 * @brief Get CTRLBus mutex.
	 *
	 * @param offset Offset of the targeted mutex.
	 * @param ip_idx index of the IP being accessed.
	 *
	 * @return int The Vart ML error (vart_ml_error_id) code of the operation.
	 */
	int npu_get_mutex(uint64_t offset, uint32_t ip_idx);

	/**
	 * @brief Release CTRLBus mutex.
	 *
	 * @param offset Offset of the targeted mutex.
	 * @param ip_idx index of the IP being accessed.
	 */
	void npu_release_mutex(uint64_t offset, uint32_t ip_idx);

	/**
	 * @brief Get the inference mutex. Blocks until acquired.
	 *
	 * @param ip_idx index of the IP being accessed.
	 */
	void npu_get_inference_mutex(uint32_t ip_idx);

	/**
	 * @brief Release the inference mutex.
	 *
	 * @param ip_idx index of the IP being accessed.
	 */
	void npu_release_inference_mutex(uint32_t ip_idx);

	/**
	 * @brief Release of the inference mutex with a return of an error value.
	 *
	 * @param ip_idx index of the IP being accessed.
	 * @param err the error that caused the need to cleanup the mutex.
	 *
	 * @return int The Vart ML error (vart_ml_error_id) code of the operation.
	 */
	int npu_cleanup_inference_operation(uint32_t ip_idx, int err);

	/**
	 * @brief Print the fpga_info file content associated to target IP.
	 *
	 */
	void npu_print_fpga_info(uint32_t ip_idx);

	/**
	 * @brief Return the size of the external memory with index extmemIdx.
	 *
	 */
	size_t npu_get_extmemlen(size_t extmemIdx);

	/**
	 * @brief Return the number of free bytes in DDR bank ddrIdx.
	 *
	 */
	size_t npu_get_ddr_free_bytes(size_t ddrIdx);

	/**
	 * @brief Return the size of the NOC address space.
	 *
	 */
	size_t npu_get_noclen(void);

	/**
	 * @brief Return the input frequency of the FPGA.
	 *
	 */
	unsigned int npu_get_inputfreq(uint32_t idx);

	/**
	 * @brief Return the running frequency of the FPGA.
	 *
	 */
	unsigned int npu_get_runningfreq(uint32_t idx);

	/**
	 * @brief Return the Service Bus frequency of the FPGA.
	 *
	 */
	unsigned int npu_get_ctrlbusfreq(uint32_t idx);

	/**
	 * @brief Get the reference clock coefficient.
	 *
	 * @return unsigned int Return the reference clock frequency times the divider.
	 */
	unsigned int npu_get_mntrclkcoef(uint32_t idx);

	/**
	 * @brief Get the FPGA name.
	 *
	 * @return Return the FPGA name.
	 */
	const char* npu_get_boardname(uint32_t idx);

	/**
	 * @brief Return the number of core in the FPGA.
	 *
	 */
	size_t npu_get_nbcores(uint32_t idx);

	/**
	 * @brief Return the number of system in the FPGA.
	 *
	 */
	size_t npu_get_nbsystems(uint32_t idx);

	/**
	 * @brief Return the number of NCE in the FPGA.
	 *
	 */
	size_t npu_get_nbnces(uint32_t idx);

	/**
	 * @brief Return the number of Columns of AIE.
	 *
	 */
	size_t npu_get_nbcolumns(uint32_t idx);

	/**
	 * @brief Return the number of AIE per column.
	 *
	 */
	size_t npu_get_nbaiepercolumn(uint32_t idx);

	/**
	 * @brief Return the number of DDR in the FPGA.
	 *
	 */
	size_t npu_get_nbddrs(void);

	/**
	 * @brief Return the number of external memories in the FPGA.
	 *
	 */
	size_t npu_get_nbextmems(void);

	/**
	 * @brief Return the base address of a the DDR with index ddrIdx.
	 *
	 */
	size_t npu_get_extmemBaseAddr(size_t ddrIdx);

	/**
	 * @brief Return a 256-bit aligned size.
	 *
	 * @param size size to be aligned.
	 * @return size_t Aligned size.
	 */
	uint32_t npu_get_aligned_size(uint32_t size);

	/**
	 * @brief Get the FPGA family.
	 *
	 * @param family Holds the returned fpga family enum: VERSAL or ZYNQ.
	 * @return int The Vart ML error (vart_ml_error_id) code of the operation.
	 */
	int npu_get_fpgafamily(enum FpgaFamily* family);

	/**
	 * @brief Get the FPGA architecture.
	 *
	 * @param arch Holds returned fpga architecture: V1 for Zynq family or the Versal AIE architecture.
	 * @return int The Vart ML error (vart_ml_error_id) code of the operation.
	 */
	int npu_get_architecture(enum FpgaArchitecture* arch);

	/**
	 * @brief Return the offset between 2 DDR.
	 *
	 */
	size_t npu_get_extddroffset(void);

	/**
	 * @brief Check if the temperature monitoring is enable.
	 *
	 * @return bool Return true is the temperature monitoring is enable, false otherwise.
	 */
	bool is_tempmonitor_en(uint32_t idx);

	/**
	 * @brief Check if the license module is enable.
	 *
	 * @return bool Return true is the license module is enable, false otherwise.
	 */
	bool is_license_en(uint32_t idx);

	/**
	 * @brief Check if the assertion module is enable.
	 *
	 * @return bool Return true is the assertion module is enable, false otherwise.
	 */
	bool is_assert_en(uint32_t idx);

	/**
	 * @brief Check if the split core functionality is enable.
	 *
	 * @return bool Return true is functionality is enable, false otherwise.
	 */
	bool is_splitCore_en(uint32_t idx);

	/**
	 * @brief Check if the MMCM is accessible through the service bus.
	 *
	 * @return bool Return true is functionality is enable, false otherwise.
	 */
	bool is_mmcm_en(uint32_t idx);

	/**
	 * @brief Check if the DDR debug module is present in the design.
	 *
	 * Accessing its registers when it is absent leaves the service bus without a
	 * response, which deadlocks the control bus, so always check this first.
	 *
	 * @return bool Return true is the module is present, false otherwise.
	 */
	bool is_debug_srv_ddr_en(uint32_t idx);

	/**
	 * @brief Print all detected resources in a table format.
	 *
	 */
	void npu_print_resource_info(void);

	/**
	 * @brief The print method used by internal logging scheme.
	 *
	 * @param fmt C string that contains a format string that follows the same specifications as format in
	 * printf
	 * @param arg A value identifying a variable arguments list initialized with va_start.
	 */
	void vart_ml_vprintf(const char* fmt, va_list args);

#ifdef __cplusplus
}
#endif

#endif
