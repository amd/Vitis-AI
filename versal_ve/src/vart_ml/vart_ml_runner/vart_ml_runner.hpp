/**
 * @file vart_ml_runner.hpp
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

#pragma once

#include <future>
#include <map>
#include <mutex>

#include "utils/log.h"
#include "utils/stats.h"
#include "vart_runner_factory.hpp"

#define T_POOL_MIN_THREADS_COUNT 1

namespace vart
{
/**
 * @brief Enum that list the different execution modes for a graph's node.
 */
enum class NodeExecutionMode
{
	NPU,
	ONNX,
};

/**
 * @brief Enum that list the different ARM operations.
 */
enum class ArmOps
{
	CAST,
	DEQUANTIZE,
	PAD,
	QUANTIZE,
	RESHAPE,
	SLICE,
	TRANSPOSE
};

/**
 * @brief Enum that list the different ways to link a node's external tensors.
 */
enum class NodeExtLinkMode
{
	DIRECT,    /**< Set node's buffers array to external one. */
	COPY_PTRS, /**< Copy external buffers pointers to node's buffer array. */
};

/**
 * @brief Structure of an Arm operation.
 *
 * @details This structure contains all the parameters needed to define an ARM operation.
 */
struct arm_ops
{
	std::string                     name;
	ArmOps                          type;
	std::map<std::string, std::any> parameter;
	/* Input infos */
	std::string           input_name;
	MemoryLayout          input_memory_layout;
	std::vector<uint32_t> input_shape;
	std::vector<uint32_t> input_strides;
	size_t                input_size;
	DataType              input_data_type;
	/* Output infos */
	std::string           output_name;
	MemoryLayout          output_memory_layout;
	std::vector<uint32_t> output_shape;
	std::vector<uint32_t> output_strides;
	size_t                output_size;
	size_t                output_size_in_bytes;
	DataType              output_data_type;
};

/**
 * @brief Structure of a shapshot graph's node.
 *
 * @details This structure contains all the parameters needed to define a snapshot graph's node.
 */
struct node_desc
{
	std::string              name;                /**< Name of the node. */
	unsigned                 hash;                /**< Hash of the node (for stats). */
	NodeExecutionMode        execution_mode;      /**< Execution mode of the node. */
	size_t                   input_count;         /**< Number of node's inputs. */
	size_t                   output_count;        /**< Number of node's outputs. */
	std::vector<std::string> input_tensors_name;  /**< Vector of input tensors. */
	std::vector<std::string> output_tensors_name; /**< Vector of output tensors. */
	NodeExtLinkMode link_mode_in;  /* Link mode for the potential external input tensors of the node. */
	NodeExtLinkMode link_mode_out; /* Link mode for the potential external output tensors of the node. */
	void*           context_ptr;   /**< Pointer to an execution mode specific context. Points to
	                                    a npu_snapshot_t for NPU nodes. Points to a
	                                    struct onnx_context_node for ONNX nodes. */
	bool pl_node;                  /* The ouput of the node is linked to a pl module */

	// [nb_threads][nb_sys][nb_input/nb_output (V1 and V2) or nbuf_size (AIEML_V1C)]
	std::vector<std::vector<std::vector<uint64_t>>> nbuf;
	// [nb threads][nb_sys][nb_cores][nb_inputs/outputs][batch_size]
	std::vector<std::vector<std::vector<std::vector<std::vector<struct addr>>>>> input_ddr_addrs;
	std::vector<std::vector<std::vector<std::vector<std::vector<struct addr>>>>> output_ddr_addrs;
	/* ONNX-only: per-thread staging buffers [ctx_id][tensor_idx].
	 * staging: full-batch buffer (size_in_bytes * batchSize_) passed to onnxruntime.
	 * tmp:     one-image bounce buffer for the non-packed XRT_BO unaligned-access workaround. */
	std::vector<std::vector<void*>> onnx_staging_inputs;
	std::vector<std::vector<void*>> onnx_staging_outputs;
	std::vector<std::vector<void*>> onnx_tmp_inputs;
	std::vector<std::vector<void*>> onnx_tmp_outputs;
};

static inline void check_err(int err)
{
	if (err)
		throw std::runtime_error(vart_ml_error::exception_message(err));
}

static inline StatusCode to_status_code(int err)
{
	using E = vart_ml_error::vart_ml_error_id;
	switch (static_cast<E>(err))
	{
	case E::SUCCESS:
		return StatusCode::SUCCESS;
	case E::SYSTEM_ERROR_MEM_ALLOC_FAILURE:
	case E::DEVICE_DDR_MALLOC_FAILURE:
		return StatusCode::OUT_OF_MEMORY;
	case E::DEVICE_INFERENCE_TIMEOUT:
	case E::DEVICE_DDR_READ_TIMEOUT:
	case E::DEVICE_READ_TIMEOUT:
	case E::DEVICE_MMCM_CONFIG_TIMEOUT:
		return StatusCode::RUNTIME_ERROR;
	case E::SYSTEM_ERROR_JOB_NOT_FOUND:
		return StatusCode::INVALID_JOB_ID;
	case E::SYSTEM_ERROR_INVALID_ARG:
	case E::SYSTEM_ERROR_INVALID_PATH:
	case E::CONFIG_BAD_ARGUMENT:
	case E::CONFIG_UNKNOWN_SHAPE_TYPE:
	case E::CONFIG_UNSUPPORTED_DIMENSION:
	case E::CONFIG_UNSUPPORTED_NPU_TENSOR_INFO:
	case E::CONFIG_INVALID_QUANTIZATION_TYPE:
	case E::CONFIG_IRIZ_MALFORMED:
	case E::CONFIG_SNAPSHOT_MALFORMED:
	case E::CONFIG_UNSUPPORTED_SNAPSHOT_LAYOUT:
	case E::DEVICE_ARG_NOT_FOUND_IN_DEV_TREE:
	case E::DEVICE_BAD_ARG:
	case E::DEVICE_BAD_ADDR_ALIGNMENT:
	case E::TOOLS_BAD_ARG:
	case E::TOOLS_BAD_USAGE:
		return StatusCode::INVALID_INPUT;
	case E::LICENSE_INVALID:
	case E::LIBRARY_MISSING:
	case E::LIBRARY_IO_MISSING:
	case E::CONFIG_MISSING_PERIPHERALS:
	case E::CONFIG_MISSING_FPGA_INFO:
	case E::DEVICE_DEV_ACCESS_FAILURE:
		return StatusCode::RESOURCE_UNAVAILABLE;
	case E::CONFIG_UNSUPPORTED_FEATURE:
	case E::DEVICE_UNSUPPORTED_FEATURE:
	case E::DEVICE_UNSUPPORTED_ARCH:
		return StatusCode::UNSUPPORTED;
	case E::DEVICE_JOB_EXCEPTION:
		return StatusCode::RUNTIME_ERROR;
	default:
		return StatusCode::FAILURE;
	}
}

/**
 * @name Utils
 *
 * These are utility functions to convert vart enums into string and vice versa.
 *
 */
//@{
DataType           to_data_type(std::string data_type_str);
MemoryLayout       to_memory_layout(std::string memory_layout_str);
TensorType         to_tensor_type(std::string tensor_type_str);
MemoryType         to_memory_type(std::string memory_type_str);
TensorDirection    to_tensor_direction(std::string tensor_direction_str);
const std::string& arm_ops_to_string(ArmOps arm_op);
ArmOps             to_arm_ops(std::string arm_op_str);
size_t             get_data_type_size(DataType data_type);
//@}

/**
 * @class VartMLRunner
 * @brief Class of the Runner, provides API to use the NPU runner.
 *   The runner instance has a number of member functions to control
 *   the execution and get the inputs and outputs of the runner.
 * */
class VartMLRunner : public Runner
{
  public:
	VartMLRunner(const std::string&                               model_path,
	             const std::unordered_map<std::string, std::any>& options = {});

	~VartMLRunner();

	// Global getters.
	const std::vector<NpuTensorInfo>& get_tensors_info(TensorDirection direction,
	                                                   TensorType      type) const override;
	const NpuTensorInfo&              get_tensor_info_by_name(const std::string& tensor_name,
	                                                          TensorType         type) const override;
	const QuantParameters&            get_quant_parameters(const std::string& tensor_name) const override;
	size_t                            get_num_input_tensors(void) const override;
	size_t                            get_num_output_tensors(void) const override;
	size_t                            get_batch_size(void) const override;
	size_t                            get_batch_size(TensorDirection direction) const override;

	// Execution.
	StatusCode execute(const std::vector<std::vector<NpuTensor>>& inputs,
	                   std::vector<std::vector<NpuTensor>>&       outputs) noexcept override;
	JobHandle  execute_async(const std::vector<std::vector<NpuTensor>>& inputs,
	                         std::vector<std::vector<NpuTensor>>&       outputs) noexcept override;
	JobHandle  execute_async(const std::vector<std::vector<NpuTensor>>& inputs,
	                         std::vector<std::vector<NpuTensor>>&       outputs,
	                         ExecuteAsyncCallback                       cb) noexcept override;
	StatusCode wait(const JobHandle& job_handle, std::chrono::milliseconds timeout) noexcept override;

	// Tensor allocation
	NpuTensor allocate_npu_tensor(const NpuTensorInfo& info) const override;
	NpuTensor allocate_npu_tensor(const NpuTensorInfo& info, uint32_t memory_bank) const override;
	NpuTensor
	allocate_sub_tensor(const NpuTensor& parent, const NpuTensorInfo& info, size_t offset) const override;
	// Utils.
	std::any       get_property(const std::string& key) const override;
	NpuTensorInfo& get_tensor_info_by_name(const std::string& tensor_name, TensorType type);
	void*          malloc_buffer(uint64_t size, uint8_t ddr = 0) const;
	void*          malloc_sub(void* parent_vaddr, size_t offset, size_t size) const;
	void           free_buffer(void* buffer_ptr) const;

  public:
	struct job_slot_t
	{
		std::promise<StatusCode> promise;
		int                      job_id;
		bool                     is_pending;
		ExecuteAsyncCallback     cb;
	};

  private:
	/*
	 * Internals.
	 */

	// Snapshot parsing.
	std::string name_translate_in(const std::string& name);
	std::string name_translate_out(const std::string& name);
	bool        is_external_input(const std::string& input_name);
	bool        is_external_output(const std::string& output_name);
	int         onnx_init(void);
	int         onnx_init_node(struct node_desc& node);
	void        build_pl_nodes(const std::vector<std::string>& output_names);
	int         set_npu_tensors(struct node_desc& node);
	void        parse_graph_tensor(const std::string& key, const void* model_info, TensorDirection direction);
	int parse_graph_node(const std::string& name, const void* model_info, std::vector<arm_ops>& arm_ops);
	int parse_graph_arm_ops(const std::string& name, const void* model_info, std::vector<arm_ops>& arm_ops);

	// Arm Ops.
	int  build_arm_op_in(struct node_desc&            node,
	                     const std::string            input_name,
	                     std::vector<struct arm_ops>& arm_ops,
	                     std::vector<struct arm_ops>& arm_ops_in,
	                     const std::string&           tensor_name);
	int  build_arm_op_out(struct node_desc&            node,
	                      const std::string            output_name,
	                      std::vector<struct arm_ops>& arm_ops,
	                      std::vector<struct arm_ops>& arm_ops_out,
	                      const std::string&           tensor_name);
	bool is_arm_ops_needed(const NpuTensor& npu_tensor);
	int
	execute_arm_ops(const struct node_desc& node, size_t tid, arm_ops& arm_op, const void* src, void* dst);
	int execute_arm_ops_in(const struct node_desc& node,
	                       std::vector<arm_ops>&   arm_ops_ref,
	                       const NpuTensor&        npu_tensor,
	                       size_t                  input_no,
	                       size_t                  tid,
	                       const struct addr&      ddr_addr);
	int execute_arm_ops_out(const struct node_desc& node,
	                        std::vector<arm_ops>&   arm_ops_ref,
	                        const NpuTensor&        npu_tensor,
	                        size_t                  output_no,
	                        size_t                  tid,
	                        const struct addr&      ddr_addr);

	// Execution.
	StatusCode execute_job(const std::vector<std::vector<NpuTensor>>& inputs,
	                       std::vector<std::vector<NpuTensor>>&       outputs,
	                       size_t                                     tid,
	                       JobHandle                                  job_handle);
	int        execute_npu(const struct node_desc&                    node,
	                       const std::vector<std::vector<NpuTensor>>& inputs,
	                       std::vector<std::vector<NpuTensor>>&       outputs,
	                       size_t                                     tid);
	int        execute_onnx(const struct node_desc&                    node,
	                        const std::vector<std::vector<NpuTensor>>& inputs,
	                        std::vector<std::vector<NpuTensor>>&       outputs,
	                        size_t                                     tid);

	// Debug utils.
	void dump_graph();
	void dump_data(const std::string& node_name, char* in_out, size_t idx, const void* ptr, size_t size);

	size_t compute_onnx_scratch_bytes(void) const;

	// Async execution handling.
	void        tpool_init(size_t nb_threads);
	void        tpool_allocate_resources(void);
	void        tpool_thread(size_t tid);
	void        tpool_fill(void);
	void        tpool_destroy(void);
	size_t      get_thread_pool_size(void) const;
	int         allocate_job_id(ExecuteAsyncCallback cb);
	job_slot_t* find_job_slot(int job_id);
	void        delete_job_slot(int job_id);
	void        notify_completion(JobHandle job_handle);

  private:
	typedef struct tpool_work
	{
		const std::vector<std::vector<NpuTensor>>& inputs;
		std::vector<std::vector<NpuTensor>>&       outputs;
		JobHandle                                  job_handle;
		struct tpool_work*                         next;
	} tpool_work_t;

	typedef struct _tpool_t
	{
		/* pool state */
		std::vector<std::thread> threads;
		size_t                   current_queue_size;
		tpool_work_t*            queue_head;
		tpool_work_t*            queue_tail;
		std::mutex               queue_lock;
		std::condition_variable  cond_var;
		bool                     shutdown;
	} tpool_t;

	enum class TensorDirectionPriv
	{
		INPUT,
		OUTPUT,
		INTERNAL
	};

	// Global.
	std::string                                                   model_name_;
	std::string                                                   model_path_;
	std::unordered_map<std::string, std::any>                     options_;
	std::map<std::string, std::string>                            arm_ops_translations_in_;
	std::map<std::string, std::string>                            arm_ops_translations_out_;
	std::map<std::string, std::string>                            npu_translations_in_;
	std::map<std::string, std::string>                            npu_translations_out_;
	size_t                                                        batchSize_;
	size_t                                                        nb_systems_;
	size_t                                                        nb_cores_per_system_;
	size_t                                                        batch_size_per_core_;
	std::vector<std::string>                                      names_in_;
	std::vector<std::string>                                      names_out_;
	std::map<unsigned, node_desc>                                 nodes_;
	std::map<std::string, std::pair<TensorDirectionPriv, size_t>> tensors_;
	std::vector<NpuTensorInfo>                                    cpu_input_tensors_;
	std::vector<NpuTensorInfo>                                    cpu_output_tensors_;
	std::vector<NpuTensorInfo>                                    hw_input_tensors_;
	std::vector<NpuTensorInfo>                                    hw_output_tensors_;
	std::vector<NpuTensorInfo>                                    cpu_internal_tensors_;
	std::vector<NpuTensorInfo>                                    hw_internal_tensors_;
	std::vector<std::map<std::string, std::vector<void*>>>        internal_buffers_;
	std::vector<void*>                                            internal_parent_buffers_;
	std::map<std::string, std::vector<arm_ops>>                   arm_ops_;
	std::map<std::string, QuantParameters>                        quant_params_;
	std::shared_ptr<tpool_t>                                      thread_pool_;
	std::once_flag                                                tpool_fill_once_;

	// PL module specific
	bool pl_ = false;

	// Onnx specific.
	void* onnx_context_global_ = NULL;

	// Misc.
	std::map<int, std::unique_ptr<job_slot_t>> slots_;
	std::mutex                                 mtx_for_slots_;
	std::unique_ptr<EmbeddedStats>             stats_;
	int8_t                                     vcd_id_;
};
} // namespace vart
