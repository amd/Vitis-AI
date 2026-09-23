/**
 * @file execute.cpp
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
#include "onnx_runner/onnx_runner.h"
#include "utils/stats.h"
#include "utils/vcd_stats.h"
#include "vart_ml_runner.hpp"
#include "vart_npu_tensor_impl.hpp" // For controlled NpuTensor implementation access via NpuTensorPrivAccess

namespace vart
{
StatusCode VartMLRunner::execute_job(const std::vector<std::vector<NpuTensor>>& inputs,
                                     std::vector<std::vector<NpuTensor>>&       outputs,
                                     size_t                                     tid,
                                     JobHandle                                  job_handle)
{
	const size_t runner_batch_size = get_batch_size();

	if (inputs.size() > runner_batch_size)
	{
		const size_t total = inputs.size();

		for (size_t offset = 0; offset < total; offset += runner_batch_size)
		{
			size_t chunk = std::min(runner_batch_size, total - offset);

			std::vector<std::vector<NpuTensor>> chunk_inputs(inputs.begin() + offset,
			                                                 inputs.begin() + offset + chunk);
			std::vector<std::vector<NpuTensor>> chunk_outputs(outputs.begin() + offset,
			                                                  outputs.begin() + offset + chunk);

			JobHandle sub_handle;
			sub_handle.job_id = 0;
			sub_handle.status = StatusCode::SUCCESS;

			job_handle.status = execute_job(chunk_inputs, chunk_outputs, tid, sub_handle);

			for (size_t b = 0; b < chunk; b++)
				outputs[offset + b] = std::move(chunk_outputs[b]);

			if (job_handle.status != StatusCode::SUCCESS)
				break;
		}

		notify_completion(job_handle);
		return job_handle.status;
	}

	struct vcd_context vcd_context = { vcd_id_, tid };

	FpgaArchitecture arch;
	npu_get_architecture(&arch);

	// Set the current run batch size
	stats_->setBatchSize(inputs.size());

	// Set the inputs/outputs shape and data type for the graph
	if (stats_->isFirstRun())
	{
		std::vector<std::vector<uint>> in_shape;
		std::vector<std::string>       in_data_type;
		for (auto& input : inputs.front())
		{
			in_shape.push_back(input.get_info().shape);
			in_data_type.push_back(std::string(to_string(input.get_info().data_type)));
		}

		std::vector<std::vector<uint>> out_shape;
		std::vector<std::string>       out_data_type;
		for (auto& output : outputs.front())
		{
			out_shape.push_back(output.get_info().shape);
			out_data_type.push_back(std::string(to_string(output.get_info().data_type)));
		}

		// Set global stats' shapes.
		stats_->setInputDims(stats_->get_hash(), in_shape);
		stats_->setOutputDims(stats_->get_hash(), out_shape);
		// Set global stats' data types.
		stats_->setInputDataType(stats_->get_hash(), in_data_type);
		stats_->setOutputDataType(stats_->get_hash(), out_data_type);
	}

	stats_->start_step(stats_->get_hash(), EmbeddedStats::WHOLEGRAPH);
	vcd_event(vcd_context, EXECUTE, 1);

	assert(tid < internal_buffers_.size());
	std::map<std::string, std::vector<void*>>& internal_buffers = internal_buffers_[tid];

	// Iterate through nodes and execute them.
	for (const auto& [execution_order, node] : nodes_)
	{
		std::vector<std::vector<NpuTensor>> node_inputs(
		    ((execution_order == 0) || (get_batch_size() > inputs.size())) ? inputs.size()
		                                                                   : get_batch_size());
		std::vector<std::vector<NpuTensor>> node_outputs(
		    ((execution_order == nodes_.size() - 1) || (get_batch_size() > outputs.size()))
		        ? outputs.size()
		        : get_batch_size());

		if (node.link_mode_in == NodeExtLinkMode::DIRECT)
		{
			for (size_t b = 0; b < node_inputs.size(); b++)
				for (size_t in_idx = 0; in_idx < node.input_count; in_idx++)
				{
					node_inputs[b].push_back(inputs[b][in_idx]);

					NpuTensorPrivAccess::get_impl(node_inputs[b][in_idx])->is_packed_ =
					    NpuTensorPrivAccess::get_impl(inputs[b][in_idx])->is_packed_;

					if (batchSize_ == 1)
						NpuTensorPrivAccess::get_impl(node_inputs[b][in_idx])->is_packed_ = true;
				}
		}
		else if (node.link_mode_in == NodeExtLinkMode::COPY_PTRS)
		{
			for (size_t in_idx = 0; in_idx < node.input_count; in_idx++)
			{
				TensorDirectionPriv input_tensor_dir = tensors_.at(node.input_tensors_name[in_idx]).first;
				size_t              input_tensor_idx = tensors_.at(node.input_tensors_name[in_idx]).second;

				const NpuTensorInfo* input_tensor;
				if (input_tensor_dir == TensorDirectionPriv::INPUT)
				{
					input_tensor = &cpu_input_tensors_[input_tensor_idx];

					for (size_t b = 0; b < node_inputs.size(); b++)
						for (size_t i = 0; i < inputs[b].size(); i++)
							if (input_tensor->name == inputs[b][i].get_info().name)
							{
								node_inputs[b].push_back(inputs[b][i]);

								NpuTensorPrivAccess::get_impl(node_inputs[b][in_idx])->is_packed_ =
								    NpuTensorPrivAccess::get_impl(inputs[b][i])->is_packed_;

								if (batchSize_ == 1)
									NpuTensorPrivAccess::get_impl(node_inputs[b][in_idx])->is_packed_ = true;
							}
				}
				else if (input_tensor_dir == TensorDirectionPriv::OUTPUT)
				{
					input_tensor = &cpu_output_tensors_[input_tensor_idx];

					for (size_t b = 0; b < node_inputs.size(); b++)
						for (size_t i = 0; i < outputs[b].size(); i++)
							if (input_tensor->name == outputs[b][i].get_info().name)
							{
								node_inputs[b].push_back(outputs[b][i]);

								NpuTensorPrivAccess::get_impl(node_inputs[b][in_idx])->is_packed_ =
								    NpuTensorPrivAccess::get_impl(outputs[b][i])->is_packed_;

								if (batchSize_ == 1)
									NpuTensorPrivAccess::get_impl(node_inputs[b][in_idx])->is_packed_ = true;
							}
				}
				else
				{
					input_tensor = &hw_internal_tensors_[input_tensor_idx];

					for (size_t b = 0; b < node_inputs.size(); b++)
					{
						node_inputs[b].push_back(vart::NpuTensor(*input_tensor,
						                                         internal_buffers[input_tensor->name][b],
						                                         vart::MemoryType::XRT_BO));

						if (input_tensor->size_in_bytes
						        == cpu_internal_tensors_[input_tensor_idx].size_in_bytes
						    && npu_is_xrt_en() && arch == FpgaArchitecture::AIEML_V1C
						    && node_inputs.size() == batchSize_)
							NpuTensorPrivAccess::get_impl(node_inputs[b][in_idx])->is_packed_ = true;
					}
				}
			}
		}

		if ((node.link_mode_out == NodeExtLinkMode::DIRECT) || node.pl_node)
		{
			for (size_t b = 0; b < node_outputs.size(); b++)
				for (size_t out_idx = 0; out_idx < node.output_count; out_idx++)
				{
					node_outputs[b].push_back(outputs[b][out_idx]);

					NpuTensorPrivAccess::get_impl(node_outputs[b][out_idx])->is_packed_ =
					    NpuTensorPrivAccess::get_impl(outputs[b][out_idx])->is_packed_;

					if (batchSize_ == 1)
						NpuTensorPrivAccess::get_impl(node_outputs[b][out_idx])->is_packed_ = true;
				}
		}
		else
		{
			// If node outputs are internal or node is in COPY_BUFS link mode, set IOs to tensor's buffers.
			for (size_t out_idx = 0; out_idx < node.output_count; out_idx++)
			{
				TensorDirectionPriv output_tensor_dir = tensors_.at(node.output_tensors_name[out_idx]).first;
				size_t              output_tensor_idx = tensors_.at(node.output_tensors_name[out_idx]).second;

				const NpuTensorInfo* output_tensor;
				if (output_tensor_dir == TensorDirectionPriv::OUTPUT)
				{
					output_tensor = &cpu_output_tensors_[output_tensor_idx];

					for (size_t b = 0; b < node_outputs.size(); b++)
						for (size_t i = 0; i < outputs[b].size(); i++)
							if (output_tensor->name == outputs[b][i].get_info().name)
							{
								node_outputs[b].push_back(outputs[b][i]);

								NpuTensorPrivAccess::get_impl(node_outputs[b][out_idx])->is_packed_ =
								    NpuTensorPrivAccess::get_impl(outputs[b][i])->is_packed_;

								if (batchSize_ == 1)
									NpuTensorPrivAccess::get_impl(node_outputs[b][out_idx])->is_packed_ =
									    true;
							}
				}
				else if (output_tensor_dir == TensorDirectionPriv::INTERNAL)
				{
					output_tensor = &hw_internal_tensors_[output_tensor_idx];

					for (size_t b = 0; b < node_outputs.size(); b++)
					{
						node_outputs[b].push_back(vart::NpuTensor(*output_tensor,
						                                          internal_buffers[output_tensor->name][b],
						                                          vart::MemoryType::XRT_BO));

						if (output_tensor->size_in_bytes
						        == cpu_internal_tensors_[output_tensor_idx].size_in_bytes
						    && npu_is_xrt_en() && arch == FpgaArchitecture::AIEML_V1C
						    && node_outputs.size() == batchSize_)
							NpuTensorPrivAccess::get_impl(node_outputs[b][out_idx])->is_packed_ = true;
					}
				}
			}
		}

		switch (node.execution_mode)
		{
		case NodeExecutionMode::NPU:
			job_handle.status = to_status_code(execute_npu(node, node_inputs, node_outputs, tid));
			break;

		case NodeExecutionMode::ONNX:
			job_handle.status = to_status_code(execute_onnx(node, node_inputs, node_outputs, tid));
			break;
		}

		if (job_handle.status != StatusCode::SUCCESS)
			break;
	}

	// If the execution failed, don't notify the stats so that this run's stats won't be considered.
	if (job_handle.status == StatusCode::SUCCESS)
	{
		vcd_event(vcd_context, EXECUTE, 0);
		stats_->stop_step(stats_->get_hash(), EmbeddedStats::WHOLEGRAPH);
	}

	notify_completion(job_handle);
	return job_handle.status;
}

StatusCode VartMLRunner::execute(const std::vector<std::vector<NpuTensor>>& inputs,
                                 std::vector<std::vector<NpuTensor>>&       outputs) noexcept
{
	JobHandle job_handle;
	job_handle.job_id = 0;
	job_handle.status = StatusCode::SUCCESS;

	return execute_job(inputs, outputs, 0, job_handle);
}

JobHandle VartMLRunner::execute_async(const std::vector<std::vector<NpuTensor>>& inputs,
                                      std::vector<std::vector<NpuTensor>>&       outputs) noexcept
{
	JobHandle job_handle;
	job_handle.job_id = allocate_job_id(nullptr);
	job_handle.status = StatusCode::SUCCESS;

	auto& tpool = thread_pool_;

	// If this is the first async run, fill thread pool.
	std::call_once(tpool_fill_once_, [this] { tpool_fill(); });

	tpool_work_t* work;
	{
		std::lock_guard<std::mutex> lock(tpool->queue_lock); // make access to queue exclusive

		/* Create new work structure */
		work = new tpool_work_t{ inputs, outputs, job_handle, NULL };

		if (tpool->current_queue_size == 0)
		{
			tpool->queue_tail = tpool->queue_head = work;
			tpool->cond_var.notify_one();
		}
		else
		{
			(tpool->queue_tail)->next = work;
			tpool->queue_tail         = work;
		}
		tpool->current_queue_size++;
	}

	return job_handle;
}

StatusCode VartMLRunner::wait(const JobHandle& job_handle, std::chrono::milliseconds timeout) noexcept
{
	auto job = find_job_slot(job_handle.job_id);
	if (job == nullptr)
		return StatusCode::INVALID_JOB_ID;

	auto ret = StatusCode::SUCCESS;
	try
	{
		auto               future = job->promise.get_future();
		std::future_status cv     = std::future_status::ready;
		if (timeout != std::chrono::milliseconds(0))
			cv = future.wait_for(timeout);
		else if (job->is_pending)
			return StatusCode::JOB_PENDING;

		if (cv == std::future_status::ready)
			ret = future.get();
		else if (cv == std::future_status::timeout)
			ret = StatusCode::JOB_PENDING;
	}
	catch (std::exception& e)
	{
		delete_job_slot(job_handle.job_id);

		return to_status_code(vart_ml_log_err_msg(vart_ml_error::DEVICE_JOB_EXCEPTION,
		                                          "Exceptions job_id = %d, what = %s.\n",
		                                          job_handle.job_id,
		                                          e.what()));
	}

	delete_job_slot(job_handle.job_id);

	return ret;
}

JobHandle VartMLRunner::execute_async(const std::vector<std::vector<NpuTensor>>& inputs,
                                      std::vector<std::vector<NpuTensor>>&       outputs,
                                      ExecuteAsyncCallback                       cb) noexcept
{
	JobHandle job_handle;
	job_handle.job_id = allocate_job_id(cb);
	job_handle.status = StatusCode::SUCCESS;

	auto& tpool = thread_pool_;

	// If this is the first async run, fill thread pool.
	std::call_once(tpool_fill_once_, [this] { tpool_fill(); });

	tpool_work_t* work;
	{
		std::lock_guard<std::mutex> lock(tpool->queue_lock); // make access to queue exclusive

		/* Create new work structure */
		work = new tpool_work_t{ inputs, outputs, job_handle, NULL };

		if (tpool->current_queue_size == 0)
		{
			tpool->queue_tail = tpool->queue_head = work;
			tpool->cond_var.notify_one();
		}
		else
		{
			(tpool->queue_tail)->next = work;
			tpool->queue_tail         = work;
		}
		tpool->current_queue_size++;
	}

	return job_handle;
}

int VartMLRunner::execute_npu(const struct node_desc&                    node,
                              const std::vector<std::vector<NpuTensor>>& inputs,
                              std::vector<std::vector<NpuTensor>>&       outputs,
                              size_t                                     tid)
{
	int                err;
	npu_snapshot_t*    snapshot    = (npu_snapshot_t*)node.context_ptr;
	FpgaArchitecture   arch        = snapshot->arch;
	struct vcd_context vcd_context = { vcd_id_, tid };

	err = stats_->start_step(node.hash, EmbeddedStats::VART_FULL);
	if (err)
		return err;
	vcd_event(vcd_context, NPU_EXECUTE, 1);

	// [nb_sys][nb_input/nb_output (V1 and V2) or nbuf_size (AIEML_V1C)]
	std::vector<std::vector<uint64_t>> nbuf = node.nbuf[tid];

	err = stats_->start_step(node.hash, EmbeddedStats::UPLOAD);
	if (err)
		return err;
	vcd_event(vcd_context, NPU_UPLOAD, 1);

	bool skip_upload = true;
	for (size_t b = 0; b < batchSize_; b++)
	{
		for (size_t i = 0; i < npu_get_nbinputs(snapshot); i++)
		{
			size_t sys_idx  = b / (snapshot->inputs[i].batch_size_per_core * nb_cores_per_system_);
			size_t core_idx = (b / snapshot->inputs[i].batch_size_per_core) % nb_cores_per_system_;
			if (b < inputs.size() && inputs[b][i].get_virtual_address())
			{
				if (!is_arm_ops_needed(inputs[b][i]))
				{
					if (arch == FpgaArchitecture::AIEML_V1C)
						nbuf[0][snapshot->inputs[i].nbuf_idx[0][b]] =
						    npu_get_nbuff_conf_val_from_phy(inputs[b][i].get_physical_address());
					// Else, if this sample sits at the beginning of the batch of a new core then configure
					// nbuf reg
					else if (b % snapshot->inputs[i].batch_size_per_core == 0)
					{
						nbuf[sys_idx][snapshot->inputs[i].nbuf_idx[sys_idx][core_idx]] =
						    npu_get_nbuff_conf_val_from_phy(inputs[b][i].get_physical_address());
					}
				}
				else
				{
					skip_upload = false;

					struct addr ddr_addr;
					if (arch == FpgaArchitecture::AIEML_V1C)
						ddr_addr = node.input_ddr_addrs[tid][0][0][i][b];
					else
						ddr_addr = node.input_ddr_addrs[tid][sys_idx][core_idx][i]
						                               [b % snapshot->inputs[i].batch_size_per_core];

					err = execute_arm_ops_in(
					    node, arm_ops_.at(inputs[b][i].get_info().name), inputs[b][i], i, tid, ddr_addr);
					if (err)
						return err;
				}
			}
		}
	}
	vcd_event(vcd_context, NPU_UPLOAD, 0);

	if (!skip_upload)
	{
		err = stats_->stop_step(node.hash, EmbeddedStats::UPLOAD);
		if (err)
			return err;
	}

	bool skip_download = true;
	if (npu_has_pl(snapshot))
	{
		for (auto& pl : snapshot->pl)
			for (auto& [idx, addr] : pl.second.pl_nbuf_addr)
				nbuf[0][idx] = addr;

		for (size_t b = 0; b < batchSize_; b++)
			for (size_t i = 0; i < outputs[b].size(); i++)
				if (b < outputs.size() && outputs[b][i].get_virtual_address())
				{
					const std::string& out_name = outputs[b][i].get_info().name;

					err = npu_set_pl_info(snapshot, out_name);
					if (err)
						return err;

					err = npu_set_pl_addr(snapshot, out_name, b, outputs[b][i].get_virtual_address());
					if (err)
						return err;
				}
				else
				{
					err = npu_set_pl_addr(snapshot, outputs[0][i].get_info().name, b, nullptr);
					if (err)
						return err;
				}
	}
	else
	{
		/* Output initialization */
		for (size_t b = 0; b < batchSize_; b++)
		{
			for (size_t i = 0; i < npu_get_nboutputs(snapshot); i++)
			{
				size_t sys_idx  = b / (snapshot->outputs[i].batch_size_per_core * nb_cores_per_system_);
				size_t core_idx = (b / snapshot->outputs[i].batch_size_per_core) % nb_cores_per_system_;
				if (b < outputs.size() && outputs[b][i].get_virtual_address())
				{
					if (!is_arm_ops_needed(outputs[b][i]))
					{
						if (arch == FpgaArchitecture::AIEML_V1C)
							nbuf[0][snapshot->outputs[i].nbuf_idx[0][b]] =
							    npu_get_nbuff_conf_val_from_phy(outputs[b][i].get_physical_address());
						else if (b % snapshot->outputs[i].batch_size_per_core == 0)
						{
							nbuf[sys_idx][snapshot->outputs[i].nbuf_idx[sys_idx][core_idx]] =
							    npu_get_nbuff_conf_val_from_phy(outputs[b][i].get_physical_address());
						}
					}
					else
						skip_download = false;
				}
			}
		}
	}

	vcd_event(vcd_context, NPU_INFERENCE_WAIT_MUTEX, 1);
	npu_get_inference_mutex(snapshot->ip_idx);
	vcd_event(vcd_context, NPU_INFERENCE_WAIT_MUTEX, 0);

	vcd_event(vcd_context, NPU_SET_NBUFF_ADDR, 1);
	err = npu_set_nbuff_addr(snapshot, nbuf);
	vcd_event(vcd_context, NPU_SET_NBUFF_ADDR, 0);
	if (err)
		return err;

	err = stats_->start_step(node.hash, EmbeddedStats::INFERENCE);
	if (err)
		return err;

	vcd_event(vcd_context, NPU_INFERENCE_START, 1);
	err = npu_start_inference(snapshot, vcd_context);
	vcd_event(vcd_context, NPU_INFERENCE_START, 0);
	if (err)
		return err;

	vcd_event(vcd_context, NPU_INFERENCE_WAIT, 1);
	err = npu_wait_for_inference(snapshot);
	vcd_event(vcd_context, NPU_INFERENCE_WAIT, 0);
	if (err)
		return npu_cleanup_inference_operation(snapshot->ip_idx, err);

	struct timespec precise_time;
	err = npu_read_preciseTimer(snapshot->ip_idx, &precise_time);
	if (err)
		return err;

	err = stats_->set_step(node.hash, EmbeddedStats::INFERENCE, precise_time);
	if (err)
		return err;

	npu_release_inference_mutex(snapshot->ip_idx);

	err = stats_->start_step(node.hash, EmbeddedStats::DOWNLOAD);
	if (err)
		return err;

	if (npu_has_pl(snapshot))
	{
		for (size_t b = 0; b < outputs.size(); b++)
			for (size_t i = 0; i < outputs[b].size(); i++)
			{
				if (outputs[b][i].get_physical_address() == 0)
				{
					const auto& pl_info = snapshot->pl.at(outputs[b][i].get_info().name);
					int         err     = npu_read_ddr(pl_info.pl_ddr_addrs[b],
                                           (uint8_t*)outputs[b][i].get_virtual_address(),
                                           pl_info.pl_buff_nbytes);
					if (err)
						return err;

					skip_download = false;
				}
			}
	}
	else
	{
		if (!skip_download)
		{
			err = stats_->start_step(node.hash, EmbeddedStats::DOWNLOAD);
			if (err)
				return err;

			vcd_event(vcd_context, NPU_DOWNLOAD, 1);
			for (size_t b = 0; b < batchSize_; b++)
			{
				for (size_t i = 0; i < npu_get_nboutputs(snapshot); i++)
				{
					size_t sys_idx  = b / (snapshot->outputs[i].batch_size_per_core * nb_cores_per_system_);
					size_t core_idx = (b / snapshot->outputs[i].batch_size_per_core) % nb_cores_per_system_;

					if (b < outputs.size() && outputs[b][i].get_virtual_address()
					    && is_arm_ops_needed(outputs[b][i]))
					{
						struct addr ddr_addr;
						if (arch == FpgaArchitecture::AIEML_V1C)
							ddr_addr = node.output_ddr_addrs[tid][0][0][i][b];
						else
							ddr_addr = node.output_ddr_addrs[tid][sys_idx][core_idx][i]
							                                [b % snapshot->outputs[i].batch_size_per_core];

						err = execute_arm_ops_out(node,
						                          arm_ops_.at(outputs[b][i].get_info().name),
						                          outputs[b][i],
						                          i,
						                          tid,
						                          ddr_addr);
						if (err)
							return err;
					}
				}
			}
			vcd_event(vcd_context, NPU_DOWNLOAD, 0);
		}
	}

	if (!skip_download)
	{
		err = stats_->stop_step(node.hash, EmbeddedStats::DOWNLOAD);
		if (err)
			return err;
	}

	vcd_event(vcd_context, NPU_EXECUTE, 0);
	return stats_->stop_step(node.hash, EmbeddedStats::VART_FULL);
}

int VartMLRunner::execute_onnx(const struct node_desc&                    node,
                               const std::vector<std::vector<NpuTensor>>& inputs,
                               std::vector<std::vector<NpuTensor>>&       outputs,
                               size_t                                     tid)
{
	struct vcd_context vcd_context = { vcd_id_, tid };

	vcd_event(vcd_context, ONNX_RUN, 1);
	stats_->startSubGraph(node.hash);

	std::vector<void*> onnx_inputs(node.input_count);
	std::vector<void*> onnx_outputs(node.output_count);

	std::vector<const NpuTensorPriv*> input_tensor_priv(node.input_count);
	std::vector<const NpuTensorPriv*> output_tensor_priv(node.output_count);

	for (size_t i = 0; i < node.input_count; i++)
	{
		input_tensor_priv[i] = NpuTensorPrivAccess::get_impl(inputs[0][i]);

		if (input_tensor_priv[i]->is_packed_)
		{
			size_t buf_size = inputs[0][i].get_info().size_in_bytes * batchSize_;

			// It is faster to copy the batch from DDR than letting OnnxRuntime access it.
			// XXX: This should be fixed when there will be cacheable area for IO.
			if (inputs[0][i].get_memory_type() == MemoryType::XRT_BO)
			{
				onnx_inputs[i] = node.onnx_staging_inputs[tid][i];
				memcpy((uint8_t*)onnx_inputs[i], (uint8_t*)inputs[0][i].get_virtual_address(), buf_size);
			}
			else
				onnx_inputs[i] = (void*)inputs[0][i].get_virtual_address();

			if (check_user_config("debug.dump_IOs"))
			{
				char in_out[] = "in";
				dump_data(node.name, in_out, i, onnx_inputs[i], buf_size);
			}
		}
		else
		{
			size_t data_type_size = get_data_type_size(inputs[0][i].get_info().data_type);
			size_t buf_size       = inputs[0][i].get_info().size * data_type_size;

			onnx_inputs[i] = node.onnx_staging_inputs[tid][i];

			for (size_t b = 0; b < inputs.size(); b++)
			{
				// This is done to avoid unaligned accesses in DDR.
				if (inputs[b][i].get_memory_type() == MemoryType::XRT_BO && !npu_is_xrt_en())
				{
					size_t size_in_bytes = inputs[b][i].get_info().size_in_bytes;
					void*  tmp           = node.onnx_tmp_inputs[tid][i];

					memcpy(tmp, (uint8_t*)inputs[b][i].get_virtual_address(), size_in_bytes);
					memcpy((uint8_t*)onnx_inputs[i] + b * buf_size, tmp, buf_size);
				}
				else
				{
					memcpy((uint8_t*)onnx_inputs[i] + b * buf_size,
					       inputs[b][i].get_virtual_address(),
					       buf_size);
				}
			}

			if (check_user_config("debug.dump_IOs"))
			{
				char in_out[] = "in";
				dump_data(node.name, in_out, i, onnx_inputs[i], buf_size * batchSize_);
			}
		}
	}

	// If there are output buffers in DDR, use a staging buffer. This is done to avoid unaligned accesses in
	// DDR.
	// XXX: This should be fixed when there will be cacheable area for IO.
	for (size_t i = 0; i < node.output_count; i++)
	{
		output_tensor_priv[i] = NpuTensorPrivAccess::get_impl(outputs[0][i]);

		if (output_tensor_priv[i]->is_packed_)
		{
			// It is faster to copy the batch from DDR than letting OnnxRuntime access it.
			// XXX: This should be fixed when there will be cacheable area for IO.
			if (outputs[0][i].get_memory_type() == MemoryType::XRT_BO)
				onnx_outputs[i] = node.onnx_staging_outputs[tid][i];
			else
				onnx_outputs[i] = outputs[0][i].get_virtual_address();
		}
		else
			onnx_outputs[i] = node.onnx_staging_outputs[tid][i];
	}

	// Build inputs' shapes while casting to correct type.
	int64_t** input_shapes = nullptr;
	if (((struct onnx_context_node*)node.context_ptr)->variable_dim_in)
	{
		input_shapes = new int64_t*[node.input_count];
		for (size_t i = 0; i < node.input_count; i++)
		{
			std::vector<uint> input_tensor_shape =
			    get_tensor_info_by_name(node.input_tensors_name[i], TensorType::CPU).shape;

			input_shapes[i] = new int64_t[input_tensor_shape.size()];
			for (size_t d = 0; d < input_tensor_shape.size(); d++)
				input_shapes[i][d] = input_tensor_shape[d];
		}
	}

	// Build outputs' shapes while casting to correct type.
	int64_t** output_shapes = nullptr;
	if (((struct onnx_context_node*)node.context_ptr)->variable_dim_out)
	{
		output_shapes = new int64_t*[node.output_count];
		for (size_t o = 0; o < node.output_count; o++)
		{
			std::vector<uint> output_tensor_shape =
			    get_tensor_info_by_name(node.output_tensors_name[o], TensorType::CPU).shape;

			output_shapes[o] = new int64_t[output_tensor_shape.size()];
			for (size_t d = 0; d < output_tensor_shape.size(); d++)
				output_shapes[o][d] = output_tensor_shape[d];
		}
	}

	int err = onnx_run_inference(*(struct onnx_context_node*)node.context_ptr,
	                             ((struct onnx_context_global*)onnx_context_global_)->memory_info,
	                             nullptr,
	                             (const int64_t**)input_shapes,
	                             onnx_inputs.data(),
	                             nullptr,
	                             (const int64_t**)output_shapes,
	                             onnx_outputs.data());

	// Copy back data from staging buffers to output tensors.
	for (size_t i = 0; i < node.output_count; i++)
	{
		if (output_tensor_priv[i]->is_packed_ && outputs[0][i].get_memory_type() == MemoryType::XRT_BO)
		{
			// It is faster to copy the batch from DDR than letting OnnxRuntime access it.
			// XXX: This should be fixed when there will be cacheable area for IO.
			size_t buf_size = outputs[0][i].get_info().size_in_bytes * batchSize_;

			memcpy((uint8_t*)outputs[0][i].get_virtual_address(), (uint8_t*)onnx_outputs[i], buf_size);

			if (check_user_config("debug.dump_IOs"))
			{
				char in_out[] = "out";
				dump_data(node.name, in_out, i, onnx_outputs[i], buf_size);
			}
		}
		else if (!output_tensor_priv[i]->is_packed_)
		{
			size_t data_type_size = get_data_type_size(outputs[0][i].get_info().data_type);
			size_t buf_size       = outputs[0][i].get_info().size * data_type_size;

			for (size_t b = 0; b < outputs.size(); b++)
			{
				if (outputs[b][i].get_memory_type() == MemoryType::XRT_BO && !npu_is_xrt_en())
				{
					size_t size_in_bytes = outputs[b][i].get_info().size_in_bytes;
					void*  tmp           = node.onnx_tmp_outputs[tid][i];

					memcpy(tmp, (uint8_t*)onnx_outputs[i] + b * buf_size, buf_size);
					memcpy((uint8_t*)outputs[b][i].get_virtual_address(), tmp, size_in_bytes);
				}
				else
					memcpy(outputs[b][i].get_virtual_address(),
					       (uint8_t*)onnx_outputs[i] + b * buf_size,
					       buf_size);
			}

			if (check_user_config("debug.dump_IOs"))
			{
				char in_out[] = "out";
				dump_data(node.name, in_out, i, onnx_outputs[i], buf_size * batchSize_);
			}
		}
		else if (check_user_config("debug.dump_IOs"))
		{
			size_t buf_size = outputs[0][i].get_info().size_in_bytes * batchSize_;

			char in_out[] = "out";
			dump_data(node.name, in_out, i, onnx_outputs[i], buf_size);
		}
	}

	if (input_shapes != nullptr)
	{
		for (size_t i = 0; i < node.input_count; i++)
			delete[] input_shapes[i];
		delete[] input_shapes;
	}
	if (output_shapes != nullptr)
	{
		for (size_t i = 0; i < node.output_count; i++)
			delete[] output_shapes[i];
		delete[] output_shapes;
	}

	stats_->stopSubGraph(node.hash);
	vcd_event(vcd_context, ONNX_RUN, 0);

	return err;
}

int VartMLRunner::allocate_job_id(ExecuteAsyncCallback cb)
{
	std::lock_guard<std::mutex> lock(mtx_for_slots_);

	auto job_id                = slots_.empty() ? 1 : slots_.rbegin()->first + 1;
	slots_[job_id]             = std::make_unique<job_slot_t>();
	slots_[job_id]->cb         = cb;
	slots_[job_id]->is_pending = true;
	return job_id;
}

VartMLRunner::job_slot_t* VartMLRunner::find_job_slot(int jobid)
{
	std::lock_guard<std::mutex> lock(mtx_for_slots_);

	if (jobid == 0)
	{
		vart_ml_log_err_msg(vart_ml_error::SYSTEM_ERROR_JOB_NOT_FOUND,
		                    "Invalid job_id: 0 is not a valid job identifier");
		return nullptr;
	}

	auto job = slots_.find(jobid);
	if (job == slots_.end())
	{
		vart_ml_log_err_msg(
		    vart_ml_error::SYSTEM_ERROR_JOB_NOT_FOUND, "No job found for job_id = %d.\n", jobid);
		return nullptr;
	}

	return job->second.get();
}

void VartMLRunner::delete_job_slot(int jobid)
{
	std::lock_guard<std::mutex> lock(mtx_for_slots_);
	slots_.erase(jobid);
}

void VartMLRunner::notify_completion(JobHandle job_handle)
{
	std::lock_guard<std::mutex> lock(mtx_for_slots_);

	if (slots_.contains(job_handle.job_id))
	{
		slots_[job_handle.job_id]->promise.set_value(job_handle.status);
		slots_[job_handle.job_id]->is_pending = false;

		if (slots_[job_handle.job_id]->cb)
			slots_[job_handle.job_id]->cb(job_handle);
	}
}

} // namespace vart
