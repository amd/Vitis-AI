/**
 * @file thread_pool.cpp
 *
 * @copyright Copyright 2026 Advanced Micro Devices Inc.
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
#include <fstream>
#include <iostream>

#include "io/io.h"
#include "npu_runner/npu_runner.h"
#include "onnx_runner/onnx_runner.h"
#include "utils/vcd_stats.h"
#include "vart_ml_runner.hpp"

namespace vart
{
static size_t get_nbuf_size(npu_snapshot_t* snap)
{
	FpgaArchitecture arch = snap->arch;

	if (arch != FpgaArchitecture::AIEML_V1C)
		return (snap->inputs.size() + snap->outputs.size()) * npu_get_nbcores(snap->ip_idx);

	// Need space to store temporary area information
	size_t nbuf_size = npu_get_nbddrs();

	for (size_t i = 0; i < snap->inputs.size(); i++)
		nbuf_size += snap->inputs[i].batchSize;

	for (size_t i = 0; i < snap->constants.size(); i++)
		nbuf_size += snap->constants[i].batchSize;

	for (size_t i = 0; i < snap->outputs.size(); i++)
		nbuf_size += snap->outputs[i].batchSize;

	return nbuf_size;
}

static int set_nbuff_reg(struct node_desc& node, npu_snapshot_t* snap, size_t nb_ctx)
{
	FpgaArchitecture arch = snap->arch;

	size_t      nbuf_size = get_nbuf_size(snap);
	size_t      nb_ddrs   = npu_get_nbddrs();
	struct addr ddr_addr;
	size_t      err;

	node.input_ddr_addrs.resize(nb_ctx);
	node.output_ddr_addrs.resize(nb_ctx);
	node.nbuf.resize(nb_ctx);

	std::vector<std::vector<struct addr>> constant_ddr_addrs;
	constant_ddr_addrs.resize(snap->constants.size());
	for (size_t i = 0; i < snap->constants.size(); i++)
	{
		auto constant = snap->constants[i];

		constant_ddr_addrs[i].resize(constant.batchSize);

		for (size_t b = 0; b < constant.batchSize; b++)
		{
			constant_ddr_addrs[i][b] = constant.ddr_addrs[0][0][b];

			// Update constant addresses with new configs address
			uint64_t phy_addr = constant.ddr_addrs[0][0][b].phy_addr;
			for (size_t mem = 0; mem < npu_get_nbextmems(); mem++)
			{
				size_t memBase = npu_get_extmemBaseAddr(mem);
				if (memBase <= phy_addr && phy_addr < memBase + npu_get_extmemlen(mem))
				{
					phy_addr = phy_addr - snap->constant_addr_in_snap[mem] + snap->config_addr[mem].phy_addr;

					constant_ddr_addrs[i][b].phy_addr       = phy_addr;
					constant_ddr_addrs[i][b].nbuff_conf_val = npu_get_nbuff_conf_val_from_phy(phy_addr);

					break;
				}
			}
		}
	}

	const size_t nb_sys           = npu_get_nbsystems(snap->ip_idx);
	const size_t nb_cores_per_sys = (arch == FpgaArchitecture::AIEML_V1C) ? 1 : npu_get_nbcores(snap->ip_idx);

	for (size_t tid = 0; tid < nb_ctx; tid++)
	{
		node.nbuf[tid]            = std::vector<std::vector<size_t>>(nb_sys, std::vector<size_t>(nbuf_size));
		node.input_ddr_addrs[tid] = std::vector<std::vector<std::vector<std::vector<struct addr>>>>(
		    nb_sys,
		    std::vector<std::vector<std::vector<struct addr>>>(
		        nb_cores_per_sys, std::vector<std::vector<struct addr>>(snap->inputs.size())));
		node.output_ddr_addrs[tid] = std::vector<std::vector<std::vector<std::vector<struct addr>>>>(
		    nb_sys,
		    std::vector<std::vector<std::vector<struct addr>>>(
		        nb_cores_per_sys, std::vector<std::vector<struct addr>>(snap->outputs.size())));

		if (arch == FpgaArchitecture::AIEML_V1C)
			// There is only one system in AIEML_V1C
			for (size_t i = 0; i < npu_get_tmp_area_size(snap); i++)
				node.nbuf[tid][0][i] = npu_get_tmp_area_nbuff_conf(snap, i);

		for (size_t i = 0; i < snap->inputs.size(); i++)
		{
			auto   input = snap->inputs[i];
			size_t size  = input.ddrimgsize;

			if (check_user_config("debug.forceSnapshotBuffer"))
			{
				if (arch == FpgaArchitecture::AIEML_V1C)
				{
					node.input_ddr_addrs[tid][0][0][i].resize(input.batchSize);

					for (size_t b = 0; b < input.batchSize; b++)
					{
						node.input_ddr_addrs[tid][0][0][i][b]   = input.ddr_addrs[0][0][b];
						node.nbuf[tid][0][input.nbuf_idx[0][b]] = input.ddr_addrs[0][0][b].nbuff_conf_val;
					}
				}
				else
					for (size_t sys = 0; sys < nb_sys; sys++)
						for (size_t core = 0; core < nb_cores_per_sys; core++)
						{
							node.input_ddr_addrs[tid][sys][core][i].resize(input.batch_size_per_core);

							for (size_t b = 0; b < input.batch_size_per_core; b++)
								node.input_ddr_addrs[tid][sys][core][i][b] = input.ddr_addrs[sys][core][b];

							node.nbuf[tid][sys][input.nbuf_idx[sys][core]] =
							    input.ddr_addrs[sys][core][0].nbuff_conf_val;
						}
			}
			else if (arch != FpgaArchitecture::AIEML_V1C)
			{
				for (size_t sys = 0; sys < nb_sys; sys++)
					for (size_t core = 0; core < nb_cores_per_sys; core++)
					{
						struct addr input_ddr_addr;
						err = npu_allocate_memory(size * input.batch_size_per_core, sys, &input_ddr_addr);
						if (err)
							return err;

						node.input_ddr_addrs[tid][sys][core][i].resize(input.batch_size_per_core);
						for (size_t b = 0; b < input.batch_size_per_core; b++)
						{
							err = npu_allocate_sub(
							    &input_ddr_addr, b * size, size, &node.input_ddr_addrs[tid][sys][core][i][b]);
							if (err)
								return err;
						}

						node.nbuf[tid][sys][input.nbuf_idx[sys][core]] =
						    node.input_ddr_addrs[tid][sys][core][i][0].nbuff_conf_val;
					}
			}
			else
			{
				node.input_ddr_addrs[tid][0][0][i].resize(input.batchSize);

				for (size_t b = 0; b < input.batchSize; b++)
					if ((err = npu_allocate_memory(size, b % nb_ddrs, &ddr_addr)) == vart_ml_error::SUCCESS)
					{
						node.input_ddr_addrs[tid][0][0][i][b]   = ddr_addr;
						node.nbuf[tid][0][input.nbuf_idx[0][b]] = ddr_addr.nbuff_conf_val;
					}
					else
						return err;
			}
		}

		for (size_t i = 0; i < snap->outputs.size(); i++)
		{
			auto   output = snap->outputs[i];
			size_t size   = output.ddrimgsize;

			if (check_user_config("debug.forceSnapshotBuffer"))
			{
				if (arch == FpgaArchitecture::AIEML_V1C)
				{
					node.output_ddr_addrs[tid][0][0][i].resize(output.batchSize);

					for (size_t b = 0; b < output.batchSize; b++)
					{
						node.output_ddr_addrs[tid][0][0][i][b]   = output.ddr_addrs[0][0][b];
						node.nbuf[tid][0][output.nbuf_idx[0][b]] = output.ddr_addrs[0][0][b].nbuff_conf_val;
					}
				}
				else
					for (size_t sys = 0; sys < nb_sys; sys++)
						for (size_t core = 0; core < nb_cores_per_sys; core++)
						{
							node.output_ddr_addrs[tid][sys][core][i].resize(output.batch_size_per_core);

							for (size_t b = 0; b < output.batch_size_per_core; b++)
								node.output_ddr_addrs[tid][sys][core][i][b] = output.ddr_addrs[sys][core][b];

							node.nbuf[tid][sys][output.nbuf_idx[sys][core]] =
							    output.ddr_addrs[sys][core][0].nbuff_conf_val;
						}
			}
			else if (arch != FpgaArchitecture::AIEML_V1C)
			{
				for (size_t sys = 0; sys < nb_sys; sys++)
					for (size_t core = 0; core < nb_cores_per_sys; core++)
					{
						struct addr output_ddr_addr;
						err = npu_allocate_memory(size * output.batch_size_per_core, sys, &output_ddr_addr);
						if (err)
							return err;

						node.output_ddr_addrs[tid][sys][core][i].resize(output.batch_size_per_core);
						for (size_t b = 0; b < output.batch_size_per_core; b++)
						{
							err = npu_allocate_sub(&output_ddr_addr,
							                       b * size,
							                       size,
							                       &node.output_ddr_addrs[tid][sys][core][i][b]);
							if (err)
								return err;
						}

						node.nbuf[tid][sys][output.nbuf_idx[sys][core]] =
						    node.output_ddr_addrs[tid][sys][core][i][0].nbuff_conf_val;
					}
			}
			else
			{
				node.output_ddr_addrs[tid][0][0][i].resize(output.batchSize);

				for (size_t b = 0; b < output.batchSize; b++)
					if ((err = npu_allocate_memory(size, b % nb_ddrs, &ddr_addr)) == vart_ml_error::SUCCESS)
					{
						node.output_ddr_addrs[tid][0][0][i][b]   = ddr_addr;
						node.nbuf[tid][0][output.nbuf_idx[0][b]] = ddr_addr.nbuff_conf_val;
					}
					else
						return err;
			}
		}

		for (size_t i = 0; i < snap->constants.size(); i++)
			for (size_t b = 0; b < snap->constants[i].batchSize; b++)
				node.nbuf[tid][0][snap->constants[i].nbuf_idx[0][b]] =
				    constant_ddr_addrs[i][b].nbuff_conf_val;
	}

	return vart_ml_error::SUCCESS;
}

static void free_nbuff_reg(const struct node_desc& node)
{
	for (auto& t : node.input_ddr_addrs)
		for (auto& sys : t)
			for (auto& core : sys)
				for (auto& in : core)
					for (auto& addr : in)
						npu_free(addr.ddr_vaddr);

	for (auto& t : node.output_ddr_addrs)
		for (auto& sys : t)
			for (auto& core : sys)
				for (auto& out : core)
					for (auto& addr : out)
						npu_free(addr.ddr_vaddr);
}

void VartMLRunner::tpool_init(size_t nb_threads)
{
	FpgaArchitecture arch;
	npu_get_architecture(&arch);

	// If the snapshot buffers are used, we can only have one thread
	if (check_user_config("debug.forceSnapshotBuffer"))
		nb_threads = 1;
	// If no number of threads was given, use number max of threads supported.
	else if (nb_threads == 0)
		nb_threads = std::thread::hardware_concurrency();

	// Compute per-thread IO memory footprint and cap nb_threads to what DDR can hold.
	size_t io_bytes_per_thread = 0;
	for (auto& [execution_order, node] : nodes_)
	{
		if (node.execution_mode != NodeExecutionMode::NPU)
			continue;
		npu_snapshot_t* snap = (npu_snapshot_t*)node.context_ptr;
		for (size_t i = 0; i < snap->inputs.size(); i++)
			io_bytes_per_thread += ((snap->inputs[i].ddrimgsize + 255) & ~255ULL) * snap->inputs[i].batchSize;
		for (size_t i = 0; i < snap->outputs.size(); i++)
			io_bytes_per_thread +=
			    ((snap->outputs[i].ddrimgsize + 255) & ~255ULL) * snap->outputs[i].batchSize;
	}

	if (io_bytes_per_thread > 0)
	{
		size_t nb_ddrs  = (arch != FpgaArchitecture::AIEML_V1C) ? 1 : npu_get_nbddrs();
		size_t ddr_free = 0;
		for (size_t d = 0; d < nb_ddrs; d++)
			ddr_free += npu_get_ddr_free_bytes(d);

		size_t max_threads = ddr_free / io_bytes_per_thread;

		if (max_threads == 0)
			throw std::runtime_error("Not enough DDR space for even 1 thread of IO buffers"
			                         " (free: "
			                         + std::to_string(ddr_free)
			                         + " bytes,"
			                           " needed: "
			                         + std::to_string(io_bytes_per_thread) + " bytes)");

		if (max_threads < nb_threads)
		{
			vart_ml_log(LOG_WARN,
			            "Warning: DDR can only fit %zu thread(s) of IO buffers (free: %zu bytes,"
			            " needed per thread: %zu bytes). Reducing from %zu to %zu thread(s).\n",
			            max_threads,
			            ddr_free,
			            io_bytes_per_thread,
			            nb_threads,
			            max_threads);
			nb_threads = max_threads;
		}
	}

	thread_pool_ = std::make_shared<tpool_t>();

	thread_pool_->current_queue_size = 0;
	thread_pool_->queue_head         = NULL;
	thread_pool_->queue_tail         = NULL;
	thread_pool_->shutdown           = false;

	// Reserve the maximum number of threads that will be created.
	thread_pool_->threads.reserve(nb_threads);

	tpool_allocate_resources();

	for (auto& [execution_order, node] : nodes_)
	{
		(void)execution_order;

		if (node.execution_mode == NodeExecutionMode::NPU)
		{
			int             err;
			npu_snapshot_t* snapshot = (npu_snapshot_t*)node.context_ptr;

			// Allocate in/out/constant buffers using NBUFF scheme
			err = set_nbuff_reg(node, snapshot, nb_threads);
			if (err != vart_ml_error::SUCCESS)
				throw std::runtime_error(vart_ml_error::exception_message(err));
		}
	}

	// Set vcd's number of threads.
	vcd_set_nb_threads(nb_threads);
}

void VartMLRunner::tpool_allocate_resources(void)
{
	FpgaArchitecture arch;
	npu_get_architecture(&arch);

	// Clear before resizing buffers.
	internal_buffers_.clear();

	// Allocate resource for all threads or at least one set of resources for synchronous run.
	const size_t nb_ctx = thread_pool_->threads.capacity();

	// Allocate resource for all threads.
	internal_buffers_.resize(nb_ctx);

	uint8_t nb_ddrs               = npu_get_nb_ddrs();
	size_t  batch_size_per_system = batch_size_per_core_ * nb_cores_per_system_;

	for (auto& [execution_order, node] : nodes_)
	{
		if (node.execution_mode == NodeExecutionMode::ONNX)
		{
			node.onnx_staging_inputs.assign(nb_ctx, std::vector<void*>(node.input_count));
			node.onnx_tmp_inputs.assign(nb_ctx, std::vector<void*>(node.input_count));

			for (size_t in_idx = 0; in_idx < node.input_count; in_idx++)
			{
				const NpuTensorInfo* input_tensor;

				TensorDirectionPriv input_tensor_dir = tensors_.at(node.input_tensors_name[in_idx]).first;
				size_t              input_tensor_idx = tensors_.at(node.input_tensors_name[in_idx]).second;

				if (input_tensor_dir == TensorDirectionPriv::INTERNAL)
				{
					input_tensor = &hw_internal_tensors_[input_tensor_idx];

					for (size_t ctx_id = 0; ctx_id < nb_ctx; ctx_id++)
						if (!internal_buffers_[ctx_id].contains(input_tensor->name))
						{
							if (arch != FpgaArchitecture::AIEML_V1C)
							{
								bool is_done = false;
								for (size_t sys = 0; sys < nb_systems_ && !is_done; sys++)
									for (size_t s = 0; s < batch_size_per_system && !is_done; s++)
									{
										internal_buffers_[ctx_id][input_tensor->name].push_back(
										    malloc_buffer(input_tensor->size_in_bytes, sys));
										if (sys * batch_size_per_system + s + 1 >= batchSize_)
											is_done = true;
									}
							}
							else if (npu_is_xrt_en())
							{
								void* parent_vaddr = npu_get_ddr_vaddr_from_vaddr(
								    npu_malloc(batchSize_ * input_tensor->size_in_bytes, in_idx % nb_ddrs));
								internal_parent_buffers_.push_back(parent_vaddr);

								for (size_t b = 0; b < batchSize_; b++)
									internal_buffers_[ctx_id][input_tensor->name].push_back(
									    npu_malloc_sub(parent_vaddr,
									                   b * input_tensor->size_in_bytes,
									                   input_tensor->size_in_bytes));
							}
							else
								for (size_t b = 0; b < batchSize_; b++)
									internal_buffers_[ctx_id][input_tensor->name].push_back(
									    malloc_buffer(input_tensor->size_in_bytes, b % nb_ddrs));
						}
				}

				size_t one_img =
				    get_tensor_info_by_name(node.input_tensors_name[in_idx], TensorType::HW).size_in_bytes;
				for (size_t ctx_id = 0; ctx_id < nb_ctx; ctx_id++)
				{
					node.onnx_staging_inputs[ctx_id][in_idx] = std::malloc(one_img * batchSize_);
					node.onnx_tmp_inputs[ctx_id][in_idx]     = std::malloc(one_img);
				}
			}

			node.onnx_staging_outputs.assign(nb_ctx, std::vector<void*>(node.output_count));
			node.onnx_tmp_outputs.assign(nb_ctx, std::vector<void*>(node.output_count));

			for (size_t out_idx = 0; out_idx < node.output_count; out_idx++)
			{
				const NpuTensorInfo* output_tensor;

				TensorDirectionPriv output_tensor_dir = tensors_.at(node.output_tensors_name[out_idx]).first;
				size_t              output_tensor_idx = tensors_.at(node.output_tensors_name[out_idx]).second;

				if (output_tensor_dir == TensorDirectionPriv::INTERNAL)
				{
					output_tensor = &hw_internal_tensors_[output_tensor_idx];

					for (size_t ctx_id = 0; ctx_id < nb_ctx; ctx_id++)
						if (!internal_buffers_[ctx_id].contains(output_tensor->name))
						{
							if (arch != FpgaArchitecture::AIEML_V1C)
							{
								bool is_done = false;
								for (size_t sys = 0; sys < nb_systems_ && !is_done; sys++)
									for (size_t s = 0; s < batch_size_per_system && !is_done; s++)
									{
										internal_buffers_[ctx_id][output_tensor->name].push_back(
										    malloc_buffer(output_tensor->size_in_bytes, sys));
										if (sys * batch_size_per_system + s + 1 >= batchSize_)
											is_done = true;
									}
							}
							else if (npu_is_xrt_en())
							{
								void* parent_vaddr = npu_get_ddr_vaddr_from_vaddr(
								    npu_malloc(batchSize_ * output_tensor->size_in_bytes, out_idx % nb_ddrs));
								internal_parent_buffers_.push_back(parent_vaddr);

								for (size_t b = 0; b < batchSize_; b++)
									internal_buffers_[ctx_id][output_tensor->name].push_back(
									    npu_malloc_sub(parent_vaddr,
									                   b * output_tensor->size_in_bytes,
									                   output_tensor->size_in_bytes));
							}
							else
								for (size_t b = 0; b < batchSize_; b++)
									internal_buffers_[ctx_id][output_tensor->name].push_back(
									    malloc_buffer(output_tensor->size_in_bytes, b % nb_ddrs));
						}
				}

				size_t one_img =
				    get_tensor_info_by_name(node.output_tensors_name[out_idx], TensorType::HW).size_in_bytes;
				for (size_t ctx_id = 0; ctx_id < nb_ctx; ctx_id++)
				{
					node.onnx_staging_outputs[ctx_id][out_idx] = std::malloc(one_img * batchSize_);
					node.onnx_tmp_outputs[ctx_id][out_idx]     = std::malloc(one_img);
				}
			}
		}
	}
}

void VartMLRunner::tpool_thread(size_t tid)
{
	auto&         tpool = thread_pool_;
	tpool_work_t* work; // pointer to a work in the queue

	while (true)
	{
		std::unique_lock<std::mutex> lock(tpool->queue_lock); // make access to queue exclusive
		tpool->cond_var.wait(lock, [tpool] { return (tpool->shutdown || tpool->current_queue_size != 0); });

		if (tpool->shutdown)
			break;

		work = tpool->queue_head;
		tpool->current_queue_size--;

		if (tpool->current_queue_size == 0)
			tpool->queue_head = tpool->queue_tail = NULL;
		else
			tpool->queue_head = work->next;

		/* Release lock before starting work */
		lock.unlock();

		/* Perform work */
		execute_job(work->inputs, work->outputs, tid, work->job_handle);
		delete work;
	}
}

void VartMLRunner::tpool_fill(void)
{
	for (size_t tid = 0; tid < thread_pool_->threads.capacity(); tid++)
		thread_pool_->threads.push_back(std::thread(&VartMLRunner::tpool_thread, this, tid));
}

void VartMLRunner::tpool_destroy(void)
{
	auto& tpool = thread_pool_;

	if (tpool->threads.size() > 0)
	{
		{
			std::lock_guard<std::mutex> lock(tpool->queue_lock);
			tpool->shutdown = true;
		}
		tpool->cond_var.notify_all();

		for (auto& t : tpool->threads)
		{
			if (t.joinable())
				t.join();
		}

		tpool->threads.clear();

		while (tpool->queue_head)
		{
			tpool_work_t* work = tpool->queue_head;
			tpool->queue_head  = work->next;
			delete work;
		}
	}

	FpgaArchitecture arch;
	npu_get_architecture(&arch);

	// Return now if no allocation were done
	if (check_user_config("debug.forceSnapshotBuffer"))
		return;

	for (const auto& [execution_order, node] : nodes_)
	{
		(void)execution_order;

		if (node.execution_mode == NodeExecutionMode::NPU)
			free_nbuff_reg(node);
	}

	for (auto& [execution_order, node] : nodes_)
	{
		(void)execution_order;

		if (node.execution_mode != NodeExecutionMode::ONNX)
			continue;

		for (auto& ctx : node.onnx_staging_inputs)
			for (void* ptr : ctx)
				std::free(ptr);

		for (auto& ctx : node.onnx_staging_outputs)
			for (void* ptr : ctx)
				std::free(ptr);

		for (auto& ctx : node.onnx_tmp_inputs)
			for (void* ptr : ctx)
				std::free(ptr);

		for (auto& ctx : node.onnx_tmp_outputs)
			for (void* ptr : ctx)
				std::free(ptr);
	}

	for (auto& ctx_buffers : internal_buffers_)
		for (auto& [name, bufs] : ctx_buffers)
			for (void* ptr : bufs)
				free_buffer(ptr);
	internal_buffers_.clear();

	for (void* ptr : internal_parent_buffers_)
		free_buffer(ptr);
	internal_parent_buffers_.clear();
}

} // namespace vart
