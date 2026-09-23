/**
 * @file arm_ops.cpp
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

#include "io/io.h"
#include "npu_runner/npu_runner.h"
#include "utils/stats.h"
#include "utils/vcd_stats.h"
#include "vart_ml_runner.hpp"

#define ALIGN         4 // All address needs to be 4 bytes aligned
#define IS_ALIGNED(x) ((x & (ALIGN - 1)) == 0)

namespace vart
{
using arm_op_buf = std::unique_ptr<void, void (*)(void*)>;

template <typename Type>
int pad(arm_ops& arm_op, const void* src, void* dst)
{
	std::vector<uint32_t> padding        = std::any_cast<std::vector<uint32_t>>(arm_op.parameter["padding"]);
	std::vector<uint32_t> input_shape    = arm_op.input_shape;
	std::vector<uint32_t> input_strides  = arm_op.input_strides;
	std::vector<uint32_t> output_strides = arm_op.output_strides;

	// Validate padding vector size.
	assert(padding.size() == (input_shape.size() - 1) * 2);

	size_t ndims = input_shape.size();

	// Optimize for common case: only one dimension padded after its data.
	int  padded_dim  = -1;
	bool simple_case = true;

	for (size_t d = 0; d < ndims - 1; d++)
	{
		size_t pad_before_d = padding[d * 2];
		size_t pad_after_d  = padding[d * 2 + 1];

		if (pad_before_d != 0)
		{
			simple_case = false;
			break;
		}

		if (pad_after_d != 0)
		{
			if (padded_dim != -1)
			{
				simple_case = false;
				break;
			}
			padded_dim = d + 1;
		}
	}

	// Fast path: single dimension padded after.
	if (simple_case && padded_dim != -1)
	{
		size_t outer_count = input_strides[0] / input_strides[padded_dim - 1];
		size_t block_size  = input_strides[padded_dim - 1] * sizeof(Type);
		size_t dst_stride  = output_strides[padded_dim - 1] * sizeof(Type);

		for (size_t i = 0; i < outer_count; i++)
			memcpy((uint8_t*)dst + i * dst_stride, (uint8_t*)src + i * block_size, block_size);

		return vart_ml_error::SUCCESS;
	}

	// General path: multi-dimensional padding.
	size_t num_copies = 1;
	for (size_t i = 0; i < ndims - 1; i++)
		num_copies *= input_shape[i];

	size_t copy_size = input_shape[ndims - 1] * sizeof(Type);

	std::vector<size_t> multi_index(ndims - 1, 0);

	for (size_t copy_idx = 0; copy_idx < num_copies; copy_idx++)
	{
		// Compute source and destination flat indices.
		size_t src_flat = multi_index[0] * input_strides[0];
		size_t dst_flat = multi_index[0] * output_strides[0];

		for (size_t d = 1; d < ndims - 1; d++)
		{
			src_flat += multi_index[d] * input_strides[d];
			dst_flat += (multi_index[d] + padding[(d - 1) * 2]) * output_strides[d];
		}

		// Add pad_before for the last dimension.
		dst_flat += padding[(ndims - 2) * 2];

		// Perform memcpy.
		memcpy((uint8_t*)dst + dst_flat * sizeof(Type), (uint8_t*)src + src_flat * sizeof(Type), copy_size);

		// Increment multi-dimensional index.
		for (int d = ndims - 2; d >= 0; d--)
		{
			multi_index[d]++;
			if (d == 0 || multi_index[d] < input_shape[d])
				break;
			multi_index[d] = 0;
		}
	}

	return vart_ml_error::SUCCESS;
}

static int dequantize(arm_ops& arm_op, const void* src, void* dst, float coeff)
{
	int err = npu_dequantize(src,
	                         dst,
	                         std::string(to_string(arm_op.input_data_type)),
	                         std::string(to_string(arm_op.output_data_type)),
	                         arm_op.input_size,
	                         coeff);
	if (err)
		return err;

	return vart_ml_error::SUCCESS;
}

static int quantize(arm_ops& arm_op, const void* src, void* dst, float coeff)
{
	int err = npu_quantize(src,
	                       dst,
	                       std::string(to_string(arm_op.input_data_type)),
	                       std::string(to_string(arm_op.output_data_type)),
	                       arm_op.input_size,
	                       coeff,
	                       std::any_cast<bool>(arm_op.parameter["use_accurate_bf16"]));
	if (err)
		return err;

	return vart_ml_error::SUCCESS;
}

template <typename Type>
static int slice(arm_ops& arm_op, const void* src_void, void* dst_void)
{
	// Cast pointers to corresponding data.
	const Type* src = static_cast<const Type*>(src_void);
	Type*       dst = static_cast<Type*>(dst_void);

	uint32_t start   = std::any_cast<uint32_t>(arm_op.parameter["starts"]);
	uint32_t axes    = std::any_cast<uint32_t>(arm_op.parameter["axes"]);
	uint32_t strides = std::any_cast<uint32_t>(arm_op.parameter["strides"]);

	std::vector<uint32_t> src_strides = arm_op.input_strides;
	std::vector<uint32_t> dst_strides = arm_op.output_strides;

	const uint32_t ndims = dst_strides.size();

	// Fast path: contiguous slice along last dimension with stride=1.
	if (axes == ndims - 1 && strides == 1)
	{
		size_t block_size    = dst_strides[ndims - 2];
		size_t num_blocks    = dst_strides[0] / block_size;
		size_t bytes_to_copy = block_size * sizeof(Type);

		for (size_t i = 0; i < num_blocks; i++)
		{
			size_t src_block_start = i * src_strides[ndims - 2] + start;
			size_t dst_block_start = i * block_size;
			memcpy(dst + dst_block_start, src + src_block_start, bytes_to_copy);
		}

		return vart_ml_error::SUCCESS;
	}

	// Iterate over dst flat indices with incremental multi-index update.
	std::vector<uint32_t> dst_multi_index(ndims, 0);

	for (size_t dst_flat = 0; dst_flat < dst_strides[0]; dst_flat++)
	{
		// Convert dst multi-index to src flat index directly.
		size_t src_flat = 0;
		for (uint32_t d = 0; d < ndims; d++)
		{
			uint32_t src_idx = (d == axes) ? (dst_multi_index[d] * strides) + start : dst_multi_index[d];
			src_flat += src_idx * src_strides[d];
		}

		dst[dst_flat] = src[src_flat];

		// Increment dst multi-index.
		for (int d = ndims - 1; d >= 0; d--)
		{
			dst_multi_index[d]++;
			if (d == 0 || dst_multi_index[d] < dst_strides[d - 1] / dst_strides[d])
				break;
			dst_multi_index[d] = 0;
		}
	}

	return vart_ml_error::SUCCESS;
}

#ifdef __ARM_NEON
#include <arm_neon.h>

// ---------------------------------------------------------------------------
// NEON tile kernels
//
// Each function transposes one fixed-size block from src (row stride src_row)
// into dst (row stride dst_row).  Strides are in elements, not bytes.
// The 3-step interleave pattern (byte → 16-bit → 32-bit) is the standard
// technique for in-register matrix transposition on ARM NEON.
// ---------------------------------------------------------------------------

// 8×8 uint8_t tile.
static void transpose_tile_8x8_u8(const uint8_t* __restrict__ src,
                                  uint8_t* __restrict__ dst,
                                  size_t src_row,
                                  size_t dst_row)
{
	uint8x8_t r0 = vld1_u8(src + 0 * src_row), r1 = vld1_u8(src + 1 * src_row);
	uint8x8_t r2 = vld1_u8(src + 2 * src_row), r3 = vld1_u8(src + 3 * src_row);
	uint8x8_t r4 = vld1_u8(src + 4 * src_row), r5 = vld1_u8(src + 5 * src_row);
	uint8x8_t r6 = vld1_u8(src + 6 * src_row), r7 = vld1_u8(src + 7 * src_row);

	// Step 1: byte-level interleave — pairs of adjacent rows.
	uint8x8x2_t q0 = vtrn_u8(r0, r1), q1 = vtrn_u8(r2, r3);
	uint8x8x2_t q2 = vtrn_u8(r4, r5), q3 = vtrn_u8(r6, r7);

	// Step 2: 16-bit-level interleave — merge pairs of row-pairs.
	uint16x4x2_t p0 = vtrn_u16(vreinterpret_u16_u8(q0.val[0]), vreinterpret_u16_u8(q1.val[0]));
	uint16x4x2_t p1 = vtrn_u16(vreinterpret_u16_u8(q0.val[1]), vreinterpret_u16_u8(q1.val[1]));
	uint16x4x2_t p2 = vtrn_u16(vreinterpret_u16_u8(q2.val[0]), vreinterpret_u16_u8(q3.val[0]));
	uint16x4x2_t p3 = vtrn_u16(vreinterpret_u16_u8(q2.val[1]), vreinterpret_u16_u8(q3.val[1]));

	// Step 3: 32-bit-level interleave — merge the two halves.
	uint32x2x2_t s0 = vtrn_u32(vreinterpret_u32_u16(p0.val[0]), vreinterpret_u32_u16(p2.val[0]));
	uint32x2x2_t s1 = vtrn_u32(vreinterpret_u32_u16(p0.val[1]), vreinterpret_u32_u16(p2.val[1]));
	uint32x2x2_t s2 = vtrn_u32(vreinterpret_u32_u16(p1.val[0]), vreinterpret_u32_u16(p3.val[0]));
	uint32x2x2_t s3 = vtrn_u32(vreinterpret_u32_u16(p1.val[1]), vreinterpret_u32_u16(p3.val[1]));

	// Output columns 0–7, now stored as rows.
	vst1_u8(dst + 0 * dst_row, vreinterpret_u8_u32(s0.val[0]));
	vst1_u8(dst + 1 * dst_row, vreinterpret_u8_u32(s2.val[0]));
	vst1_u8(dst + 2 * dst_row, vreinterpret_u8_u32(s1.val[0]));
	vst1_u8(dst + 3 * dst_row, vreinterpret_u8_u32(s3.val[0]));
	vst1_u8(dst + 4 * dst_row, vreinterpret_u8_u32(s0.val[1]));
	vst1_u8(dst + 5 * dst_row, vreinterpret_u8_u32(s2.val[1]));
	vst1_u8(dst + 6 * dst_row, vreinterpret_u8_u32(s1.val[1]));
	vst1_u8(dst + 7 * dst_row, vreinterpret_u8_u32(s3.val[1]));
}

// 4×4 uint16_t tile.
static void transpose_tile_4x4_u16(const uint16_t* __restrict__ src,
                                   uint16_t* __restrict__ dst,
                                   size_t src_row,
                                   size_t dst_row)
{
	uint16x4_t r0 = vld1_u16(src + 0 * src_row), r1 = vld1_u16(src + 1 * src_row);
	uint16x4_t r2 = vld1_u16(src + 2 * src_row), r3 = vld1_u16(src + 3 * src_row);

	// Step 1: 16-bit-level interleave.
	uint16x4x2_t t01 = vtrn_u16(r0, r1);
	uint16x4x2_t t23 = vtrn_u16(r2, r3);

	// Step 2: 32-bit-level interleave.
	uint32x2x2_t s0 = vtrn_u32(vreinterpret_u32_u16(t01.val[0]), vreinterpret_u32_u16(t23.val[0]));
	uint32x2x2_t s1 = vtrn_u32(vreinterpret_u32_u16(t01.val[1]), vreinterpret_u32_u16(t23.val[1]));

	// Output columns 0–3.
	vst1_u16(dst + 0 * dst_row, vreinterpret_u16_u32(s0.val[0]));
	vst1_u16(dst + 1 * dst_row, vreinterpret_u16_u32(s1.val[0]));
	vst1_u16(dst + 2 * dst_row, vreinterpret_u16_u32(s0.val[1]));
	vst1_u16(dst + 3 * dst_row, vreinterpret_u16_u32(s1.val[1]));
}

// 4×4 uint32_t tile.
static void transpose_tile_4x4_u32(const uint32_t* __restrict__ src,
                                   uint32_t* __restrict__ dst,
                                   size_t src_row,
                                   size_t dst_row)
{
	uint32x4_t r0 = vld1q_u32(src + 0 * src_row), r1 = vld1q_u32(src + 1 * src_row);
	uint32x4_t r2 = vld1q_u32(src + 2 * src_row), r3 = vld1q_u32(src + 3 * src_row);

	// Step 1: 32-bit-level interleave across 128-bit registers.
	uint32x4x2_t t01 = vtrnq_u32(r0, r1);
	uint32x4x2_t t23 = vtrnq_u32(r2, r3);

	// Step 2: 64-bit zip to assemble the final rows.
	vst1q_u32(dst + 0 * dst_row,
	          vreinterpretq_u32_u64(
	              vzip1q_u64(vreinterpretq_u64_u32(t01.val[0]), vreinterpretq_u64_u32(t23.val[0]))));
	vst1q_u32(dst + 1 * dst_row,
	          vreinterpretq_u32_u64(
	              vzip1q_u64(vreinterpretq_u64_u32(t01.val[1]), vreinterpretq_u64_u32(t23.val[1]))));
	vst1q_u32(dst + 2 * dst_row,
	          vreinterpretq_u32_u64(
	              vzip2q_u64(vreinterpretq_u64_u32(t01.val[0]), vreinterpretq_u64_u32(t23.val[0]))));
	vst1q_u32(dst + 3 * dst_row,
	          vreinterpretq_u32_u64(
	              vzip2q_u64(vreinterpretq_u64_u32(t01.val[1]), vreinterpretq_u64_u32(t23.val[1]))));
}

// 2×2 uint64_t tile.
static void transpose_tile_2x2_u64(const uint64_t* __restrict__ src,
                                   uint64_t* __restrict__ dst,
                                   size_t src_row,
                                   size_t dst_row)
{
	uint64x2_t r0 = vld1q_u64(src + 0 * src_row);
	uint64x2_t r1 = vld1q_u64(src + 1 * src_row);
	vst1q_u64(dst + 0 * dst_row, vzip1q_u64(r0, r1));
	vst1q_u64(dst + 1 * dst_row, vzip2q_u64(r0, r1));
}

// ---------------------------------------------------------------------------
// Tiled 2D transpose: src[M][N] -> dst[N][M].
//
// Tile size is chosen per element width to fill NEON registers without
// spilling.  Rows and columns that do not fill a complete tile are handled
// by a scalar fallback at the end.
// ---------------------------------------------------------------------------
template <typename Type>
static void transpose_2d_neon(const Type* __restrict__ src, Type* __restrict__ dst, size_t M, size_t N)
{
	constexpr size_t TILE = (sizeof(Type) == 1) ? 8u : (sizeof(Type) <= 4) ? 4u : 2u;

	size_t mi = 0;
	for (; mi + TILE <= M; mi += TILE)
	{
		size_t ni = 0;
		for (; ni + TILE <= N; ni += TILE)
		{
			if constexpr (sizeof(Type) == 1)
				transpose_tile_8x8_u8(reinterpret_cast<const uint8_t*>(src + mi * N + ni),
				                      reinterpret_cast<uint8_t*>(dst + ni * M + mi),
				                      N,
				                      M);
			else if constexpr (sizeof(Type) == 2)
				transpose_tile_4x4_u16(reinterpret_cast<const uint16_t*>(src + mi * N + ni),
				                       reinterpret_cast<uint16_t*>(dst + ni * M + mi),
				                       N,
				                       M);
			else if constexpr (sizeof(Type) == 4)
				transpose_tile_4x4_u32(reinterpret_cast<const uint32_t*>(src + mi * N + ni),
				                       reinterpret_cast<uint32_t*>(dst + ni * M + mi),
				                       N,
				                       M);
			else
				transpose_tile_2x2_u64(reinterpret_cast<const uint64_t*>(src + mi * N + ni),
				                       reinterpret_cast<uint64_t*>(dst + ni * M + mi),
				                       N,
				                       M);
		}
		// Scalar remainder: columns [ni, N) within the current row-tile.
		for (size_t mj = mi; mj < mi + TILE; mj++)
			for (size_t nj = ni; nj < N; nj++)
				dst[nj * M + mj] = src[mj * N + nj];
	}
	// Scalar remainder: rows [mi, M) for all columns.
	for (; mi < M; mi++)
		for (size_t ni = 0; ni < N; ni++)
			dst[ni * M + mi] = src[mi * N + ni];
}

// ---------------------------------------------------------------------------
// NCHW → NHWC with optional channel zero-padding.
//
// Handles src[1][C][H][W] → dst[1][H][W][Cout] where Cout >= C.
// The primary specialisation targets float32 (sizeof==4) with C=3, Cout=8:
// a common ML preprocessing layout where 3 RGB channels are written into an
// 8-channel DDR buffer required by the NPU.
//
// For 4 consecutive pixels, NEON assembles the interleaved output entirely
// in registers using zip+combine — no per-element scatter writes:
//
//   loads:  v0=[c00,c01,c02,c03]  v1=[c10,c11,c12,c13]  v2=[c20,c21,c22,c23]
//   lo01  = vzip1(v0,v1) = [c00,c10,c01,c11]
//   lo2z  = vzip1(v2, 0) = [c20, 0, c21, 0 ]
//   p0    = combine(low(lo01), low(lo2z))  = [c00,c10,c20,0]  ← pixel 0, ch 0-3
//   p1    = combine(high(lo01),high(lo2z)) = [c01,c11,c21,0]  ← pixel 1, ch 0-3
//   (hi01/hi2z give p2 and p3 the same way)
//   trailing ch 4-7 per pixel = zero register, stored separately.
// ---------------------------------------------------------------------------
template <typename Type>
static void nchw_to_nhwc_neon(const Type* __restrict__ src,
                              Type* __restrict__ dst,
                              size_t C,
                              size_t H,
                              size_t W,
                              size_t Cout)
{
	const size_t plane = H * W;

	if constexpr (sizeof(Type) == 4)
	{
		if (C == 3 && Cout == 8)
		{
			const float*      ch0  = reinterpret_cast<const float*>(src);
			const float*      ch1  = ch0 + plane;
			const float*      ch2  = ch1 + plane;
			float*            out  = reinterpret_cast<float*>(dst);
			const float32x4_t zero = vdupq_n_f32(0.0f);

			size_t hw = 0;
			for (; hw + 4 <= plane; hw += 4)
			{
				float32x4_t v0 = vld1q_f32(ch0 + hw);
				float32x4_t v1 = vld1q_f32(ch1 + hw);
				float32x4_t v2 = vld1q_f32(ch2 + hw);

				// Interleave ch0/ch1 and ch2/zero across the 4 pixels.
				float32x4_t lo01 = vzip1q_f32(v0, v1);   // [p0_c0,p0_c1, p1_c0,p1_c1]
				float32x4_t hi01 = vzip2q_f32(v0, v1);   // [p2_c0,p2_c1, p3_c0,p3_c1]
				float32x4_t lo2z = vzip1q_f32(v2, zero); // [p0_c2, 0,    p1_c2, 0   ]
				float32x4_t hi2z = vzip2q_f32(v2, zero); // [p2_c2, 0,    p3_c2, 0   ]

				// Combine halves: each result is [c0, c1, c2, 0] for one pixel.
				float32x4_t p0 = vcombine_f32(vget_low_f32(lo01), vget_low_f32(lo2z));
				float32x4_t p1 = vcombine_f32(vget_high_f32(lo01), vget_high_f32(lo2z));
				float32x4_t p2 = vcombine_f32(vget_low_f32(hi01), vget_low_f32(hi2z));
				float32x4_t p3 = vcombine_f32(vget_high_f32(hi01), vget_high_f32(hi2z));

				// Store 8 channels per pixel: [c0,c1,c2,0] then [0,0,0,0].
				float* o = out + hw * 8;
				vst1q_f32(o + 0, p0);
				vst1q_f32(o + 4, zero);
				vst1q_f32(o + 8, p1);
				vst1q_f32(o + 12, zero);
				vst1q_f32(o + 16, p2);
				vst1q_f32(o + 20, zero);
				vst1q_f32(o + 24, p3);
				vst1q_f32(o + 28, zero);
			}
			// Scalar remainder for H*W not divisible by 4.
			for (; hw < plane; hw++)
			{
				float* o = out + hw * 8;
				o[0]     = ch0[hw];
				o[1]     = ch1[hw];
				o[2]     = ch2[hw];
				o[3] = o[4] = o[5] = o[6] = o[7] = 0.0f;
			}
			return;
		}
	}

	// General scalar fallback for other type/channel combinations.
	for (size_t hw = 0; hw < plane; hw++)
	{
		for (size_t c = 0; c < C; c++)
			dst[hw * Cout + c] = src[c * plane + hw];
		for (size_t c = C; c < Cout; c++)
			dst[hw * Cout + c] = Type{};
	}
}
#endif // __ARM_NEON

template <typename Type>
static int transpose(arm_ops& arm_op, const void* src_void, void* dst_void)
{
	// Cast pointers to corresponding data.
	const Type* src = static_cast<const Type*>(src_void);
	Type*       dst = static_cast<Type*>(dst_void);

	std::vector<int>      perm         = std::any_cast<std::vector<int>>(arm_op.parameter["perm"]);
	std::vector<uint32_t> input_shape  = arm_op.input_shape;
	std::vector<uint32_t> output_shape = arm_op.output_shape;
	std::vector<uint32_t> src_strides  = arm_op.input_strides;
	std::vector<uint32_t> dst_strides  = arm_op.output_strides;

	const size_t ndims = input_shape.size();
	assert(ndims == perm.size());
	assert(ndims == output_shape.size());

	if (check_user_config("disable.optim"))
	{
		// Unoptimized reference path: per-element flat-index rebuild via division/modulo.
		if (src_strides[0] > dst_strides[0])
		{
			// Slicing case: iterate only over output elements (smaller set).
			std::vector<uint32_t> dst_multi_index(ndims);

			for (size_t dst_flat = 0; dst_flat < dst_strides[0]; dst_flat++)
			{
				// Convert dst flat index to dst multi-index and compute src flat index.
				size_t tmp      = dst_flat;
				size_t src_flat = 0;
				for (size_t d = 0; d < ndims; ++d)
				{
					dst_multi_index[d] = tmp / dst_strides[d];
					tmp %= dst_strides[d];
					// Apply inverse permutation: dst[d] corresponds to src[perm[d]].
					src_flat += dst_multi_index[d] * src_strides[perm[d]];
				}

				dst[dst_flat] = src[src_flat];
			}
		}
		else
		{
			// Padding or simple transpose: iterate over input elements.
			std::vector<uint32_t> src_multi_index(ndims);

			for (size_t src_flat = 0; src_flat < src_strides[0]; src_flat++)
			{
				// Convert src flat index to src multi-index.
				size_t tmp = src_flat;
				for (size_t d = 0; d < ndims; ++d)
				{
					src_multi_index[d] = tmp / src_strides[d];
					tmp %= src_strides[d];
				}

				// Apply permutation and compute dst flat index.
				size_t dst_flat = 0;
				for (size_t d = 0; d < ndims; ++d)
					dst_flat += src_multi_index[perm[d]] * dst_strides[d];

				dst[dst_flat] = src[src_flat];
			}
		}

		return vart_ml_error::SUCCESS;
	}

	// Maximum supported number of dimensions for stack-allocated index arrays.
	constexpr size_t MAX_NDIMS = 8;
	assert(ndims <= MAX_NDIMS);

	// NEON fast paths.
#ifdef __ARM_NEON
	// NCHW → NHWC with optional channel zero-padding.
	// Fires for perm={0,2,3,1} on a 4D tensor where Cout >= C.
	// The specialised kernel handles the C=3→8 float32 case with NEON zip+combine;
	// other type/channel combinations fall back to the scalar path inside it.
	if (ndims == 4 && perm[0] == 0 && perm[1] == 2 && perm[2] == 3 && perm[3] == 1
	    && output_shape[3] >= input_shape[1])
	{
		nchw_to_nhwc_neon(src,
		                  dst,
		                  input_shape[1],   // C
		                  input_shape[2],   // H
		                  input_shape[3],   // W
		                  output_shape[3]); // Cout
		return vart_ml_error::SUCCESS;
	}

	// Pure innermost-2D swap with identity outer dimensions.
	// Fires when perm = {0, 1, ..., ndims-3, ndims-1, ndims-2} and no slicing
	// or padding is present (src and dst have the same total element count).
	// The entire operation reduces to outer_count independent [M x N] -> [N x M]
	// matrix transposes, each handled by the tiled NEON kernel.
	if (src_strides[0] == dst_strides[0] && ndims >= 2 && perm[ndims - 2] == (int)(ndims - 1)
	    && perm[ndims - 1] == (int)(ndims - 2))
	{
		bool outer_identity = true;
		for (size_t d = 0; d < ndims - 2; ++d)
			if (perm[d] != (int)d)
			{
				outer_identity = false;
				break;
			}

		if (outer_identity)
		{
			const size_t M           = input_shape[ndims - 2];
			const size_t N           = input_shape[ndims - 1];
			const size_t outer_count = (M * N > 0) ? src_strides[0] / (M * N) : 0;
			for (size_t outer = 0; outer < outer_count; ++outer)
				transpose_2d_neon(src + outer * M * N, dst + outer * M * N, M, N);
			return vart_ml_error::SUCCESS;
		}
	}
#endif

	// Determine case based on stride comparison:
	// Slicing: source has more elements (src_strides[0] > dst_strides[0]) - iterate over dst.
	// Padding or simple transpose: iterate over src (src_strides[0] <= dst_strides[0]).

	if (src_strides[0] > dst_strides[0])
	{
		// Slicing case: iterate only over output elements (smaller set).
		//
		// Precompute permuted src strides: permuted_src_strides[d] = src_strides[perm[d]].
		// When dst_multi_index[d] increments by 1, src_flat advances by permuted_src_strides[d].
		// Precompute dst extents: dst_extent[d] = output size along dimension d.
		// Both arrays are used in the carry loop to update src_flat without any division.
		size_t   permuted_src_strides[MAX_NDIMS];
		uint32_t dst_extent[MAX_NDIMS];
		for (size_t d = 0; d < ndims; ++d)
			permuted_src_strides[d] = src_strides[perm[d]];
		for (size_t d = 1; d < ndims; ++d)
			dst_extent[d] = dst_strides[d - 1] / dst_strides[d];

		uint32_t dst_multi_index[MAX_NDIMS] = {};
		size_t   src_flat                   = 0;

		for (size_t dst_flat = 0; dst_flat < dst_strides[0]; dst_flat++)
		{
			dst[dst_flat] = src[src_flat];

			// Increment the innermost dimension and propagate carries outward.
			// src_flat is updated by addition/subtraction only - no division.
			for (int d = ndims - 1; d >= 1; d--)
			{
				dst_multi_index[d]++;
				src_flat += permuted_src_strides[d];
				if (dst_multi_index[d] < dst_extent[d])
					break;
				// This dimension overflowed: reset it and carry into d-1.
				src_flat -= dst_extent[d] * permuted_src_strides[d];
				dst_multi_index[d] = 0;
			}
		}
	}
	else
	{
		// Padding or simple transpose: iterate over input elements.
		//
		// Precompute dst_stride_for_src[d]: the amount dst_flat changes when src dimension d
		// increments by 1. Derived by inverting the permutation:
		//   dst_flat = sum_d(src_multi_index[d] * dst_stride_for_src[d])
		//   where dst_stride_for_src[perm[d]] = dst_strides[d].
		// Precompute src extents: src_extent[d] = input size along dimension d.
		size_t   dst_stride_for_src[MAX_NDIMS] = {};
		uint32_t src_extent[MAX_NDIMS];
		for (size_t d = 0; d < ndims; ++d)
			dst_stride_for_src[perm[d]] = dst_strides[d];
		for (size_t d = 1; d < ndims; ++d)
			src_extent[d] = src_strides[d - 1] / src_strides[d];

		uint32_t src_multi_index[MAX_NDIMS] = {};
		size_t   dst_flat                   = 0;

		for (size_t src_flat = 0; src_flat < src_strides[0]; src_flat++)
		{
			dst[dst_flat] = src[src_flat];

			// Increment the innermost dimension and propagate carries outward.
			// dst_flat is updated by addition/subtraction only - no division.
			for (int d = ndims - 1; d >= 1; d--)
			{
				src_multi_index[d]++;
				dst_flat += dst_stride_for_src[d];
				if (src_multi_index[d] < src_extent[d])
					break;
				// This dimension overflowed: reset it and carry into d-1.
				dst_flat -= src_extent[d] * dst_stride_for_src[d];
				src_multi_index[d] = 0;
			}
		}
	}

	return vart_ml_error::SUCCESS;
}

static void
check_arm_ops(std::vector<arm_ops>& arm_ops_ref, const NpuTensor& npu_tensor, std::vector<arm_ops>& arm_ops)
{
	// Get index dimension of the input's channel to check if channel padding is needed.
	std::vector<uint32_t> strides = npu_tensor.get_info().strides;

	for (auto& arm_op : arm_ops_ref)
		switch (arm_op.type)
		{
		case ArmOps::CAST:
			// Skip arm op if the data type are of the same size or if tensor does not have cast's base type
			// (input dtype for input tensors & output dtype for output tensors).
			if (get_data_type_size(arm_op.input_data_type) != get_data_type_size(arm_op.output_data_type))
			{
				if (((npu_tensor.get_info().direction == TensorDirection::INPUT)
				     && (npu_tensor.get_info().data_type == arm_op.input_data_type))
				    || ((npu_tensor.get_info().direction == TensorDirection::OUTPUT)
				        && (npu_tensor.get_info().data_type == arm_op.output_data_type)))
					arm_ops.push_back(arm_op);
			}
			break;

		case ArmOps::PAD:
			// Check input's dims against the padding result's dims.
			for (size_t d = 1; d < strides.size(); d++)
				if ((strides[d - 1] / strides[d]) != arm_op.output_shape[d])
				{
					arm_ops.push_back(arm_op);
					break;
				}
			break;

		case ArmOps::DEQUANTIZE:
			if (arm_op.input_data_type != npu_tensor.get_info().data_type)
				arm_ops.push_back(arm_op);
			break;

		case ArmOps::QUANTIZE:
			if (arm_op.output_data_type != npu_tensor.get_info().data_type)
				arm_ops.push_back(arm_op);
			break;

		case ArmOps::RESHAPE:
			break;

		case ArmOps::SLICE:
			// If the resulting buffer is non contiguous, there is no need to slice it.
			if (npu_tensor.get_memory_type() != MemoryType::USER_POINTER_NON_CMA)
				arm_ops.push_back(arm_op);
			break;

		case ArmOps::TRANSPOSE:
			// Always push TRANSPOSE arm op as filtering has already been done during parsing.
			arm_ops.push_back(arm_op);
			break;

		default:
			break;
		}
}

int VartMLRunner::execute_arm_ops(const struct node_desc& node,
                                  size_t                  tid,
                                  arm_ops&                arm_op,
                                  const void*             src,
                                  void*                   dst)
{
	// Arm ops execute per-batch-item; normalize batch dimension.
	arm_op.input_shape[0]  = 1;
	arm_op.output_shape[0] = 1;

	int                err;
	DataType           data_type;
	struct vcd_context vcd_context = { vcd_id_, tid };

	switch (arm_op.type)
	{
	case ArmOps::CAST:
		vcd_event(vcd_context, NPU_ARM_OP_CAST, 1);

		// If CAST reduces data type size it is equivalent to a quantization with coeff == 1. Otherwise it is
		// equivalent to an unquantization.
		if (get_data_type_size(arm_op.input_data_type) > get_data_type_size(arm_op.output_data_type))
		{
			err = stats_->start_step(node.hash, EmbeddedStats::QUANTIZE);
			if (err)
				return err;

			quantize(arm_op, src, dst, 1);

			err = stats_->update_step(node.hash, EmbeddedStats::QUANTIZE);
			if (err)
				return err;
		}
		else
		{
			err = stats_->start_step(node.hash, EmbeddedStats::DEQUANTIZE);
			if (err)
				return err;

			dequantize(arm_op, src, dst, 1);

			err = stats_->update_step(node.hash, EmbeddedStats::DEQUANTIZE);
			if (err)
				return err;
		}

		vcd_event(vcd_context, NPU_ARM_OP_CAST, 0);
		break;

	case ArmOps::PAD:
		vcd_event(vcd_context, NPU_ARM_OP_PAD, 1);

		data_type = arm_op.input_data_type;
		if (get_data_type_size(data_type) == 1)
			pad<uint8_t>(arm_op, src, dst);
		else if (get_data_type_size(data_type) == 2)
			pad<uint16_t>(arm_op, src, dst);
		else if (get_data_type_size(data_type) == 4)
			pad<uint32_t>(arm_op, src, dst);
		else if (get_data_type_size(data_type) == 8)
			pad<uint64_t>(arm_op, src, dst);
		else
			return vart_ml_log_err_msg(vart_ml_error::CONFIG_IRIZ_MALFORMED,
			                           "ARM Ops pad: not implemented for data type %s.\n",
			                           to_string(data_type).data());

		vcd_event(vcd_context, NPU_ARM_OP_PAD, 0);
		break;

	case ArmOps::DEQUANTIZE:
		vcd_event(vcd_context, NPU_ARM_OP_DEQUANTIZE, 1);

		err = stats_->start_step(node.hash, EmbeddedStats::DEQUANTIZE);
		if (err)
			return err;

		dequantize(arm_op, src, dst, std::any_cast<float>(arm_op.parameter["scale"]));

		err = stats_->update_step(node.hash, EmbeddedStats::DEQUANTIZE);
		if (err)
			return err;

		vcd_event(vcd_context, NPU_ARM_OP_DEQUANTIZE, 0);
		break;

	case ArmOps::QUANTIZE:
		vcd_event(vcd_context, NPU_ARM_OP_QUANTIZE, 1);

		err = stats_->start_step(node.hash, EmbeddedStats::QUANTIZE);
		if (err)
			return err;

		quantize(arm_op, src, dst, std::any_cast<float>(arm_op.parameter["scale"]));

		err = stats_->update_step(node.hash, EmbeddedStats::QUANTIZE);
		if (err)
			return err;

		vcd_event(vcd_context, NPU_ARM_OP_QUANTIZE, 0);
		break;

	case ArmOps::SLICE:
		vcd_event(vcd_context, NPU_ARM_OP_SLICE, 1);

		data_type = arm_op.output_data_type;
		if (get_data_type_size(data_type) == 1)
			slice<uint8_t>(arm_op, src, dst);
		else if (get_data_type_size(data_type) == 2)
			slice<uint16_t>(arm_op, src, dst);
		else if (get_data_type_size(data_type) == 4)
			slice<uint32_t>(arm_op, src, dst);
		else if (get_data_type_size(data_type) == 8)
			slice<uint64_t>(arm_op, src, dst);
		else
			return vart_ml_log_err_msg(vart_ml_error::CONFIG_IRIZ_MALFORMED,
			                           "ARM Ops Slice is not implemented for data type %s.\n",
			                           to_string(data_type).data());

		vcd_event(vcd_context, NPU_ARM_OP_SLICE, 0);
		break;

	case ArmOps::TRANSPOSE:
		vcd_event(vcd_context, NPU_ARM_OP_TRANSPOSE, 1);

		data_type = arm_op.input_data_type;
		if (get_data_type_size(data_type) == 1)
			transpose<uint8_t>(arm_op, src, dst);
		else if (get_data_type_size(data_type) == 2)
			transpose<uint16_t>(arm_op, src, dst);
		else if (get_data_type_size(data_type) == 4)
			transpose<uint32_t>(arm_op, src, dst);
		else if (get_data_type_size(data_type) == 8)
			transpose<uint64_t>(arm_op, src, dst);
		else
			return vart_ml_log_err_msg(vart_ml_error::CONFIG_IRIZ_MALFORMED,
			                           "ARM Ops Transpose is not implemented for data type %s.\n",
			                           to_string(data_type).data());

		vcd_event(vcd_context, NPU_ARM_OP_TRANSPOSE, 0);
		break;

	default:
		return vart_ml_log_err_msg(vart_ml_error::CONFIG_IRIZ_MALFORMED,
		                           "ARM Ops %s not implemented.\n",
		                           arm_ops_to_string(arm_op.type).c_str());
	}

	return vart_ml_error::SUCCESS;
}

bool VartMLRunner::is_arm_ops_needed(const NpuTensor& npu_tensor)
{
	uint64_t phy_addr = 0;
	if (npu_tensor.get_memory_type() == MemoryType::XRT_BO)
		phy_addr = npu_tensor.get_physical_address();
	bool phy_addr_ok = phy_addr != 0 && IS_ALIGNED(phy_addr);

	return (npu_tensor.get_memory_type() != MemoryType::XRT_BO) || !phy_addr_ok
	       || npu_tensor.get_info().tensor_type == TensorType::CPU;
}

int VartMLRunner::execute_arm_ops_in(const struct node_desc& node,
                                     std::vector<arm_ops>&   arm_ops_ref,
                                     const NpuTensor&        npu_tensor,
                                     size_t                  input_no,
                                     size_t                  tid,
                                     const struct addr&      ddr_addr)
{
	int                err;
	char               in_out[]    = "in";
	struct vcd_context vcd_context = { vcd_id_, tid };

	std::vector<arm_ops> arm_ops_to_run;

	if (npu_tensor.get_info().tensor_type != TensorType::HW)
		check_arm_ops(arm_ops_ref, npu_tensor, arm_ops_to_run);

	const NpuTensorInfo& hw_tensor_info = get_tensor_info_by_name(npu_tensor.get_info().name, TensorType::HW);
	if (arm_ops_to_run.empty())
	{
		if (check_user_config("debug.dump_IOs"))
			dump_data(
			    node.name, in_out, input_no, npu_tensor.get_virtual_address(), hw_tensor_info.size_in_bytes);

		vcd_event(vcd_context, NPU_WRITE_DDR, 1);
		err = npu_write_ddr(
		    ddr_addr, (const uint8_t*)npu_tensor.get_virtual_address(), hw_tensor_info.size_in_bytes);
		vcd_event(vcd_context, NPU_WRITE_DDR, 0);
		if (err)
			return err;
	}
	else
	{
		err = stats_->start_step(node.hash, EmbeddedStats::REORDER_IN);
		if (err)
			return err;

		arm_op_buf arm_ops_input(nullptr, std::free);
		arm_op_buf arm_ops_output(nullptr, std::free);

		vcd_event(vcd_context, NPU_REORDER_IN, 1);
		size_t arm_op_out_size = 0;
		for (size_t i = 0; i < arm_ops_to_run.size(); i++)
		{
			arm_op_out_size = arm_ops_to_run[i].output_size_in_bytes;

			if (i == 0)
			{
				arm_ops_output = arm_op_buf(std::malloc(arm_op_out_size), std::free);

				err = execute_arm_ops(node,
				                      tid,
				                      arm_ops_to_run[i],
				                      (void*)npu_tensor.get_virtual_address(),
				                      arm_ops_output.get());
				if (err)
					return err;
			}
			else
			{
				arm_ops_input  = std::move(arm_ops_output);
				arm_ops_output = arm_op_buf(std::malloc(arm_op_out_size), std::free);

				err =
				    execute_arm_ops(node, tid, arm_ops_to_run[i], arm_ops_input.get(), arm_ops_output.get());
				if (err)
					return err;
			}
		}
		vcd_event(vcd_context, NPU_REORDER_IN, 0);

		err = stats_->update_step(node.hash, EmbeddedStats::REORDER_IN);
		if (err)
			return err;

		if (check_user_config("debug.dump_IOs"))
			dump_data(node.name, in_out, input_no, arm_ops_output.get(), hw_tensor_info.size_in_bytes);

		vcd_event(vcd_context, NPU_WRITE_DDR, 1);
		err = npu_write_ddr(ddr_addr, (const uint8_t*)arm_ops_output.get(), arm_op_out_size);
		vcd_event(vcd_context, NPU_WRITE_DDR, 0);
		if (err)
			return err;
	}

	return vart_ml_error::SUCCESS;
}

int VartMLRunner::execute_arm_ops_out(const struct node_desc& node,
                                      std::vector<arm_ops>&   arm_ops_ref,
                                      const NpuTensor&        npu_tensor,
                                      size_t                  output_no,
                                      size_t                  tid,
                                      const struct addr&      ddr_addr)
{
	int                err;
	char               in_out[]    = "out";
	struct vcd_context vcd_context = { vcd_id_, tid };

	std::vector<arm_ops> arm_ops_to_run;

	if (npu_tensor.get_info().tensor_type != TensorType::HW)
		check_arm_ops(arm_ops_ref, npu_tensor, arm_ops_to_run);

	const NpuTensorInfo& hw_tensor_info = get_tensor_info_by_name(npu_tensor.get_info().name, TensorType::HW);
	if (arm_ops_to_run.empty())
	{
		size_t read_sz = (npu_tensor.get_memory_type() == MemoryType::USER_POINTER_CMA)
		                     ? npu_tensor.get_info().size_in_bytes
		                     : hw_tensor_info.size_in_bytes;

		vcd_event(vcd_context, NPU_READ_DDR, 1);
		err = npu_read_ddr(ddr_addr, (uint8_t*)npu_tensor.get_virtual_address(), read_sz);
		vcd_event(vcd_context, NPU_READ_DDR, 0);
		if (err)
			return err;

		if (check_user_config("debug.dump_IOs"))
			dump_data(node.name, in_out, output_no, npu_tensor.get_virtual_address(), read_sz);
	}
	else
	{
		arm_op_buf arm_ops_input(nullptr, std::free);
		arm_op_buf arm_ops_output(std::malloc(hw_tensor_info.size_in_bytes), std::free);

		vcd_event(vcd_context, NPU_READ_DDR, 1);
		err = npu_read_ddr(ddr_addr, (uint8_t*)arm_ops_output.get(), hw_tensor_info.size_in_bytes);
		vcd_event(vcd_context, NPU_READ_DDR, 0);
		if (err)
			return err;

		if (check_user_config("debug.dump_IOs"))
			dump_data(node.name, in_out, output_no, arm_ops_output.get(), hw_tensor_info.size_in_bytes);

		err = stats_->start_step(node.hash, EmbeddedStats::REORDER_OUT);
		if (err)
			return err;

		vcd_event(vcd_context, NPU_REORDER_OUT, 1);
		for (size_t i = 0; i < arm_ops_to_run.size(); i++)
		{
			if (i == arm_ops_to_run.size() - 1)
			{
				err = execute_arm_ops(node,
				                      tid,
				                      arm_ops_to_run[i],
				                      arm_ops_output.get(),
				                      (void*)npu_tensor.get_virtual_address());
				if (err)
					return err;
			}
			else
			{
				arm_ops_input  = std::move(arm_ops_output);
				arm_ops_output = arm_op_buf(std::malloc(arm_ops_to_run[i].output_size_in_bytes), std::free);

				err =
				    execute_arm_ops(node, tid, arm_ops_to_run[i], arm_ops_input.get(), arm_ops_output.get());
				if (err)
					return err;
			}
		}
		vcd_event(vcd_context, NPU_REORDER_OUT, 0);

		err = stats_->update_step(node.hash, EmbeddedStats::REORDER_OUT);
		if (err)
			return err;
	}

	return vart_ml_error::SUCCESS;
}
} // namespace vart
