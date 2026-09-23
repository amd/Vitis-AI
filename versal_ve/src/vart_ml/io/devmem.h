/**
 * @file devmem.h
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

#ifndef DEVMEM_H
#define DEVMEM_H

#include <stddef.h>
#include <stdint.h>

#include "utils/fpga_info.h"

#ifdef __cplusplus
extern "C"
{
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
	 * @brief Initialize and explore the XRT IP context.
	 *
	 * This function will retrieve IP related information and store it in the handle.
	 *
	 * @param handle Struct with IP context information.
	 * @return int The Vart ML error (vart_ml_error_id) code of the operation.
	 */
	int create_ip_context(void** handle);

	/**
	 * @brief Disconnect from IP.
	 *
	 * This function will remove connection with the target IP.
	 *
	 * @param handle Struct with IP context information.
	 * @param ip_idx Index of the IP being accessed.
	 * @return int The Vart ML error (vart_ml_error_id) code of the operation.
	 */
	int disconnect_ip(void* handle, uint32_t ip_idx);

	/**
	 * @brief Establish connection to IP.
	 *
	 * This function will establish a connection with the target IP.
	 *
	 * @param handle Struct with IP context information.
	 * @param ip_idx Index of the IP being accessed.
	 * @return int The Vart ML error (vart_ml_error_id) code of the operation.
	 */
	int connect_ip(void* handle, uint32_t ip_idx);

	/**
	 * @brief Establish connection with platform peripherals.
	 *
	 * This function will establish a connection to access peripherals such as ddr, noc, memtiles, etc.
	 *
	 * @param handle Struct with IP context information.
	 * @return int The Vart ML error (vart_ml_error_id) code of the operation.
	 */
	int connect_peripherals(void* handle);

	/**
	 * @brief Retrieve the IP index associated to a timestamp.
	 *
	 * @param handle Struct with connection information.
	 * @param timestamp Timestamp of a snapshot generated for one of the IP on the board.
	 * @param ip_idx Pointer to the IP index returned.
	 * @return int The Vart ML error (vart_ml_error_id) code of the operation.
	 */
	int get_ip_from_timestamp(void* handle, uint32_t timestamp, uint32_t* ip_idx);

	/**
	 * @brief Retrieve the timestamp associated to IP.
	 *
	 * @param handle Struct with connection information.
	 * @param ip_idx Index of the IP being accessed.
	 * @param timestamp Pointer to the timestamp returned.
	 * @return int The Vart ML error (vart_ml_error_id) code of the operation.
	 */
	int get_timestamp_from_ip(void* handle, uint32_t ip_idx, uint32_t* timestamp);

	/**
	 * @brief Retrieve the IP that verifies attribute is_pp. Returns an error if more that one IP exists.
	 *
	 * @param handle Struct with connection information.
	 * @param is_pp Flag used to specifie the nature of IP fetched.
	 * @param ip_idx Pointer to the IP index returned.
	 * @return int The Vart ML error (vart_ml_error_id) code of the operation.
	 */
	int get_ip(void* handle, bool is_pp, uint32_t* ip_idx);

	/**
	 * @brief Return the number of IPs present on the board.
	 *
	 * @param handle Struct with connection information.
	 * @return uint32_t Return the number of IPs.
	 */
	uint32_t get_nb_ip(void* handle);

	/**
	 * @brief Return the number of PP_engines present on the board.
	 *
	 * @param handle Struct with connection information.
	 * @return uint32_t Return the number of PP_enginess.
	 */
	uint32_t get_nb_pp(void* handle);

	/**
	 * @brief Return whether the IP at ip_idx is a PP engine.
	 *
	 * @param handle Struct with connection information.
	 * @param ip_idx Index of the IP being queried.
	 * @param is_pp  Pointer to the boolean result.
	 * @return int The Vart ML error (vart_ml_error_id) code of the operation.
	 */
	int get_ip_is_pp(void* handle, uint32_t ip_idx, bool* is_pp);

	/**
	 * @name Service Bus access.
	 *
	 * Function to access (read or write) the Service Bus.
	 *
	 * @param handle Struct with connection information.
	 * @param offset offset in the service bus.
	 * @param data the uint32_t word which will be written in the service bus.
	 * @param ip_idx index of the IP being accessed.
	 * @return the uint32_t word read from the service bus.
	 */
	//@{
	uint32_t read_register(void* handle, uint32_t offset, uint32_t ip_idx);
	void     write_register(void* handle, uint32_t offset, uint32_t data, uint32_t ip_idx);
	//@}

	/**
	 * @name DDR access.
	 *
	 * Function to access (read or write) the DDRs.
	 *
	 * @param handle Struct with connection information.
	 * @param addr DDR address of the allocated buffer.
	 * @param buf pointer to the input/output data buffer.
	 * @param size Size in bytes to access in memory.
	 * @return int The Vart ML error (vart_ml_error_id) code of the operation.
	 */
	//@{
	int read_ddr(void* handle, struct addr addr, uint8_t* buf, uint32_t size);
	int write_ddr(void* handle, struct addr addr, const uint8_t* buf, uint32_t size);
	//@}

	void read_noc(void* handle, uint64_t offset, uint32_t* buf, uint32_t wordsize);
	void write_noc(void* handle, uint64_t offset, const uint32_t* buf, uint32_t wordsize);

	/**
	 * @brief Return the physical address associated with a virtual address.
	 *
	 * The virtual address needs to be the beginning of the mapped space.
	 *
	 * @param handle Struct with connection information.
	 * @param ddr_vaddr Virtual address of the mapped space in memory.
	 * @return physical address.
	 */
	uint64_t get_phy_addr(void* handle, void* ddr_vaddr);

	/**
	 * @brief Return the virtual address associated with a virtual address.
	 *
	 * The virtual address might be a pointer to an io specific struct
	 *
	 * @param handle Struct with connection information.
	 * @param ddr_vaddr pointer to a io specific struct.
	 * @return virtual address.
	 */
	void* get_vaddr(void* handle, void* ddr_vaddr);

	/**
	 * @brief Return base virtual address of a DDR.
	 *
	 * The virtual address needs to be the beginning of the mapped space.
	 *
	 * @param handle Struct with connection information.
	 * @param ddrIdx Index of the targeted DDR.
	 * @return Virtual address of the mapped space in memory.
	 */
	void* get_base_vaddr(void* handle, size_t ddrIdx);

	/**
	 * @brief Allocate a buffer in DDR.
	 *
	 * @param handle Struct with connection information.
	 * @param size Size of the buffer.
	 * @param ddr Select the DDR in which the buffer will be allocated.
	 * @param addr DDR address of the allocated buffer.
	 * @return int The Vart ML error (vart_ml_error_id) code of the operation.
	 */
	int ddr_allocate(void* handle, uint64_t size, uint32_t ddr, struct addr* addr);

	/**
	 * @brief Register a sub-buffer view of a parent DDR allocation.
	 *
	 * Does not allocate new memory. Registers parent_addr+offset into the
	 * backend's address maps.
	 *
	 * @param handle Struct with connection information.
	 * @param parent_addr DDR address of the parent buffer.
	 * @param offset Byte offset into the parent buffer.
	 * @param size Size of the sub-buffer in bytes.
	 * @param addr DDR address of the registered buffer.
	 * @return int The Vart ML error (vart_ml_error_id) code of the operation.
	 */
	int ddr_allocate_sub(void*        handle,
	                     struct addr* parent_addr,
	                     uint64_t     offset,
	                     uint64_t     size,
	                     struct addr* addr);

	/**
	 * @brief Allocates DDR memory.
	 *
	 * @param handle Struct with connection information.
	 * @param ddrIdx Index of the targeted DDR.
	 * @param size Requested allocation size.
	 * @return pointeur to the virtual mapped space.
	 */
	void* ddr_malloc(void* handle, size_t ddrIdx, size_t size);

	/**
	 * @brief Register a sub-buffer view of a parent DDR allocation.
	 *
	 * Does not allocate new memory. Registers parent_vaddr+offset into the
	 * backend's address maps so that get_phy_addr works on the sub-buffer.
	 *
	 * @param handle Struct with connection information.
	 * @param parent_vaddr Virtual address of the parent buffer.
	 * @param offset Byte offset into the parent buffer.
	 * @param size Size of the sub-buffer in bytes.
	 * @return Pointer to the sub-buffer virtual address, or NULL on failure.
	 */
	void* ddr_malloc_sub(void* handle, void* parent_vaddr, size_t offset, size_t size);

	/**
	 * @brief Free pre-allocated DDR memory.
	 *
	 * @param handle Struct with connection information.
	 * @param ddr_vaddr Pointer to the mapped virtual buffer.
	 * @return int The Vart ML error (vart_ml_error_id) code of the operation.
	 */
	int ddr_free(void* handle, void* ddr_vaddr);

	/**
	 * @brief Synchronize a buffer to the device.
	 *
	 * @param handle Struct with connection information.
	 * @param buffer Pointer to the buffer to synchronize.
	 */
	void ddr_sync_to_device(void* handle, void* buffer);

	/**
	 * @brief Synchronize a buffer from the device.
	 *
	 * @param handle Struct with connection information.
	 * @param buffer Pointer to the buffer to synchronize.
	 */
	void ddr_sync_from_device(void* handle, void* buffer);

	/**
	 * @brief Export a buffer as a dma-buf file descriptor.
	 *
	 * @param handle Struct with connection information.
	 * @param buffer Pointer to the buffer to export.
	 * @return File descriptor on success, or -1 on failure.
	 */
	int ddr_export_buffer(void* handle, void* buffer);

	/**
	 * @brief Wait for the end of inference (UIO) interrupt.
	 *
	 * @param handle Struct with connection information.
	 * @param ip_idx index of the IP being accessed.
	 * @return int The Vart ML error (vart_ml_error_id) code of the operation.
	 */
	int wait_interrupt(void* handle, uint32_t ip_idx);

	/**
	 * @brief Clean all remaining memory allocation.
	 *
	 * @param handle Struct with connection information.
	 * @return int The Vart ML error (vart_ml_error_id) code of the operation.
	 */
	int destroy_ip_context(void* handle);

	/**
	 * @brief Print the fpga_info file content associated to target IP.
	 *
	 * @param handle Struct with connection information.
	 * @param ip_idx index of the IP being accessed.
	 */
	void print_fpga_info(void* handle, uint32_t ip_idx);

	/**
	 * @brief Retrieve a numerical attribute in the fpga info of target IP.
	 *
	 * @param handle Struct with connection information.
	 * @param ip_idx index of the IP being accessed.
	 * @param attr   name of the attribute
	 * @return unsigned int The numerical value of the attribute.
	 */
	unsigned int get_ip_fpga_info_attribute(void* handle, uint32_t ip_idx, char* attr);

	/**
	 * @brief Get the FPGA name.
	 *
	 * @param handle Struct with connection information.
	 * @param ip_idx index of the IP being accessed.
	 * @param The FPGA name returned.
	 */
	char* get_boardname(void* handle, uint32_t ip_idx);

	/**
	 * @brief Return the size of the external memory with index extmemIdx.
	 *
	 * @param handle Struct with connection information.
	 * @param extmemIdx index of the external memory.
	 */
	size_t get_extmemlen(void* handle, size_t extmemIdx);

	/**
	 * @brief Return the number of free bytes in DDR bank ddrIdx.
	 *
	 * @param handle Struct with connection information.
	 * @param ddrIdx Index of the DDR bank.
	 */
	size_t get_ddr_free_bytes(void* handle, size_t ddrIdx);

	/**
	 * @brief Return the size of NOC address space.
	 *
	 * @param handle Struct with connection information.
	 */
	size_t get_noclen(void* handle);

	/**
	 * @brief Return the number of DDR in the FPGA.
	 *
	 * @param handle Struct with connection information.
	 */
	size_t get_nbddrs(void* handle);

	/**
	 * @brief Return the number of external memories in the FPGA.
	 *
	 * @param handle Struct with connection information.
	 */
	size_t get_nbextmems(void* handle);

	/**
	 * @brief Return the base address of a the DDR with index ddrIdx.
	 *
	 * @param handle Struct with connection information.
	 * @param extmemIdx index of the external memory.
	 */
	size_t get_extmemBaseAddr(void* handle, size_t extmemIdx);

	/**
	 * @brief Get the FPGA family.
	 *
	 * @param handle Struct with connection information.
	 * @param family Holds the returned fpga family enum: VERSAL or ZYNQ.
	 * @return int The Vart ML error (vart_ml_error_id) code of the operation.
	 */
	int get_fpgafamily(void* handle, enum FpgaFamily* family);

	/**
	 * @brief Get the FPGA architecture.
	 *
	 * @param handle Struct with connection information.
	 * @param arch Holds returned fpga architecture: V1 for Zynq family or the Versal AIE architecture.
	 * @return int The Vart ML error (vart_ml_error_id) code of the operation.
	 */
	int get_architecture(void* handle, enum FpgaArchitecture* arch);

	/**
	 * @brief Print all detected resources in a table format.
	 *
	 * This function prints information about DDR, NOC, and IP resources
	 * including their physical addresses and sizes.
	 *
	 * @param handle Struct with connection information.
	 */
	void print_resource_info(void* handle);

#ifdef __cplusplus
}
#endif

#endif
