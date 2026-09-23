/**
 * @file stats.h
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

#ifndef STATS_H
#define STATS_H

#include <optional>
#include <string>
#include <time.h>

#include "reporting.h"

/**
 * @brief Statistic computation and management class.
 *
 */
struct EmbeddedStats
{
  public:
	enum step_t
	{
		UNDEFINED,
		QUANTIZE,
		REORDER_IN,
		UPLOAD,
		INFERENCE,
		DOWNLOAD,
		REORDER_OUT,
		DEQUANTIZE,
		VART_FULL,
		FRAMEWORK,
		WHOLEGRAPH
	};

  public:
	/**
	 * @brief Construct a new Embedded Stats object
	 *
	 */
	EmbeddedStats();

	/**
	 * @brief Destroy the Embedded Stats object
	 *
	 */
	~EmbeddedStats();

	/**
	 * @brief Destroy the Embedded Stats object without printing
	 *
	 */
	void delete_stats_object();

	/**
	 * @brief Return the hash of the Stats object.
	 *
	 */
	int get_hash(void);

	/**
	 * @brief set first step and last step to whole graph step.
	 *
	 */
	void use_whole_graph(void);

	/**
	 * @brief Upade the stats class with the networks name
	 *
	 * @param networkName_ Networks name.
	 * @param nbNetworks_ Number of networks.
	 */
	void update_network(const std::string& networkName_, size_t nbNetworks_);

	/**
	 * @brief Delete graph from reporting.
	 * @return int The Vart ML error (vart_ml_error_id) code of the operation.
	 */
	int deleteGraph(void);

	/**
	 * @brief Set the current step.
	 *
	 * @param hash_ Hash of the subgraph.
	 * @param step_ Current step.
	 * @return int The Vart ML error (vart_ml_error_id) code of the operation.
	 */
	int start_step(int hash_, EmbeddedStats::step_t step_);

	/**
	 * @brief Stop the current step and register the time used for it.
	 *
	 * @param hash_ Hash of the subgraph.
	 * @param step_ Current step.
	 * @return int The Vart ML error (vart_ml_error_id) code of the operation.
	 */
	int stop_step(int hash_, EmbeddedStats::step_t step_);

	/**
	 * @brief Stop the current step and set the time used for it.
	 *
	 * @param hash_ Hash of the subgraph.
	 * @param step_ Current step.
	 * @param time_ Time used for the current step.
	 * @return int The Vart ML error (vart_ml_error_id) code of the operation.
	 */
	int set_step(int hash_, EmbeddedStats::step_t step_, struct timespec time_);

	/**
	 * @brief Update the current step time. Can be called multiple time.
	 *
	 * @param hash_ Hash of the subgraph.
	 * @param step_ Current step.
	 * @return int The Vart ML error (vart_ml_error_id) code of the operation.
	 */
	int update_step(int hash_, EmbeddedStats::step_t step_);

	/**
	 * @brief Set the batch size of the current run.
	 *
	 * If this is the first run the graph batch size will be updated as well.
	 *
	 * @param batch_size Batch size of the current run.
	 */
	void setBatchSize(size_t batch_size);

	/**
	 * @brief Set the input shape of a subgraph.
	 *
	 * @param hash_ Hash of the subgraph.
	 * @param inputShape_ Shape of the input of the subgraph.
	 */
	void setInputDims(int hash_, const std::vector<std::vector<uint>>& inputShape_);

	/**
	 * @brief Set the output shape of a subgraph.
	 *
	 * @param hash_ Hash of the subgraph.
	 * @param outputShape_ Shape of the output of the subgraph.
	 */
	void setOutputDims(int hash_, const std::vector<std::vector<uint>>& outputShape_);

	/**
	 * @brief Set the input shape of a subgraph.
	 *
	 * @param hash_ Hash of the subgraph.
	 * @param inputDataType_ Data types of the inputs of the subgraph.
	 */
	void setInputDataType(int hash_, const std::vector<std::string>& inputDataType_);

	/**
	 * @brief Set the input shape of a subgraph.
	 *
	 * @param hash_ Hash of the subgraph.
	 * @param outputDataType_ Data types of the outputs of the subgraph.
	 */
	void setOutputDataType(int hash_, const std::vector<std::string>& outputDataType_);

	/**
	 * @brief Check if the first run has started.
	 *
	 * @param hash_ Hash of the subgraph.
	 */
	bool isFirstRun(void);

	/**
	 * @brief Add a subgraph information the the class.
	 *
	 * @param hash_ Hash of the subgraph.
	 * @param name_ Name of the subgraph.
	 * @param onnxSubGraph_ True if the subgraph is an ONNX one, otherwise it is a NPU one.
	 */
	void addSubGraph(int hash_, const std::string& name_, bool onnxSubGraph_);

	/**
	 * @brief Set this subgraph as the first subgraph of the run.
	 *
	 * @param hash_ Hash of the subgraph.
	 */
	void setAsFirst(int hash_);

	/**
	 * @brief Set this subgraph as the last subgraph of the run.
	 *
	 * @param hash_ Hash of the subgraph.
	 */
	void setAsLast(int hash_);

	/**
	 * @brief Set the configuration string of the IP used for this run.
	 *
	 */
	void set_config_string(const std::string& config_str);

	/**
	 * @brief Start the subgraph measurement.
	 *
	 * @param hash_ Hash of the subgraph.
	 */
	void startSubGraph(int hash_);

	/**
	 * @brief Stop the subgraph measurement.
	 *
	 * @param hash_ Hash of the subgraph.
	 */
	void stopSubGraph(int hash_);

	/**
	 * @brief Display the summary at the end of a run.
	 *
	 */
	void print_summary(void);

  private:
	struct SnapshotInfos
	{
		struct timespec timeStart       = {};
		struct timespec timeStop        = {};
		struct timespec startGraph      = {};
		struct timespec endGraph        = {};
		struct timespec startVartML     = {};
		struct timespec endVartML       = {};
		struct timespec startQuantize   = {};
		struct timespec endQuantize     = {};
		struct timespec startReorderIn  = {};
		struct timespec endReorderIn    = {};
		struct timespec startWrite      = {};
		struct timespec endWrite        = {};
		struct timespec startRun        = {};
		struct timespec endRun          = {};
		struct timespec startRead       = {};
		struct timespec endRead         = {};
		struct timespec startReorderOut = {};
		struct timespec endReorderOut   = {};
		struct timespec startDequantize = {};
		struct timespec endDequantize   = {};
		struct timespec startFramework  = {};
		struct timespec endFramework    = {};
	};

	struct DeltaTimes
	{
		struct Npu
		{
			float full        = 0;
			float preProcess  = 0;
			float reorderIn   = 0;
			float upload      = 0;
			float runFpga     = 0;
			float download    = 0;
			float reorderOut  = 0;
			float postProcess = 0;
			float cpu         = 0;
		};

		struct Framework
		{
			float cpu = 0;
		};

		float     wholeGraph = 0;
		Npu       npu;
		Framework framework;
	};

	struct PerGraphInfo
	{
		int         hash;
		std::string name;

		std::vector<std::vector<uint>> inputShape;
		std::vector<std::vector<uint>> outputShape;

		std::vector<std::string> inputDataType  = {};
		std::vector<std::string> outputDataType = {};

		size_t batchSize     = (size_t)-1;
		bool   runOnCpu      = false;
		bool   firstGraph    = false;
		bool   lastGraph     = false;
		step_t previous_step = EmbeddedStats::UNDEFINED;

		struct SnapshotInfos    curr_run;
		std::vector<DeltaTimes> times;
		size_t                  computedTimes = 0;
		std::string             config_str;
	};

	struct ParentGraph
	{
		std::string               networkName;
		PerGraphInfo              info;
		int                       nbNpuSubGraphs;
		int                       nbOnnxSubGraphs;
		std::vector<PerGraphInfo> subGraphs;
	};

  private:
	/**
	 * @brief Return the information from a graph or subgraph.
	 *
	 * @param hash_ Hash of the graph.
	 * @return PerGraphInfo* Information structure of the graph.
	 */
	PerGraphInfo* findPerGraphInfo(int hash_);

	/**
	 * @brief Convert steps absolut time into timing information for one inference.
	 *
	 * @param perGraphInfo_ Inofrmation of the graph.
	 * @param snapshotInfos_ Time information of all steps of the run.
	 * @return DeltaTimes Timing informations.
	 */
	DeltaTimes snapshotToDelta(PerGraphInfo* perGraphInfo_, const SnapshotInfos& snapshotInfos_);

	/**
	 * @brief Initialize the stats class
	 *
	 * @param hash_ Hash of the main graph.
	 * @return int The Vart ML error (vart_ml_error_id) code of the operation.
	 */
	int init(int hash_);

	/**
	 * @brief Update the run information before the inference.
	 *
	 * @param hash_ Hash of the graph.
	 * @param batchSize_ Number of image in the batch.
	 */
	void beforeForward(int hash_, size_t batchSize_);

	/**
	 * @brief Get the information for the considered graph.
	 *
	 * @param perGraphInfo_
	 */
	void initAfterForward(PerGraphInfo* perGraphInfo_);

	/**
	 * @brief Update the run information after the inference.
	 *
	 * @param hash_ Hash of the graph.
	 * @param infos_ Time information of all steps of the run.
	 */
	void afterForward(int hash_, const SnapshotInfos& infos_);

	/**
	 * @brief Compute the timing informations from the absolute time of all steps.
	 *
	 * @param deltaTimes_ Computed timing informations.
	 */
	void computeWholeGraph(DeltaTimes& deltaTimes_);

	/**
	 * @brief Add the computed timing information to the reporting Json.
	 *
	 * @param deltaTimes_ Computed timing informations.
	 */
	void addJsonObject(const DeltaTimes& deltaTimes_);

	/**
	 * @brief Add the summary of the run to the reporting Json.
	 *
	 * @param deltaTimes_ Computed timing informations.
	 */
	void update_summary(const std::optional<DeltaTimes>);

	/**
	 * @brief Print the summary of the run.
	 *
	 */
	void summary();

  private:
	ParentGraph _graph{};

	bool               _initDone     = false;
	bool               _printed      = false;
	std::string        _statsView    = "none";
	std::string        _log_libLevel = "none";
	long long          _uploadCnt    = -1;
	unsigned long long _downloadCnt  = 0;
	unsigned long long _nbImages     = 0;
	bool               _quantize     = false;
	bool               _reorderIn    = false;
	bool               _upload       = false;
	bool               _download     = false;
	bool               _reorderOut   = false;
	bool               _dequantize   = false;
	bool               _vart_ml_full = false;
	bool               _wholeGraph   = false;

	EmbeddedReporting*    _reporting;
	ordered_json          _jsonRoot;
	ordered_json::array_t _batchArray;
	ordered_json          _summary;

	size_t batch_size;
	step_t first_step;
	step_t last_step;
};

#endif
