/**
 * @file private.h
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

#ifndef PRIVATE_H
#define PRIVATE_H

#include <algorithm>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#include "io/io.h"
#include "utils/fpga_info.h"
#include "utils/npu_reg.h"

typedef enum
{
	NC,
	NHW,
	NHWC,
	NCHW,
	NHWC4,
	NHWC8,
	NC4HW4,
	NC8HW8,
	NHW16C4WC,
	UNKNOWN
} shape_format_t;

typedef enum
{
	INT8,
	FLOAT32,
	UINT8,
	BF16,
	NA
} data_type_t;

inline std::unordered_map<shape_format_t, std::string> shape_format_to_string_map = {
	{ UNKNOWN, "UNKNOWN" },    { NC, "NC" },         { NHW, "NHW" },
	{ NHWC, "NHWC" },          { NCHW, "NCHW" },     { NHWC4, "NHWC4" },
	{ NHWC8, "NHWC8" },        { NC4HW4, "NC4HW4" }, { NC8HW8, "NC8HW8" },
	{ NHW16C4WC, "NHW16C4WC" }
};

inline std::unordered_map<std::string, shape_format_t> string_to_shape_format_map = {
	{ "UNKNOWN", UNKNOWN }, { "NC", NC },           { "CUSTOM", NC },     { "FLAT", NC },
	{ "NHW", NHW },         { "NHWC", NHWC },       { "NCHW", NCHW },     { "NHWC4", NHWC4 },
	{ "NHWC8", NHWC8 },     { "NC4HW4", NC4HW4 },   { "NC8HW8", NC8HW8 }, { "NHW16C4WC", NHW16C4WC },
	{ "DDR_NHWC", NHWC8 },  { "DDR_NCHWC", NC8HW8 }
};

inline std::unordered_map<data_type_t, std::string> data_type_to_string_map = { { NA, "UNKNOWN" },
	                                                                            { INT8, "INT8" },
	                                                                            { UINT8, "UINT8" },
	                                                                            { BF16, "BF16" },
	                                                                            { FLOAT32, "FLOAT32" } };

inline std::unordered_map<std::string, data_type_t> string_to_data_type_map = {
	{ "UNKNOWN", NA }, { "INT8", INT8 },       { "UINT8", UINT8 },
	{ "BF16", BF16 },  { "FLOAT32", FLOAT32 }, { "FLOAT", FLOAT32 }
};

static inline const std::string& formatToString(shape_format_t format)
{
	if (shape_format_to_string_map.contains(format))
		return shape_format_to_string_map.at(format);

	return shape_format_to_string_map.at(UNKNOWN);
}

static inline shape_format_t stringToFormat(std::string str)
{
	// Character case is variable. Convert by default in uppercase.
	std::transform(str.begin(), str.end(), str.begin(), [](unsigned char c) { return std::toupper(c); });

	if (string_to_shape_format_map.contains(str))
		return string_to_shape_format_map.at(str);

	return UNKNOWN;
}

static inline const std::string& dataTypeToString(data_type_t data_type)
{
	if (data_type_to_string_map.contains(data_type))
		return data_type_to_string_map.at(data_type);

	return data_type_to_string_map.at(NA);
}

static inline data_type_t stringToDataType(std::string str)
{
	// Character case is variable. Convert by default in uppercase.
	std::transform(str.begin(), str.end(), str.begin(), [](unsigned char c) { return std::toupper(c); });

	// If no data is found we should return UNKNOWN but it is already taken by the shape format.
	// TODO: change enum to handle this case better
	if (string_to_data_type_map.contains(str))
		return string_to_data_type_map.at(str);

	return INT8;
}

static inline size_t sizeOfDataType(data_type_t data_type)
{
	if (data_type == INT8)
		return sizeof(int8_t);
	else if (data_type == UINT8)
		return sizeof(uint8_t);
	else if (data_type == FLOAT32)
		return sizeof(float);
	else if (data_type == BF16)
		return sizeof(uint16_t);
	else
		return 0;
}

static inline bool is_core_config_addr(uint64_t addr, FpgaArchitecture arch)
{
	if (arch == AIEML_V1C)
		return addr == CONFIG_OFFSET_VERSAL_V1C;

	if (arch == V1 && (MCTRL_MEM_BASE_USCALE <= addr && addr < MCTRL_MEM_BASE_USCALE + MCTRL_MEM_LEN_USCALE))
		return true;

	for (size_t s = 0; s < NB_MAX_SYSTEMS; s++)
		for (size_t c = 0; c < NB_MAX_CORES_PER_SYSTEM; c++)
		{
			if (addr == ENABLE_OFFSET(s, c) || addr == LOAD_OFFSET(s, c) || addr == CONFIG_OFFSET(s, c))
				return true;
			else if (arch == V2
			         && (MCTRL_VERSAL_OFFSET(s, c) <= addr
			             && addr < MCTRL_VERSAL_OFFSET(s, c) + VERSAL_MCTRL_MEM_LEN))
				return true;
		}

	return false;
}

static inline bool is_supervisor_config_addr(uint64_t addr, FpgaArchitecture arch)
{
	/* Supervisor configuration is not recorded for V1C */
	return (arch != AIEML_V1C) && (SUPERVISOR_REG_OFFSET <= addr) && (addr <= SUPERVISOR_REG_SYS_OFFSET);
}

/**
 * @brief Structure to pack supervisor reg.
 *
 */
typedef struct supervisor_reg_bf
{
	uint32_t sen : 1, use_nbuf : 1, _rfu : 30;

} supervisor_reg_bf;

typedef union supervisor_reg
{
	uint32_t          reg;
	supervisor_reg_bf bf;
} supervisor_reg;

/*! \brief structure to pack config */
typedef struct lm_coreBus_masterControl_config_aieml_v1c_bf
{
	uint8_t  _0x00[0];
	uint32_t supra_addr_ddr : 30, rfu0 : 2;
	uint8_t  _0x04[0];
	uint32_t supra_block_size : 14, rfu1 : 18;
	uint8_t  _0x08[0];
	uint32_t inactivity_timeout_ms : 16, rfu2 : 16;
	uint8_t  _0x0C[0];
	uint32_t offset_ddr0 : 32;
	uint8_t  _0x10[0];
	uint32_t offset_ddr1 : 32;
	uint8_t  _0x14[0];
	uint32_t offset_ddr2 : 32;
	uint8_t  _0x18[0];
	uint32_t offset_ddr3 : 32;
	uint8_t  _0x1C[0];
} lm_coreBus_masterControl_config_aieml_v1c_bf;

typedef union lm_coreBus_masterControl_config_aieml_v1c
{
	uint32_t                                     reg[7] = { 0 };
	lm_coreBus_masterControl_config_aieml_v1c_bf bf;

} lm_coreBus_masterControl_config_aieml_v1c;

/*! \brief structure to pack status */
typedef struct lm_coreBus_masterControl_status_uscale_bf
{
	uint32_t start                   : 1,  // RW
	    idle                         : 1,  // RO
	    _rfu0                        : 1,  //
	    mode_layer                   : 1,  // RW
	    _rfu1                        : 1,  //
	    subLayer_count               : 15, // RO
	    index_master_control_network : 12; // RO

} lm_coreBus_masterControl_status_uscale_bf;

/*! \brief union to access to the packed status */
typedef union lm_coreBus_masterControl_status_uscale
{
	uint32_t                                  reg;
	lm_coreBus_masterControl_status_uscale_bf bf;

} lm_coreBus_masterControl_status_uscale;

/*! \brief structure to pack status */
typedef struct lm_coreBus_masterControl_status_versal_bf
{
	uint8_t  _0x00[0];
	uint32_t system_en     : 1,  // RW
	    system_idle        : 1,  // RO
	    system_pause       : 1,  // RO
	    system_running     : 1,  // RO
	    sublayer_count_run : 24, // RO
	    supra_mem_idx_msb  : 4;  // RO
	uint8_t  _0x04[0];
	uint32_t rfu0          : 2,
	    sublayer_count_cfg : 24, // RO
	    supra_mem_idx_lsb  : 6;  // RO
	uint8_t _0x08[0];

} lm_coreBus_masterControl_status_versal_bf;

typedef union lm_coreBus_masterControl_status_versal
{
	uint64_t                                  reg;
	lm_coreBus_masterControl_status_versal_bf bf;

} lm_coreBus_masterControl_status_versal;

/*! \brief structure to pack status */
typedef struct lm_coreBus_masterControl_status_aieml_v1c_bf
{
	uint8_t  _0x00[0];
	uint32_t system_run : 1,  // RW
	    system_error    : 1,  // RO
	    rfu0            : 2,  // RO
	    supra_count     : 10, // RO
	    rfu1            : 18; // RO
	uint8_t _0x04[0];
} lm_coreBus_masterControl_status_aieml_v1c_bf;

typedef union lm_coreBus_masterControl_status_aieml_v1c
{
	uint32_t                                     reg;
	lm_coreBus_masterControl_status_aieml_v1c_bf bf;
} lm_coreBus_masterControl_status_aieml_v1c;

using lm_coreBus_masterControl_status = std::variant<lm_coreBus_masterControl_status_uscale,
                                                     lm_coreBus_masterControl_status_versal,
                                                     lm_coreBus_masterControl_status_aieml_v1c>;

/**
 * @brief Structure of the tensor.
 *
 *  This structure contains all the necessary parameter to access and upload/download data to DDR.
 *
 */
typedef struct npu_tensor
{
	int (*reorder)(const struct npu_tensor&, const void*, void*);
	std::string                                        name;
	size_t                                             ddrimgsize;
	size_t                                             size;
	size_t                                             nbdims;
	shape_format_t                                     format;
	shape_format_t                                     ddr_format;
	std::vector<uint32_t>                              shape;
	std::vector<uint32_t>                              strides;
	float                                              coeff;
	size_t                                             bigPixel;
	size_t                                             batchSize;
	size_t                                             batch_size_per_core;
	data_type_t                                        data_type;
	std::vector<std::vector<std::vector<struct addr>>> ddr_addrs; // [sys_idx][core_idx][batch_sample_idx]
	std::vector<std::vector<size_t>> nbuf_idx; // [sys_idx][batch_sample_idx in V1C, else core_idx]
} npu_tensor_t;

struct npu_constant_tensor : npu_tensor
{
	std::vector<std::string> path;
};

struct pl_tensor
{
	std::vector<uint32_t>      pl_shape;
	std::vector<uint32_t>      pl_strides;
	data_type_t                pl_data_type;
	std::map<size_t, uint64_t> pl_nbuf_addr;
	uint32_t                   pl_cfg_base_addr;
	std::vector<uint32_t>      pl_cfg_offset;
	size_t                     pl_buff_nbytes;
	std::vector<struct addr>   pl_ddr_addrs;
};

/**
 * @brief Structure of a snapshot
 *
 * This structure contains all the parameters needed to run a snapshot. They are retrieved by parsing the
 * snapshot files.
 *
 */
typedef struct npu_snapshot
{
	std::string      boardname;
	std::string      path;
	FpgaArchitecture arch;
	FpgaFamily       fpgaFamily;
	uint32_t         timestamp;
	uint32_t         ip_idx;

	/* Each one of these arrays has one element per core */
	uint32_t sublayer_count;
	uint32_t index_mctrl_network;
	uint32_t nbSupra;

	lm_coreBus_masterControl_config_aieml_v1c config;

	std::vector<struct addr> context_addr;
	std::vector<size_t>      context_size;
	std::vector<struct addr> config_addr;
	std::vector<size_t>      config_size;
	std::vector<size_t>      config_addr_in_snap;
	std::vector<struct addr> tmp_area_addr;
	std::vector<size_t>      tmp_area_size;
	std::vector<size_t>      tmp_area_addr_in_snap;
	std::vector<size_t>      constant_size;
	std::vector<size_t>      constant_addr_in_snap;

	uint32_t                                pl_timestamp;
	std::map<uint64_t, uint32_t>            pl_info;
	std::map<std::string, struct pl_tensor> pl;

	std::vector<struct npu_tensor>          inputs;
	std::vector<struct npu_tensor>          outputs;
	std::vector<struct npu_constant_tensor> constants;

	std::vector<std::pair<size_t, size_t>>                  active_cores;
	std::map<size_t, size_t>                                srv_write_sequence;
	std::vector<std::pair<uint64_t, std::vector<uint32_t>>> ctrl_reg_config;

	bool is_nbuff_en;
	bool checkConfig;
	bool debug_show_IO_address;
	bool skipTimestampCheck;
	bool interrupt_en;
} npu_snapshot_t;

/**
 * @brief Execute all the commands in a snapshot config file.
 *
 * @param snap Snapshot to run.
 * @return int The Vart ML error (vart_ml_error_id) code of the operation.
 */
int npu_run_snapshot(struct npu_snapshot* snap);

/**
 * @brief Execute all the commands in a snapshot config file in binary format.
 *
 * @param snap Snapshot to run.
 * @return int The Vart ML error (vart_ml_error_id) code of the operation.
 */
int npu_run_snapshot_bin(struct npu_snapshot* snap, std::ifstream& snapshot);

/**
 * @brief Execute all the commands in a snapshot config file in text format.
 *
 * @param snap Snapshot to run.
 * @return int The Vart ML error (vart_ml_error_id) code of the operation.
 */
int npu_run_snapshot_txt(struct npu_snapshot* snap, std::ifstream& snapshot);

#endif
