/**
 * @file accessors.cpp
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

#include "npu_runner.h"

uint8_t npu_get_nb_ddrs(void) { return npu_get_nbddrs(); }

size_t npu_get_tmp_area_size(const npu_snapshot_t* snap) { return snap->tmp_area_addr.size(); }

uint64_t npu_get_tmp_area_nbuff_conf(const npu_snapshot_t* snap, size_t tmpareano)
{
	return snap->tmp_area_addr[tmpareano].nbuff_conf_val;
}

size_t npu_get_nbinputs(const npu_snapshot_t* snap) { return snap->inputs.size(); }

const std::string& npu_get_in_name(const npu_snapshot_t* snap, size_t inputno)
{
	return snap->inputs[inputno].name;
}

size_t npu_get_in_ddrimgsize(const npu_snapshot_t* snap, size_t inputno)
{
	return snap->inputs[inputno].ddrimgsize;
}

float npu_get_in_quantization_coeff(const npu_snapshot_t* snap, size_t inputno)
{
	return snap->inputs[inputno].coeff;
}

const std::string& npu_get_in_shape_format(const npu_snapshot_t* snap, size_t inputno)
{
	return formatToString(snap->inputs[inputno].format);
}

const std::string& npu_get_in_ddr_shape_format(const npu_snapshot_t* snap, size_t inputno)
{
	return formatToString(snap->inputs[inputno].ddr_format);
}

const std::string& npu_get_in_data_type(const npu_snapshot_t* snap, size_t inputno)
{
	return dataTypeToString(snap->inputs[inputno].data_type);
}

size_t npu_get_nboutputs(const npu_snapshot_t* snap) { return snap->outputs.size(); }

const std::string& npu_get_out_name(const npu_snapshot_t* snap, size_t outputno)
{
	return snap->outputs[outputno].name;
}

size_t npu_get_out_ddrimgsize(const npu_snapshot_t* snap, size_t outputno)
{
	return snap->outputs[outputno].ddrimgsize;
}

float npu_get_out_quantization_coeff(const npu_snapshot_t* snap, size_t outputno)
{
	return snap->outputs[outputno].coeff;
}

const std::string& npu_get_out_shape_format(const npu_snapshot_t* snap, size_t outputno)
{
	return formatToString(snap->outputs[outputno].format);
}

const std::string& npu_get_out_ddr_shape_format(const npu_snapshot_t* snap, size_t outputno)
{
	return formatToString(snap->outputs[outputno].ddr_format);
}

const std::string& npu_get_out_data_type(const npu_snapshot_t* snap, size_t outputno)
{
	return dataTypeToString(snap->outputs[outputno].data_type);
}
