/**
 * @file quantization.cpp
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

#include <cmath>
#include <cstdint>
#include <cstring>

#include "npu_runner.h"
#include "utils/log.h"

static int clip(int x, int min, int max)
{
	if (x >= max)
		return max;
	if (x <= min)
		return min;
	return x;
}

template <typename To, typename Ti>
To std_reinterpret_cast_and_deref(Ti i)
{
	static_assert(sizeof(To) == sizeof(Ti),
	              "Invalid dereferencing_type_punned_pointer_without_breaking_aliasing_rules call.");
	auto o = To();
	memcpy(&o, &i, sizeof(Ti));
	return o;
}

static void uint8_to_float(const uint8_t* src, float* dst, size_t size, float coeff)
{
	for (size_t i = 0; i < size; i++)
		dst[i] = src[i] / coeff;
}

static void int8_to_float(const int8_t* src, float* dst, size_t size, float coeff)
{
	for (size_t i = 0; i < size; i++)
		dst[i] = src[i] / coeff;
}

static void bf16_to_float(const uint16_t* src, float* dst, size_t size)
{
	for (size_t i = 0; i < size; i++)
	{
		std::uint32_t fi = std::uint32_t(src[i]) << 16;
		dst[i]           = std_reinterpret_cast_and_deref<float>(fi);
	}
}

/* Attention must be paid to rounding modes. The default SW implementation rounds the result of multiplying
   with the coefficient to the neareset EVEN integer.
*/
static void float_to_int8(const float* src, int8_t* dst, size_t size, float coeff)
{
	for (size_t i = 0; i < size; i++)
		dst[i] = clip(nearbyintf(src[i] * coeff), -128, 127);
}

static void float_to_bf16_accurate(const float* src, uint16_t* dst, size_t size)
{
	for (size_t i = 0; i < size; i++)
	{
		uint32_t if32 = std_reinterpret_cast_and_deref<uint32_t>(src[i]);
		uint32_t fs   = (if32 >> 31) & 0x1;
		uint32_t fe   = (if32 >> 23) & 0xFF;
		uint32_t fm   = if32 & 0x7FFFFF;
		uint16_t m, e, s;

		if (fe == 0)
		{ // subnormal
			m      = 0;
			e      = 0;
			s      = fs;
			dst[i] = ((s & 0x1) << 15) | ((e & 0xFF) << 7) | ((m & 0x7F) << 0);
		}
		else if (fe == 0xFF)
		{
			if (fm == 0)
			{ // inf
				m = 0;
				e = 0xFF;
				s = fs;
			}
			else
			{ // nan
				m = 1;
				e = 0xFF;
				s = 0;
			}
			dst[i] = ((s & 0x1) << 15) | ((e & 0xFF) << 7) | ((m & 0x7F) << 0);
		}
		else
		{
			uint32_t rbias = 0x7FFF + ((if32 >> 16) & 0x1);
			uint32_t iraw  = if32 + rbias;
			if (((iraw >> 23) & 0xFF) == 0xFF)
			{
				dst[i] = (fs << 15) | 0x7F80;
			}
			else
			{
				dst[i] = (iraw >> 16);
			}
		}
	}
}

static void float_to_bf16(const float* src, uint16_t* dst, size_t size)
{
	for (size_t i = 0; i < size; i++)
	{
		uint32_t if32 = std_reinterpret_cast_and_deref<uint32_t>(src[i]);
		dst[i]        = (if32 >> 16);
	}
}

static void uint8_to_int8(const uint8_t* src, int8_t* dst, size_t size)
{
	for (size_t i = 0; i < size; i++)
		dst[i] = clip(nearbyintf(src[i]), -128, 127);
}

int npu_dequantize(const void*        src,
                   void*              dst,
                   const std::string& data_type_in,
                   const std::string& data_type_out,
                   size_t             size,
                   float              coeff)
{
	if (src == NULL || dst == NULL)
		return vart_ml_log_err_msg(vart_ml_error::SYSTEM_ERROR_INVALID_ARG, "NULL pointer found.\n");

	if (stringToDataType(data_type_in) == BF16 && stringToDataType(data_type_out) == FLOAT32)
		bf16_to_float((const uint16_t*)src, (float*)dst, size);
	else if (stringToDataType(data_type_in) == INT8 && stringToDataType(data_type_out) == FLOAT32)
		int8_to_float((const int8_t*)src, (float*)dst, size, coeff);
	else if (stringToDataType(data_type_in) == UINT8 && stringToDataType(data_type_out) == FLOAT32)
		uint8_to_float((const uint8_t*)src, (float*)dst, size, coeff);
	else
		return vart_ml_log_err_msg(vart_ml_error::SYSTEM_ERROR_INVALID_ARG,
		                           "Unquantization from %s to %s not supported.\n",
		                           data_type_in.c_str(),
		                           data_type_out.c_str());

	return vart_ml_error::SUCCESS;
}

int npu_quantize(const void*        src,
                 void*              dst,
                 const std::string& data_type_in,
                 const std::string& data_type_out,
                 size_t             size,
                 float              coeff,
                 bool               use_accurate_bf16)
{
	if (src == NULL || dst == NULL)
		return vart_ml_log_err_msg(vart_ml_error::SYSTEM_ERROR_INVALID_ARG, "NULL pointer found.\n");

	if (stringToDataType(data_type_in) == FLOAT32 && stringToDataType(data_type_out) == INT8)
		float_to_int8((const float*)src, (int8_t*)dst, size, coeff);
	else if (stringToDataType(data_type_in) == FLOAT32 && stringToDataType(data_type_out) == BF16)
	{
		if (use_accurate_bf16)
			float_to_bf16_accurate((const float*)src, (uint16_t*)dst, size);
		else
			float_to_bf16((const float*)src, (uint16_t*)dst, size);
	}
	else if (stringToDataType(data_type_in) == UINT8 && stringToDataType(data_type_out) == INT8)
		uint8_to_int8((const uint8_t*)src, (int8_t*)dst, size);
	else
		return vart_ml_log_err_msg(vart_ml_error::SYSTEM_ERROR_INVALID_ARG,
		                           "Quantization from %s to %s not supported.\n",
		                           data_type_in.c_str(),
		                           data_type_out.c_str());

	return vart_ml_error::SUCCESS;
}
