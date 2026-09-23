/**
 * @file stats.cpp
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

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <vector>

#include "fpga_info.h"
#include "log.h"
#include "stats.h"

#define accumulatePipeline(_times_, _graphInfo_, _type_)                                                     \
	_times_._type_ = std::accumulate(_graphInfo_.times.begin() + _graphInfo_.computedTimes,                  \
	                                 _graphInfo_.times.end(),                                                \
	                                 _times_._type_,                                                         \
	                                 [](float current, const auto& time) { return current + time._type_; });

static double diff_msec(const struct timespec& start, const struct timespec& end)
{
	struct timespec diff;

	if (end.tv_nsec < start.tv_nsec)
	{
		diff.tv_sec  = end.tv_sec - start.tv_sec - 1;
		diff.tv_nsec = end.tv_nsec - start.tv_nsec + 1e9;
	}
	else
	{
		diff.tv_sec  = end.tv_sec - start.tv_sec;
		diff.tv_nsec = end.tv_nsec - start.tv_nsec;
	}

	return diff.tv_sec / 1e-3 + diff.tv_nsec / 1e6;
};

static struct timespec add_time(struct timespec start, struct timespec end)
{
	struct timespec add;

	add.tv_sec  = end.tv_sec + start.tv_sec;
	add.tv_nsec = end.tv_nsec + start.tv_nsec;
	if (add.tv_nsec >= 1e9)
	{
		add.tv_sec++;
		add.tv_nsec -= 1e9;
	}

	return add;
}

static struct timespec diff_time(struct timespec start, const struct timespec end)
{
	struct timespec diff;

	diff.tv_sec  = end.tv_sec - start.tv_sec;
	diff.tv_nsec = end.tv_nsec - start.tv_nsec;
	if (diff.tv_nsec < 0)
	{
		diff.tv_sec--;
		diff.tv_nsec += 1000000000;
	}

	return diff;
}

static float roundValue(float value_)
{
	float roundedValue = (float)((int)(value_ * 1000)) / 1000;

	// do not print 0.00 to simplify stats DB in QA
	if (roundedValue < 0.01)
		roundedValue = 0.01;

	return roundedValue;
}

static size_t searchMinIndex(const std::vector<float>& data_)
{
	auto min_elmt = std::min_element(data_.begin(), data_.end());
	return std::distance(data_.begin(), min_elmt);
}

static size_t searchMaxIndex(const std::vector<float>& data_)
{
	auto max_elmt = std::max_element(data_.begin(), data_.end());
	return std::distance(data_.begin(), max_elmt);
}

static size_t searchPercentileIndex(const std::vector<float>& data_, unsigned level)
{
	size_t medianIndex = 0;

	if (not data_.empty())
	{
		std::vector<float> sortedData = data_;

		std::sort(sortedData.begin(), sortedData.end());
		size_t cut = sortedData.size() * level / 100;

		// to be optimistic
		if (sortedData.size() % 2 == 0)
			cut--;

		auto it = find(data_.begin(), data_.end(), sortedData[cut]);

		medianIndex = it - data_.begin();
	}

	return medianIndex;
}

static size_t searchMedianIndex(const std::vector<float>& data_) { return searchPercentileIndex(data_, 50); }

static std::string printMultiDims(const char*                           name_,
                                  const std::vector<std::vector<uint>>& shape_,
                                  std::vector<std::string>              dataType_ = {})
{
	std::stringstream ss;

	if (dataType_.size() < shape_.size())
		dataType_.resize(shape_.size(), "");

	if (not shape_.empty())
	{
		ss << shape_.size() << " " << name_ << ((shape_.size() > 1) ? "s" : "") << ". Tensor shape: ";

		size_t i;
		for (i = 0; i < std::min(shape_.size(), 10UL); ++i)
		{
			if (i > 0)
				ss << ", ";
			if (not shape_[i].empty())
			{
				for (size_t j = 0; j < shape_[i].size(); ++j)
					ss << ((j == 0) ? "" : "x") << shape_[i][j];
				if (dataType_[i] != "")
					ss << " (" << dataType_[i] << ")";
			}
			else
				ss << "NA";
		}

		if (i < shape_.size())
			ss << "...";
	}
	else
		ss << "";

	return ss.str();
}

int EmbeddedStats::init(int hash_)
{
	_initDone = true;
	const char* info;
	int         err;

	err = get_user_config("runSession.summary", &info);
	if (err)
		return err;

	if (info != NULL)
		_statsView = info;

	err = get_user_config("log.libLevel", &info);
	if (err)
		return err;

	if (info != NULL)
		_log_libLevel = info;

	if (_log_libLevel == "LIB_RUNSTATS")
		vart_ml_log(LOG_INFO, "[VART] [LIB_RUNSTATS] %s 0x%08x\n", __func__, hash_);

	if (_statsView != "none")
	{
		_graph.info.hash = hash_;

		_reporting = &EmbeddedReporting::getInstance();

		if (_reporting && _reporting->isEnabled())
		{
			if (_batchArray.empty())
			{
				_batchArray          = ordered_json::array();
				_jsonRoot["batches"] = _batchArray;
				_summary             = ordered_json::object();
				_jsonRoot["summary"] = _summary;
			}
		}
	}

	return vart_ml_error::SUCCESS;
}

void EmbeddedStats::update_network(const std::string& networkName_, size_t nbNetworks_)
{
	if (_log_libLevel == "LIB_RUNSTATS")
		vart_ml_log(LOG_INFO,
		            "[VART] [LIB_RUNSTATS] %s %s nbNetworks_ %zu\n",
		            __func__,
		            networkName_.c_str(),
		            nbNetworks_);

	if (_initDone == false || _statsView == "none")
		return;

	if (_graph.networkName.empty())
	{
		_graph.networkName = networkName_;

		if (_reporting && _reporting->isEnabled())
		{
			bool is_found;
			_reporting->isValueExist("RunStats network " + _graph.networkName, is_found);
			if (is_found == false)
				_reporting->addData("RunStats network " + _graph.networkName, _jsonRoot);
			else
			{
				int i = 0;
				_reporting->isValueExist("RunStats network " + _graph.networkName + "_" + std::to_string(i),
				                         is_found);
				while (is_found == true)
				{
					i++;
					_reporting->isValueExist(
					    "RunStats network " + _graph.networkName + "_" + std::to_string(i), is_found);
				}

				_reporting->addData("RunStats network " + _graph.networkName + "_" + std::to_string(i),
				                    _jsonRoot);
			}

			_reporting->isValueExist("Nb networks", is_found);
			if (is_found == false)
				_reporting->addData("Nb networks", json::number_integer_t(nbNetworks_));
			else
				_reporting->updateData("Nb networks", json::number_integer_t(nbNetworks_));
		}
	}
}

int EmbeddedStats::deleteGraph(void)
{
	if (_log_libLevel == "LIB_RUNSTATS")
		vart_ml_log(LOG_INFO, "[VART] [LIB_RUNSTATS] %s %s\n", __func__, _graph.networkName.c_str());

	if (_initDone == false || _statsView == "none")
		return vart_ml_error::SUCCESS;

	if (_reporting && _reporting->isEnabled())
	{
		bool is_found;
		int  err;
		err = _reporting->isValueExist("RunStats network " + _graph.networkName, is_found);
		if (err)
			return err;

		if (is_found == true)
		{
			err = _reporting->removeData("RunStats network " + _graph.networkName);
			if (err)
				return err;
		}
		else
		{
			int i = 0;

			err = _reporting->isValueExist("RunStats network " + _graph.networkName + "_" + std::to_string(i),
			                               is_found);
			if (err)
				return err;

			while (is_found == true)
			{
				err = _reporting->removeData("RunStats network " + _graph.networkName + "_"
				                             + std::to_string(i));
				if (err)
					return err;

				i++;

				err = _reporting->isValueExist(
				    "RunStats network " + _graph.networkName + "_" + std::to_string(i), is_found);
				if (err)
					return err;
			}
		}
	}

	return vart_ml_error::SUCCESS;
}

void EmbeddedStats::setInputDims(int hash_, const std::vector<std::vector<uint>>& inputShape_)
{
	if (inputShape_.size() && _log_libLevel == "LIB_RUNSTATS")
	{
		vart_ml_log(LOG_INFO, "[VART] [LIB_RUNSTATS] %s 0x%08x ", __func__, hash_);
		for (size_t i = 0; i < inputShape_[0].size(); i++)
			vart_ml_log(LOG_INFO, "%dx", inputShape_[0][i]);
		vart_ml_log(LOG_INFO, "\b \n");
	}

	if (_initDone == false || _statsView == "none")
		return;

	this->findPerGraphInfo(hash_)->inputShape = inputShape_;
}

void EmbeddedStats::setBatchSize(size_t batch_size)
{
	if (_log_libLevel == "LIB_RUNSTATS")
		vart_ml_log(LOG_INFO, "[VART] [LIB_RUNSTATS] %s batch size %zu\n", __func__, batch_size);

	if (_initDone == false || _statsView == "none")
		return;

	this->batch_size = batch_size;
}

void EmbeddedStats::setOutputDims(int hash_, const std::vector<std::vector<uint>>& outputShape_)
{
	if (outputShape_.size() && _log_libLevel == "LIB_RUNSTATS")
	{
		vart_ml_log(LOG_INFO, "[VART] [LIB_RUNSTATS] %s 0x%08x ", __func__, hash_);

		for (size_t i = 0; i < outputShape_[0].size(); i++)
			vart_ml_log(LOG_INFO, "%dx", outputShape_[0][i]);
		vart_ml_log(LOG_INFO, "\b \n");
	}

	if (_initDone == false || _statsView == "none")
		return;

	this->findPerGraphInfo(hash_)->outputShape = outputShape_;
}

void EmbeddedStats::setInputDataType(int hash_, const std::vector<std::string>& inputDataType_)
{
	if (inputDataType_.size() && _log_libLevel == "LIB_RUNSTATS")
	{
		vart_ml_log(LOG_INFO, "[VART] [LIB_RUNSTATS] %s 0x%08x ", __func__, hash_);
		for (size_t i = 0; i < inputDataType_.size(); i++)
			vart_ml_log(LOG_INFO, "%s, ", inputDataType_[i].c_str());
		vart_ml_log(LOG_INFO, "\b\b\n");
	}

	if (_initDone == false || _statsView == "none")
		return;

	this->findPerGraphInfo(hash_)->inputDataType = inputDataType_;
}

void EmbeddedStats::setOutputDataType(int hash_, const std::vector<std::string>& outputDataType_)
{
	if (outputDataType_.size() && _log_libLevel == "LIB_RUNSTATS")
	{
		vart_ml_log(LOG_INFO, "[VART] [LIB_RUNSTATS] %s 0x%08x ", __func__, hash_);
		for (size_t i = 0; i < outputDataType_.size(); i++)
			vart_ml_log(LOG_INFO, "%s, ", outputDataType_[i].c_str());
		vart_ml_log(LOG_INFO, "\b\b\n");
	}

	if (_initDone == false || _statsView == "none")
		return;

	this->findPerGraphInfo(hash_)->outputDataType = outputDataType_;
}

bool EmbeddedStats::isFirstRun(void)
{
	if (_log_libLevel == "LIB_RUNSTATS")
		vart_ml_log(LOG_INFO, "[VART] [LIB_RUNSTATS] %s\n", __func__);

	if (_initDone == false || _statsView == "none")
		return false;

	return _uploadCnt == -1;
}

void EmbeddedStats::addSubGraph(int hash_, const std::string& name_, bool onnxSubGraph_)
{
	if (_log_libLevel == "LIB_RUNSTATS")
		vart_ml_log(LOG_INFO,
		            "[VART] [LIB_RUNSTATS] %s 0x%08x %s onnxSubGraph_ %d\n",
		            __func__,
		            hash_,
		            name_.c_str(),
		            onnxSubGraph_);

	if (_initDone == false || _statsView == "none")
		return;

	for (size_t subGraphIndex = 0; subGraphIndex < _graph.subGraphs.size(); ++subGraphIndex)
		if (_graph.subGraphs[subGraphIndex].hash == hash_)
			return;

	PerGraphInfo graphInfo = {};

	graphInfo.hash = hash_;
	if (name_.empty())
	{
		char buf[100];
		sprintf(buf, "Sub-graph %2zu", _graph.subGraphs.size());
		graphInfo.name = buf;
	}
	else
		graphInfo.name = name_;
	graphInfo.runOnCpu = onnxSubGraph_;
	if (not onnxSubGraph_)
	{
		graphInfo.firstGraph = true;
		graphInfo.lastGraph  = true;
	}

	_graph.subGraphs.push_back(graphInfo);

	if (onnxSubGraph_)
		_graph.nbOnnxSubGraphs++;
	else
		_graph.nbNpuSubGraphs++;
}

void EmbeddedStats::setAsFirst(int hash_)
{
	if (_log_libLevel == "LIB_RUNSTATS")
		vart_ml_log(LOG_INFO, "[VART] [LIB_RUNSTATS] %s 0x%08x\n", __func__, hash_);

	if (_initDone == false || _statsView == "none")
		return;

	bool found = false;
	for (size_t subGraphIndex = 0; subGraphIndex < _graph.subGraphs.size(); ++subGraphIndex)
		if (_graph.subGraphs[subGraphIndex].hash == hash_)
		{
			_graph.subGraphs[subGraphIndex].firstGraph = true;
			found                                      = true;
		}
		else
			_graph.subGraphs[subGraphIndex].firstGraph = false;

	if (!found && hash_ != _graph.info.hash)
	{
		std::cerr << "Cannot find graph with hash " << hash_ << " in saved list:" << std::endl;
		for (size_t subGraphIndex = 0; subGraphIndex < _graph.subGraphs.size(); ++subGraphIndex)
			std::cerr << "\tname: " << _graph.subGraphs[subGraphIndex].name
			          << " hash: " << _graph.subGraphs[subGraphIndex].hash << std::endl;
	}
}

void EmbeddedStats::setAsLast(int hash_)
{
	if (_log_libLevel == "LIB_RUNSTATS")
		vart_ml_log(LOG_INFO, "[VART] [LIB_RUNSTATS] %s  0x%08x\n", __func__, hash_);

	if (_initDone == false || _statsView == "none")
		return;

	bool found = false;
	for (size_t subGraphIndex = 0; subGraphIndex < _graph.subGraphs.size(); ++subGraphIndex)
		if (_graph.subGraphs[subGraphIndex].hash == hash_)
		{
			_graph.subGraphs[subGraphIndex].lastGraph = true;
			found                                     = true;
		}
		else
			_graph.subGraphs[subGraphIndex].lastGraph = false;

	if (!found && hash_ != _graph.info.hash)
	{
		std::cerr << "Cannot find graph with hash " << hash_ << " in saved list:" << std::endl;
		for (size_t subGraphIndex = 0; subGraphIndex < _graph.subGraphs.size(); ++subGraphIndex)
			std::cerr << "\tname: " << _graph.subGraphs[subGraphIndex].name
			          << " hash: " << _graph.subGraphs[subGraphIndex].hash << std::endl;
	}
}

void EmbeddedStats::set_config_string(const std::string& config_str)
{
	if (_log_libLevel == "LIB_RUNSTATS")
		vart_ml_log(LOG_INFO, "[VART] [LIB_RUNSTATS] %s  0x%08x\n", __func__);
	_graph.info.config_str = config_str;
}

/* TODO: do we want to propagate the error here? */
void EmbeddedStats::startSubGraph(int hash_) { this->start_step(hash_, EmbeddedStats::FRAMEWORK); }

/* TODO: do we want to propagate the error here? */
void EmbeddedStats::stopSubGraph(int hash_) { this->stop_step(hash_, EmbeddedStats::FRAMEWORK); }

EmbeddedStats::PerGraphInfo* EmbeddedStats::findPerGraphInfo(int hash_)
{
	if (hash_ == _graph.info.hash)
		return &_graph.info;

	for (size_t subGraphIndex = 0; subGraphIndex < _graph.subGraphs.size(); ++subGraphIndex)
		if (_graph.subGraphs[subGraphIndex].hash == hash_)
			return &_graph.subGraphs[subGraphIndex];

	std::cerr << "Cannot find graph with hash " << hash_ << " in saved list:" << std::endl;
	for (size_t subGraphIndex = 0; subGraphIndex < _graph.subGraphs.size(); ++subGraphIndex)
		std::cerr << "\tname: " << _graph.subGraphs[subGraphIndex].name
		          << " hash: " << _graph.subGraphs[subGraphIndex].hash << std::endl;
	return nullptr;
}

void EmbeddedStats::beforeForward(int hash_, size_t batchSize_)
{
	if (_log_libLevel == "LIB_RUNSTATS")
		vart_ml_log(
		    LOG_INFO, "[VART] [LIB_RUNSTATS] %s 0x%08x batchSize_ %zu\n", __func__, hash_, batchSize_);

	if (_initDone == false || _statsView == "none")
		return;

	if (hash_ == _graph.info.hash)
	{
		_uploadCnt++;
		if (_log_libLevel == "LIB_RUNSTATS")
			vart_ml_log(LOG_INFO, "[VART] [LIB_RUNSTATS] uploadCnt: %lld\n", _uploadCnt);
	}

	PerGraphInfo* perGraphInfo = this->findPerGraphInfo(hash_);

	if (_uploadCnt == 0)
	{
		if (batchSize_ == 0)
			perGraphInfo->batchSize = _graph.subGraphs[0].batchSize;
		else if (batchSize_ != (size_t)-1)
			perGraphInfo->batchSize = batchSize_;
	}

	if (hash_ == _graph.info.hash)
	{
		/* Flush previous log */
		fflush(stdout);
		vart_ml_log(LOG_INFO, "[VART]\n[VART] Statistics (in ms)");
		if (batchSize_ != (size_t)-1)
			vart_ml_log(LOG_INFO, ", %zu sample%s", batchSize_, batchSize_ > 1 ? "s" : "");
		vart_ml_log(LOG_INFO, ", batch number %3lld:\n", _uploadCnt);
	}
}

void EmbeddedStats::initAfterForward(PerGraphInfo* perGraphInfo_)
{
	if (perGraphInfo_->hash == _graph.info.hash)
	{
		if (_graph.subGraphs.empty())
			vart_ml_log(LOG_INFO, "[VART]  %s       ", _graph.networkName.c_str());
		else
			vart_ml_log(LOG_INFO,
			            "[VART]  %-*s ",
			            _graph.networkName.size() + strlen("_subgraph#")
			                + (int)log10(_graph.subGraphs.size()),
			            _graph.networkName.c_str());
	}
	else if (_log_libLevel == "LIB_RUNSTATS")
		vart_ml_log(LOG_INFO, "[VART] [LIB_RUNSTATS]  %s  ", perGraphInfo_->name.c_str());
}

void EmbeddedStats::computeWholeGraph(DeltaTimes& deltaTimes_)
{
	/* Keep last wholeGraph computed */
	float wholeGraph = deltaTimes_.wholeGraph;

	deltaTimes_ = {};

	for (auto& subgraph : _graph.subGraphs)
	{
		if (subgraph.runOnCpu == false)
		{
			accumulatePipeline(deltaTimes_, subgraph, npu.preProcess);
			accumulatePipeline(deltaTimes_, subgraph, npu.reorderIn);
			accumulatePipeline(deltaTimes_, subgraph, npu.download);
			accumulatePipeline(deltaTimes_, subgraph, npu.runFpga);
			accumulatePipeline(deltaTimes_, subgraph, npu.upload);
			accumulatePipeline(deltaTimes_, subgraph, npu.reorderOut);
			accumulatePipeline(deltaTimes_, subgraph, npu.postProcess);
			accumulatePipeline(deltaTimes_, subgraph, npu.cpu);
			accumulatePipeline(deltaTimes_, subgraph, npu.full);
		}
		else
			accumulatePipeline(deltaTimes_, subgraph, framework.cpu);

		subgraph.computedTimes = subgraph.times.size();
	}

	if (this->_wholeGraph)
		deltaTimes_.wholeGraph = wholeGraph;
	else
		deltaTimes_.wholeGraph = roundValue(deltaTimes_.npu.full + deltaTimes_.framework.cpu);
}

EmbeddedStats::DeltaTimes EmbeddedStats::snapshotToDelta(PerGraphInfo*        perGraphInfo_,
                                                         const SnapshotInfos& times_)
{
	DeltaTimes deltaTimes = {};

	deltaTimes.wholeGraph = roundValue(diff_msec(times_.startGraph, times_.endGraph));

	if (perGraphInfo_->hash == _graph.info.hash) // Don't need to compute the rest it won't be use
		return deltaTimes;

	if (perGraphInfo_->runOnCpu == true)
	{
		deltaTimes.framework.cpu = roundValue(diff_msec(times_.startFramework, times_.endFramework));
		if (_log_libLevel == "LIB_RUNSTATS")
			vart_ml_log(LOG_INFO, " Framework: CPU   %f\n", deltaTimes.framework.cpu);
		return deltaTimes;
	}

	deltaTimes.npu.preProcess  = roundValue(diff_msec(times_.startQuantize, times_.endQuantize));
	deltaTimes.npu.postProcess = roundValue(diff_msec(times_.startDequantize, times_.endDequantize));

	deltaTimes.npu.reorderIn =
	    roundValue(diff_msec(times_.startReorderIn, times_.endReorderIn) - deltaTimes.npu.preProcess);
	deltaTimes.npu.reorderOut =
	    roundValue(diff_msec(times_.startReorderOut, times_.endReorderOut) - deltaTimes.npu.postProcess);

	deltaTimes.npu.upload   = roundValue(diff_msec(times_.startWrite, times_.endWrite)
                                       - deltaTimes.npu.reorderIn - deltaTimes.npu.preProcess);
	deltaTimes.npu.download = roundValue(diff_msec(times_.startRead, times_.endRead)
	                                     - deltaTimes.npu.reorderOut - deltaTimes.npu.postProcess);

	deltaTimes.npu.runFpga = roundValue(diff_msec(times_.startRun, times_.endRun));

	deltaTimes.npu.full = roundValue(diff_msec(times_.startVartML, times_.endVartML));
	deltaTimes.npu.cpu  = roundValue(deltaTimes.npu.full - deltaTimes.npu.runFpga);

	if (_log_libLevel == "LIB_RUNSTATS")
	{
		vart_ml_log(LOG_INFO,
		            ": Total  %6.2f | NPU %6.2f | CPU sum %6.2f",
		            deltaTimes.npu.full,
		            deltaTimes.npu.runFpga,
		            deltaTimes.npu.cpu);
		if (this->_quantize)
			vart_ml_log(LOG_INFO, " ( Quantize %6.2f | ", deltaTimes.npu.preProcess);
		else
			vart_ml_log(LOG_INFO, " ( ");
		if (this->_reorderIn)
			vart_ml_log(LOG_INFO, "Reorder In %6.2f | ", deltaTimes.npu.reorderIn);
		if (this->_upload)
			vart_ml_log(LOG_INFO, "Upload %6.2f | ", deltaTimes.npu.upload);
		if (this->_download)
			vart_ml_log(LOG_INFO, "Download %6.2f ", deltaTimes.npu.download);
		if (this->_reorderOut)
			vart_ml_log(LOG_INFO, " | Reorder Out %6.2f ", deltaTimes.npu.reorderOut);
		if (this->_dequantize)
			vart_ml_log(LOG_INFO, " | Dequantize %6.2f )\n", deltaTimes.npu.postProcess);
		else
			vart_ml_log(LOG_INFO, " )\n");
	}

	return deltaTimes;
}

void EmbeddedStats::afterForward(int hash_, const SnapshotInfos& infos_)
{
	if (_log_libLevel == "LIB_RUNSTATS")
		vart_ml_log(LOG_INFO, "[VART] [LIB_RUNSTATS] %s 0x%08x\n", __func__, hash_);

	if (_initDone == false || _statsView == "none")
		return;

	PerGraphInfo* perGraphInfo_ = this->findPerGraphInfo(hash_);

	initAfterForward(perGraphInfo_);

	DeltaTimes deltaTimes_ = snapshotToDelta(perGraphInfo_, infos_);

	if ((perGraphInfo_->hash == _graph.info.hash) and (_graph.subGraphs.size() > 0))
	{
		if (_downloadCnt > 0)
		{
			computeWholeGraph(deltaTimes_);

			vart_ml_log(LOG_INFO,
			            ": Total %6.2f | AIE %6.2f | CPU sum %6.2f",
			            deltaTimes_.wholeGraph,
			            deltaTimes_.npu.runFpga,
			            deltaTimes_.npu.cpu);

			if (_graph.nbOnnxSubGraphs)
				vart_ml_log(LOG_INFO, " | Framework %6.2f", deltaTimes_.framework.cpu);
		}
		vart_ml_log(LOG_INFO, "\n");
	}

	if (_downloadCnt > 0)
	{
		perGraphInfo_->times.push_back(deltaTimes_);

		/* Only store the 10 first batches in the report */
		if (_reporting && _reporting->isEnabled() && (perGraphInfo_->hash == _graph.info.hash))
			addJsonObject(deltaTimes_);
	}

	if (perGraphInfo_->inputShape.size() == 0)
	{
		if (_log_libLevel == "LIB_RUNSTATS")
			vart_ml_log(
			    LOG_INFO,
			    "%s\n",
			    printMultiDims("output", perGraphInfo_->outputShape, perGraphInfo_->outputDataType).c_str());
	}
	else
	{
		if (perGraphInfo_->outputShape.size() == 0)
		{
			if (_log_libLevel == "LIB_RUNSTATS")
				vart_ml_log(
				    LOG_INFO,
				    "[VART] [LIB_RUNSTATS] %s\n",
				    printMultiDims("input", perGraphInfo_->inputShape, perGraphInfo_->inputDataType).c_str());
		}
		else
		{
			if (_log_libLevel == "LIB_RUNSTATS")
				vart_ml_log(
				    LOG_INFO,
				    "[VART] [LIB_RUNSTATS] %s processed into %s\n",
				    printMultiDims("input", perGraphInfo_->inputShape, perGraphInfo_->inputDataType).c_str(),
				    printMultiDims("output", perGraphInfo_->outputShape, perGraphInfo_->outputDataType)
				        .c_str());
		}
	}

	if (perGraphInfo_->hash == _graph.info.hash)
	{
		if (_log_libLevel == "LIB_RUNSTATS")
			vart_ml_log(LOG_INFO, "[VART] [LIB_RUNSTATS] downloadCnt: %llu\n", _downloadCnt);
		_downloadCnt++;
		_nbImages += perGraphInfo_->batchSize;
	}
	/* Display log */
	fflush(stdout);
}

void EmbeddedStats::addJsonObject(const DeltaTimes& deltaTimes_)
{
	ordered_json batchObject;
	batchObject["batchCnt"]     = _downloadCnt;
	batchObject["wholeGraph"]   = round((int)(deltaTimes_.wholeGraph * 100)) / 100;
	batchObject["Pre-process"]  = round((int)(deltaTimes_.npu.preProcess * 100)) / 100;
	batchObject["Reorder In"]   = round((int)(deltaTimes_.npu.reorderIn * 100)) / 100;
	batchObject["Upload"]       = round((int)(deltaTimes_.npu.upload * 100)) / 100;
	batchObject["AIE"]          = round((int)(deltaTimes_.npu.runFpga * 100)) / 100;
	batchObject["Post-process"] = round((int)(deltaTimes_.npu.postProcess * 100)) / 100;
	batchObject["Download"]     = round((int)(deltaTimes_.npu.download * 100)) / 100;
	batchObject["Reorder Out"]  = round((int)(deltaTimes_.npu.reorderOut * 100)) / 100;
	batchObject["CPU sum"]      = round((int)(deltaTimes_.npu.cpu * 100)) / 100;
	batchObject["Framework"]    = round((int)(deltaTimes_.framework.cpu * 100)) / 100;

	_batchArray.push_back(batchObject);

	_jsonRoot["batches"] = _batchArray;
}

void EmbeddedStats::update_summary(const std::optional<DeltaTimes> medianDeltaTimes_)
{
	if (_summary == nullptr)
		return;

	// To avoid issue when calling this function multiple times we enforce
	// the updating of the summary.
	auto updateSummary = [&](const std::string& key, auto value) {
		if (_summary.count(key) > 0) // key already exist
			_summary.erase(key);

		_summary[key] = value;

		if (_summary[key].is_number_float())
			_summary[key] = round((int)(value * 100)) / 100;
	};

	updateSummary("Batch Size", _graph.info.batchSize);
	updateSummary("Nb Images", _nbImages);
	if (medianDeltaTimes_)
	{
		updateSummary("wholeGraph", medianDeltaTimes_->wholeGraph);
		updateSummary("Pre-process", medianDeltaTimes_->npu.preProcess);
		updateSummary("Reorder In", medianDeltaTimes_->npu.reorderIn);
		updateSummary("Upload", medianDeltaTimes_->npu.upload);
		updateSummary("AIE", medianDeltaTimes_->npu.runFpga);
		updateSummary("Post-process", medianDeltaTimes_->npu.postProcess);
		updateSummary("Download", medianDeltaTimes_->npu.download);
		updateSummary("Reorder Out", medianDeltaTimes_->npu.reorderOut);
		updateSummary("CPU sum", medianDeltaTimes_->npu.cpu);
		updateSummary("Framework", medianDeltaTimes_->framework.cpu);
		if (medianDeltaTimes_->npu.runFpga)
			updateSummary("imgs per sec", _graph.info.batchSize / (medianDeltaTimes_->npu.runFpga / 1000));
		else
			updateSummary("imgs per sec", 0);
	}
	updateSummary("Nb FPGA subgraphs", _graph.nbNpuSubGraphs);
	updateSummary("Nb CPU subgraphs", _graph.nbOnnxSubGraphs);

	_jsonRoot["summary"].update(_summary);
}

void EmbeddedStats::summary()
{
	if (_initDone == false || _statsView == "none")
		return;

	if (_downloadCnt <= 1)
	{
		vart_ml_log(
		    LOG_INFO,
		    "[VART] The statistic summary can not be displayed, more than 1 inference must be run but "
		    "%llu inference has been executed.\n",
		    _downloadCnt);
		return;
	}

	// update summary for all non timing infos.
	update_summary({});

	vart_ml_log(LOG_INFO, "[VART]\n");
	vart_ml_log(LOG_INFO, "[VART]\t\t %s\n", _graph.info.config_str.c_str());

	vart_ml_log(
	    LOG_INFO,
	    "[VART]\t\t %llu inferences of batch size %zu (the first inference is not used to compute the "
	    "detailed times)\n",
	    _downloadCnt,
	    _graph.info.batchSize);

	if (_graph.info.inputShape.size() != 0)
		vart_ml_log(LOG_INFO,
		            "[VART]\t\t %s\n",
		            printMultiDims("input layer", _graph.info.inputShape, _graph.info.inputDataType).c_str());
	if (_graph.info.outputShape.size() != 0)
		vart_ml_log(
		    LOG_INFO,
		    "[VART]\t\t %s\n",
		    printMultiDims("output layer", _graph.info.outputShape, _graph.info.outputDataType).c_str());

	size_t const nbSubgraphs = _graph.subGraphs.size();
	vart_ml_log(LOG_INFO, "[VART]\t\t %zu total subgraph%s:\n", nbSubgraphs, nbSubgraphs > 1 ? "s" : "");
	vart_ml_log(LOG_INFO,
	            "[VART]\t\t\t %d VART (AIE) subgraph%s\n",
	            _graph.nbNpuSubGraphs,
	            _graph.nbNpuSubGraphs > 1 ? "s" : "");
	vart_ml_log(LOG_INFO,
	            "[VART]\t\t\t %d Framework (CPU) subgraph%s\n",
	            _graph.nbOnnxSubGraphs,
	            _graph.nbOnnxSubGraphs > 1 ? "s" : "");
	vart_ml_log(LOG_INFO, "[VART]\t\t %d sample%s\n", _nbImages, _nbImages > 1 ? "s" : "");
	vart_ml_log(LOG_INFO, "[VART]\n");

	if (_statsView == "short")
		return;

	// display summary
	vart_ml_log(LOG_INFO, "[VART] \"%s\" run summary:\n", _graph.networkName.c_str());

	const PerGraphInfo& graphInfo_ = _graph.info;

#define extractInfo(__type__, __func__)                                                                      \
	({                                                                                                       \
		std::vector<float> data;                                                                             \
                                                                                                             \
		for (size_t index = 0; index < graphInfo_.times.size(); index++)                                     \
			data.push_back(graphInfo_.times[index].__type__);                                                \
                                                                                                             \
		__func__(data);                                                                                      \
	})

	DeltaTimes minDeltaTimes      = {};
	size_t     minIndex           = extractInfo(wholeGraph, searchMinIndex);
	minDeltaTimes.wholeGraph      = graphInfo_.times[minIndex].wholeGraph;
	minIndex                      = extractInfo(npu.full, searchMinIndex);
	minDeltaTimes.npu.full        = graphInfo_.times[minIndex].npu.full;
	minIndex                      = extractInfo(npu.runFpga, searchMinIndex);
	minDeltaTimes.npu.runFpga     = graphInfo_.times[minIndex].npu.runFpga;
	minIndex                      = extractInfo(npu.cpu, searchMinIndex);
	minDeltaTimes.npu.cpu         = graphInfo_.times[minIndex].npu.cpu;
	minIndex                      = extractInfo(framework.cpu, searchMinIndex);
	minDeltaTimes.framework.cpu   = graphInfo_.times[minIndex].framework.cpu;
	minIndex                      = extractInfo(npu.preProcess, searchMinIndex);
	minDeltaTimes.npu.preProcess  = graphInfo_.times[minIndex].npu.preProcess;
	minIndex                      = extractInfo(npu.reorderIn, searchMinIndex);
	minDeltaTimes.npu.reorderIn   = graphInfo_.times[minIndex].npu.reorderIn;
	minIndex                      = extractInfo(npu.upload, searchMinIndex);
	minDeltaTimes.npu.upload      = graphInfo_.times[minIndex].npu.upload;
	minIndex                      = extractInfo(npu.download, searchMinIndex);
	minDeltaTimes.npu.download    = graphInfo_.times[minIndex].npu.download;
	minIndex                      = extractInfo(npu.reorderOut, searchMinIndex);
	minDeltaTimes.npu.reorderOut  = graphInfo_.times[minIndex].npu.reorderOut;
	minIndex                      = extractInfo(npu.postProcess, searchMinIndex);
	minDeltaTimes.npu.postProcess = graphInfo_.times[minIndex].npu.postProcess;

	DeltaTimes maxDeltaTimes      = {};
	size_t     maxIndex           = extractInfo(wholeGraph, searchMaxIndex);
	maxDeltaTimes.wholeGraph      = graphInfo_.times[maxIndex].wholeGraph;
	maxIndex                      = extractInfo(npu.full, searchMaxIndex);
	maxDeltaTimes.npu.full        = graphInfo_.times[maxIndex].npu.full;
	maxIndex                      = extractInfo(npu.runFpga, searchMaxIndex);
	maxDeltaTimes.npu.runFpga     = graphInfo_.times[maxIndex].npu.runFpga;
	maxIndex                      = extractInfo(npu.cpu, searchMaxIndex);
	maxDeltaTimes.npu.cpu         = graphInfo_.times[maxIndex].npu.cpu;
	maxIndex                      = extractInfo(framework.cpu, searchMaxIndex);
	maxDeltaTimes.framework.cpu   = graphInfo_.times[maxIndex].framework.cpu;
	maxIndex                      = extractInfo(npu.preProcess, searchMaxIndex);
	maxDeltaTimes.npu.preProcess  = graphInfo_.times[maxIndex].npu.preProcess;
	maxIndex                      = extractInfo(npu.reorderIn, searchMaxIndex);
	maxDeltaTimes.npu.reorderIn   = graphInfo_.times[maxIndex].npu.reorderIn;
	maxIndex                      = extractInfo(npu.upload, searchMaxIndex);
	maxDeltaTimes.npu.upload      = graphInfo_.times[maxIndex].npu.upload;
	maxIndex                      = extractInfo(npu.download, searchMaxIndex);
	maxDeltaTimes.npu.download    = graphInfo_.times[maxIndex].npu.download;
	maxIndex                      = extractInfo(npu.reorderOut, searchMaxIndex);
	maxDeltaTimes.npu.reorderOut  = graphInfo_.times[maxIndex].npu.reorderOut;
	maxIndex                      = extractInfo(npu.postProcess, searchMaxIndex);
	maxDeltaTimes.npu.postProcess = graphInfo_.times[maxIndex].npu.postProcess;

	DeltaTimes medianDeltaTimes      = {};
	size_t     medianIndex           = extractInfo(wholeGraph, searchMedianIndex);
	medianDeltaTimes.wholeGraph      = graphInfo_.times[medianIndex].wholeGraph;
	medianDeltaTimes.npu.full        = graphInfo_.times[medianIndex].npu.full;
	medianDeltaTimes.npu.runFpga     = graphInfo_.times[medianIndex].npu.runFpga;
	medianDeltaTimes.npu.cpu         = graphInfo_.times[medianIndex].npu.cpu;
	medianDeltaTimes.npu.preProcess  = graphInfo_.times[medianIndex].npu.preProcess;
	medianDeltaTimes.npu.reorderIn   = graphInfo_.times[medianIndex].npu.reorderIn;
	medianDeltaTimes.npu.upload      = graphInfo_.times[medianIndex].npu.upload;
	medianDeltaTimes.npu.download    = graphInfo_.times[medianIndex].npu.download;
	medianDeltaTimes.npu.reorderOut  = graphInfo_.times[medianIndex].npu.reorderOut;
	medianDeltaTimes.npu.postProcess = graphInfo_.times[medianIndex].npu.postProcess;
	medianDeltaTimes.framework.cpu   = graphInfo_.times[medianIndex].framework.cpu;
	/* Update CPU time with non measured CPU processing time */
	float cpu_others = medianDeltaTimes.npu.cpu;
	if (this->_quantize)
		cpu_others -= medianDeltaTimes.npu.preProcess;
	if (this->_reorderIn)
		cpu_others -= medianDeltaTimes.npu.reorderIn;
	if (this->_upload)
		cpu_others -= medianDeltaTimes.npu.upload;
	if (this->_download)
		cpu_others -= medianDeltaTimes.npu.download;
	if (this->_reorderOut)
		cpu_others -= medianDeltaTimes.npu.reorderOut;
	if (this->_dequantize)
		cpu_others -= medianDeltaTimes.npu.postProcess;
	/* Update wholeGraph time with non measured CPU processing time */
	float wholeGraph_others =
	    medianDeltaTimes.wholeGraph - medianDeltaTimes.npu.full - medianDeltaTimes.framework.cpu;

	vart_ml_log(LOG_INFO, "[VART]\t\t detailed times in ms\n");
	vart_ml_log(LOG_INFO,
	            "[VART] "
	            "+--------------------------------+------------+------------+------------+------------+\n");
	vart_ml_log(LOG_INFO,
	            "[VART] | Performance Summary            |  ms/batch  |  ms/batch  |  ms/batch  |   "
	            "sample/s |\n");
	vart_ml_log(LOG_INFO,
	            "[VART] |                                |    min     |    max     |   median   |   "
	            "median   |\n");
	vart_ml_log(LOG_INFO,
	            "[VART] "
	            "+--------------------------------+------------+------------+------------+------------+\n");
	vart_ml_log(LOG_INFO,
	            "[VART] | Whole Graph total              | %10.2f | %10.2f | %10.2f | %10.2f |\n",
	            minDeltaTimes.wholeGraph,
	            maxDeltaTimes.wholeGraph,
	            medianDeltaTimes.wholeGraph,
	            graphInfo_.batchSize / (medianDeltaTimes.wholeGraph / 1000));
	vart_ml_log(LOG_INFO,
	            "[VART] |   VART total (%  4d sub-graph%s | %10.2f | %10.2f | %10.2f | %10.2f |\n",
	            _graph.nbNpuSubGraphs,
	            _graph.nbNpuSubGraphs > 1 ? "s)" : ") ",
	            minDeltaTimes.npu.full,
	            maxDeltaTimes.npu.full,
	            medianDeltaTimes.npu.full,
	            graphInfo_.batchSize / (medianDeltaTimes.npu.full / 1000));
	vart_ml_log(LOG_INFO,
	            "[VART] |     AI acceleration (*)        | %10.2f | %10.2f | %10.2f | %10.2f |\n",
	            minDeltaTimes.npu.runFpga,
	            maxDeltaTimes.npu.runFpga,
	            medianDeltaTimes.npu.runFpga,
	            graphInfo_.batchSize / (medianDeltaTimes.npu.runFpga / 1000));
	vart_ml_log(LOG_INFO,
	            "[VART] |     CPU processing             | %10.2f | %10.2f | %10.2f |            |\n",
	            minDeltaTimes.npu.cpu,
	            maxDeltaTimes.npu.cpu,
	            medianDeltaTimes.npu.cpu);
	if (this->_quantize)
		vart_ml_log(LOG_INFO,
		            "[VART] |       Input quantize           | %10.2f | %10.2f | %10.2f |            |\n",
		            minDeltaTimes.npu.preProcess,
		            maxDeltaTimes.npu.preProcess,
		            medianDeltaTimes.npu.preProcess);
	if (this->_reorderIn)
		vart_ml_log(LOG_INFO,
		            "[VART] |       Input reorder            | %10.2f | %10.2f | %10.2f |            |\n",
		            minDeltaTimes.npu.reorderIn,
		            maxDeltaTimes.npu.reorderIn,
		            medianDeltaTimes.npu.reorderIn);
	if (this->_upload)
		vart_ml_log(LOG_INFO,
		            "[VART] |       Input copy (user->phys)  | %10.2f | %10.2f | %10.2f |            |\n",
		            minDeltaTimes.npu.upload,
		            maxDeltaTimes.npu.upload,
		            medianDeltaTimes.npu.upload);
	if (this->_download)
		vart_ml_log(LOG_INFO,
		            "[VART] |       Output copy (phys->user) | %10.2f | %10.2f | %10.2f |            |\n",
		            minDeltaTimes.npu.download,
		            maxDeltaTimes.npu.download,
		            medianDeltaTimes.npu.download);
	if (this->_reorderOut)
		vart_ml_log(LOG_INFO,
		            "[VART] |       Output reorder           | %10.2f | %10.2f | %10.2f |            |\n",
		            minDeltaTimes.npu.reorderOut,
		            maxDeltaTimes.npu.reorderOut,
		            medianDeltaTimes.npu.reorderOut);
	if (this->_dequantize)
		vart_ml_log(LOG_INFO,
		            "[VART] |       Output dequantize        | %10.2f | %10.2f | %10.2f |            |\n",
		            minDeltaTimes.npu.postProcess,
		            maxDeltaTimes.npu.postProcess,
		            medianDeltaTimes.npu.postProcess);
	if (cpu_others >= 0.01)
		vart_ml_log(LOG_INFO,
		            "[VART] |       Others                   |            |            | %10.2f |        "
		            "    |\n",
		            cpu_others);
	if (_graph.nbOnnxSubGraphs)
		vart_ml_log(LOG_INFO,
		            "[VART] |   OnnxRT CPU (%  4d sub-graph%s | %10.2f | %10.2f | %10.2f |            |\n",
		            _graph.nbOnnxSubGraphs,
		            _graph.nbOnnxSubGraphs > 1 ? "s)" : ") ",
		            minDeltaTimes.framework.cpu,
		            maxDeltaTimes.framework.cpu,
		            medianDeltaTimes.framework.cpu);
	if (wholeGraph_others >= 0.01)
		vart_ml_log(LOG_INFO,
		            "[VART] |   Others                       |            |            | %10.2f |        "
		            "    |\n",
		            wholeGraph_others);
	vart_ml_log(LOG_INFO,
	            "[VART] "
	            "+--------------------------------+------------+------------+------------+------------+\n");
	vart_ml_log(LOG_INFO,
	            "[VART] (min and max are measured individually, only the median sums are meaningful).\n");
	vart_ml_log(LOG_INFO,
	            "[VART] (*) AI Acceleration time includes the transfer to/from the external memories.\n");

	update_summary(medianDeltaTimes);

	if (_reporting && _reporting->isEnabled())
	{
		bool is_found;
		_reporting->isValueExist("RunStats network " + _graph.networkName, is_found);
		if (is_found == true)
			_reporting->updateData("RunStats network " + _graph.networkName, _jsonRoot);
		else
		{
			int i = 0;

			_reporting->isValueExist("RunStats network " + _graph.networkName + "_" + std::to_string(i),
			                         is_found);
			while (is_found == true)
			{
				_reporting->updateData("RunStats network " + _graph.networkName + "_" + std::to_string(i),
				                       _jsonRoot);
				i++;
			}
		}
	}

	_printed = true;
}

EmbeddedStats::EmbeddedStats()
{
	/* The snapshot option is set to false so that statistics are
	   enabled by default, as runSession.summary is on by default.
	*/
	int err = init((int)(long)this);
	if (err)
		throw std::runtime_error(vart_ml_error::exception_message(err));

	/* Set input & output dims ? */
	this->batch_size = 1;
	/* Initialize first step */
	this->first_step = EmbeddedStats::UNDEFINED;
	/* Initialize last step */
	this->last_step = EmbeddedStats::INFERENCE;
}

EmbeddedStats::~EmbeddedStats() { this->print_summary(); }

void EmbeddedStats::delete_stats_object()
{
	_printed = true;

	delete this;
}

int EmbeddedStats::get_hash() { return (int)(long)this; }

void EmbeddedStats::print_summary()
{
	if (!_printed)
		summary();
}

void EmbeddedStats::use_whole_graph()
{
	/* Initialize first step */
	this->first_step = EmbeddedStats::VART_FULL;
	/* Initialize last step */
	this->last_step = EmbeddedStats::VART_FULL;

	/* Set the graph as first and last graph to use wholeGraph */
	setAsFirst((int)(long)this);
	setAsLast((int)(long)this);
}

int EmbeddedStats::start_step(int hash_, EmbeddedStats::step_t step_)
{
	if (_initDone == false || _statsView == "none")
		return vart_ml_error::SUCCESS;

	PerGraphInfo* perGraphInfo_ = this->findPerGraphInfo(hash_);

	clock_gettime(CLOCK_REALTIME, &perGraphInfo_->curr_run.timeStart);

	if (!perGraphInfo_->runOnCpu && perGraphInfo_->previous_step == step_)
		return vart_ml_error::SUCCESS;

	switch (step_)
	{
	case EmbeddedStats::QUANTIZE:
		perGraphInfo_->curr_run.startQuantize = perGraphInfo_->curr_run.timeStart;
		perGraphInfo_->curr_run.endQuantize   = perGraphInfo_->curr_run.startQuantize;
		this->_quantize                       = true;
		break;
	case EmbeddedStats::REORDER_IN:
		perGraphInfo_->curr_run.startReorderIn = perGraphInfo_->curr_run.timeStart;
		perGraphInfo_->curr_run.endReorderIn   = perGraphInfo_->curr_run.startReorderIn;
		this->_reorderIn                       = true;
		break;
	case EmbeddedStats::UPLOAD:
		perGraphInfo_->curr_run.startWrite = perGraphInfo_->curr_run.timeStart;
		perGraphInfo_->curr_run.endWrite   = perGraphInfo_->curr_run.startWrite;
		break;
	case EmbeddedStats::INFERENCE:
		perGraphInfo_->curr_run.startRun = perGraphInfo_->curr_run.timeStart;
		perGraphInfo_->curr_run.endRun   = perGraphInfo_->curr_run.startRun;
		break;
	case EmbeddedStats::DOWNLOAD:
		perGraphInfo_->curr_run.startRead = perGraphInfo_->curr_run.timeStart;
		perGraphInfo_->curr_run.endRead   = perGraphInfo_->curr_run.startRead;
		break;
	case EmbeddedStats::REORDER_OUT:
		perGraphInfo_->curr_run.startReorderOut = perGraphInfo_->curr_run.timeStart;
		perGraphInfo_->curr_run.endReorderOut   = perGraphInfo_->curr_run.startReorderOut;
		this->_reorderOut                       = true;
		break;
	case EmbeddedStats::DEQUANTIZE:
		perGraphInfo_->curr_run.startDequantize = perGraphInfo_->curr_run.timeStart;
		perGraphInfo_->curr_run.endDequantize   = perGraphInfo_->curr_run.startDequantize;
		this->_dequantize                       = true;
		break;
	case EmbeddedStats::VART_FULL:
		perGraphInfo_->curr_run.startVartML = perGraphInfo_->curr_run.timeStart;
		perGraphInfo_->curr_run.endVartML   = perGraphInfo_->curr_run.startVartML;
		this->_vart_ml_full                 = true;
		break;
	case EmbeddedStats::FRAMEWORK:
		perGraphInfo_->curr_run.startFramework = perGraphInfo_->curr_run.timeStart;
		perGraphInfo_->curr_run.endFramework   = perGraphInfo_->curr_run.startFramework;
		break;
	case EmbeddedStats::WHOLEGRAPH:
		perGraphInfo_->curr_run.startGraph = perGraphInfo_->curr_run.timeStart;
		perGraphInfo_->curr_run.endGraph   = perGraphInfo_->curr_run.startGraph;
		this->_wholeGraph                  = true;
		beforeForward((int)(long)this, this->batch_size);
		return vart_ml_error::SUCCESS;
	default:
		return vart_ml_log_err_msg(vart_ml_error::CONFIG_UNSUPPORTED_FEATURE, "Not implemented yet.\n");
	}

	if (this->first_step == EmbeddedStats::UNDEFINED)
		this->first_step = step_;

	if (perGraphInfo_->firstGraph && step_ == this->first_step)
		beforeForward((int)(long)this, this->batch_size);

	if (step_ == EmbeddedStats::INFERENCE)
	{
		if (!this->_upload)
		{
			perGraphInfo_->curr_run.startWrite = perGraphInfo_->curr_run.startRun;
			perGraphInfo_->curr_run.endWrite   = perGraphInfo_->curr_run.startWrite;
		}

		if (!this->_quantize)
		{
			perGraphInfo_->curr_run.startQuantize = perGraphInfo_->curr_run.startWrite;
			perGraphInfo_->curr_run.endQuantize   = perGraphInfo_->curr_run.startQuantize;
		}

		if (!this->_vart_ml_full)
			perGraphInfo_->curr_run.startVartML = perGraphInfo_->curr_run.startQuantize;

		if (!this->_wholeGraph)
			perGraphInfo_->curr_run.startGraph = perGraphInfo_->curr_run.startVartML;

		beforeForward(hash_, this->batch_size);
	}
	else if (step_ == EmbeddedStats::FRAMEWORK)
		beforeForward(hash_, this->batch_size);

	perGraphInfo_->previous_step = step_;

	return vart_ml_error::SUCCESS;
}

int EmbeddedStats::stop_step(int hash_, enum step_t step_)
{
	if (_initDone == false || _statsView == "none")
		return vart_ml_error::SUCCESS;

	PerGraphInfo* perGraphInfo_ = this->findPerGraphInfo(hash_);

	clock_gettime(CLOCK_REALTIME, &perGraphInfo_->curr_run.timeStop);

	switch (step_)
	{
	case EmbeddedStats::QUANTIZE:
		perGraphInfo_->curr_run.endQuantize = perGraphInfo_->curr_run.timeStop;
		break;
	case EmbeddedStats::REORDER_IN:
		perGraphInfo_->curr_run.endReorderIn = perGraphInfo_->curr_run.timeStop;
		break;
	case EmbeddedStats::UPLOAD:
		perGraphInfo_->curr_run.endWrite = perGraphInfo_->curr_run.timeStop;
		this->_upload                    = true;
		break;
	case EmbeddedStats::INFERENCE:
		perGraphInfo_->curr_run.endRun = perGraphInfo_->curr_run.timeStop;
		break;
	case EmbeddedStats::DOWNLOAD:
		perGraphInfo_->curr_run.endRead = perGraphInfo_->curr_run.timeStop;
		this->_download                 = true;
		break;
	case EmbeddedStats::REORDER_OUT:
		perGraphInfo_->curr_run.endReorderOut = perGraphInfo_->curr_run.timeStop;
		break;
	case EmbeddedStats::DEQUANTIZE:
		perGraphInfo_->curr_run.endDequantize = perGraphInfo_->curr_run.timeStop;
		break;
	case EmbeddedStats::VART_FULL:
		perGraphInfo_->curr_run.endVartML = perGraphInfo_->curr_run.timeStop;
		break;
	case EmbeddedStats::FRAMEWORK:
		perGraphInfo_->curr_run.endFramework = perGraphInfo_->curr_run.timeStop;
		break;
	case EmbeddedStats::WHOLEGRAPH:
		perGraphInfo_->curr_run.endGraph = perGraphInfo_->curr_run.timeStop;
		afterForward((int)(long)this, perGraphInfo_->curr_run);
		return vart_ml_error::SUCCESS;
	default:
		return vart_ml_log_err_msg(vart_ml_error::CONFIG_UNSUPPORTED_FEATURE, "Not implemented yet.\n");
	}

	if (step_ == this->last_step)
	{
		if (!this->_download)
		{
			perGraphInfo_->curr_run.startRead = perGraphInfo_->curr_run.endRun;
			perGraphInfo_->curr_run.endRead   = perGraphInfo_->curr_run.startRead;
		}

		if (!this->_dequantize)
		{
			perGraphInfo_->curr_run.startDequantize = perGraphInfo_->curr_run.endRead;
			perGraphInfo_->curr_run.endDequantize   = perGraphInfo_->curr_run.startDequantize;
		}

		if (!this->_vart_ml_full)
			perGraphInfo_->curr_run.endVartML = perGraphInfo_->curr_run.endDequantize;

		if (!this->_wholeGraph)
			perGraphInfo_->curr_run.endGraph = perGraphInfo_->curr_run.endVartML;

		afterForward(hash_, perGraphInfo_->curr_run);
		if (perGraphInfo_->lastGraph)
			afterForward((int)(long)this, perGraphInfo_->curr_run);
	}
	else if (step_ == EmbeddedStats::FRAMEWORK)
	{
		afterForward(hash_, perGraphInfo_->curr_run);
		if (perGraphInfo_->lastGraph)
			afterForward((int)(long)this, perGraphInfo_->curr_run);
	}
	else if (step_ > this->last_step)
		this->last_step = step_;

	return vart_ml_error::SUCCESS;
}

int EmbeddedStats::set_step(int hash_, EmbeddedStats::step_t step_, struct timespec time_)
{
	if (_initDone == false || _statsView == "none")
		return vart_ml_error::SUCCESS;

	PerGraphInfo* perGraphInfo_ = this->findPerGraphInfo(hash_);

	switch (step_)
	{
	case EmbeddedStats::QUANTIZE:
		perGraphInfo_->curr_run.endQuantize = add_time(perGraphInfo_->curr_run.startQuantize, time_);
		break;
	case EmbeddedStats::REORDER_IN:
		perGraphInfo_->curr_run.endReorderIn = add_time(perGraphInfo_->curr_run.startReorderIn, time_);
		break;
	case EmbeddedStats::UPLOAD:
		perGraphInfo_->curr_run.endWrite = add_time(perGraphInfo_->curr_run.startWrite, time_);
		break;
	case EmbeddedStats::INFERENCE:
		perGraphInfo_->curr_run.endRun = add_time(perGraphInfo_->curr_run.startRun, time_);
		break;
	case EmbeddedStats::DOWNLOAD:
		perGraphInfo_->curr_run.endRead = add_time(perGraphInfo_->curr_run.startRead, time_);
		break;
	case EmbeddedStats::REORDER_OUT:
		perGraphInfo_->curr_run.endReorderOut = add_time(perGraphInfo_->curr_run.startReorderOut, time_);
		break;
	case EmbeddedStats::DEQUANTIZE:
		perGraphInfo_->curr_run.endDequantize = add_time(perGraphInfo_->curr_run.startDequantize, time_);
		break;
	case EmbeddedStats::VART_FULL:
		perGraphInfo_->curr_run.endVartML = add_time(perGraphInfo_->curr_run.startVartML, time_);
		break;
	case EmbeddedStats::FRAMEWORK:
		perGraphInfo_->curr_run.endFramework = add_time(perGraphInfo_->curr_run.startFramework, time_);
		break;
	case EmbeddedStats::WHOLEGRAPH:
		perGraphInfo_->curr_run.endGraph = add_time(perGraphInfo_->curr_run.startGraph, time_);
		afterForward((int)(long)this, perGraphInfo_->curr_run);
		return vart_ml_error::SUCCESS;
	default:
		return vart_ml_log_err_msg(vart_ml_error::CONFIG_UNSUPPORTED_FEATURE, "Not implemented yet.\n");
	}

	if (step_ == this->last_step)
	{
		if (!this->_download)
		{
			perGraphInfo_->curr_run.startRead = perGraphInfo_->curr_run.endRun;
			perGraphInfo_->curr_run.endRead   = perGraphInfo_->curr_run.startRead;
		}

		if (!this->_dequantize)
		{
			perGraphInfo_->curr_run.startDequantize = perGraphInfo_->curr_run.endRead;
			perGraphInfo_->curr_run.endDequantize   = perGraphInfo_->curr_run.startDequantize;
		}

		if (!this->_vart_ml_full)
			perGraphInfo_->curr_run.endVartML = perGraphInfo_->curr_run.endDequantize;

		afterForward(hash_, perGraphInfo_->curr_run);
		if (perGraphInfo_->lastGraph)
			afterForward((int)(long)this, perGraphInfo_->curr_run);
	}
	else if (step_ == EmbeddedStats::FRAMEWORK)
	{
		afterForward(hash_, perGraphInfo_->curr_run);
		if (perGraphInfo_->lastGraph)
			afterForward((int)(long)this, perGraphInfo_->curr_run);
	}
	else if (step_ > this->last_step)
		this->last_step = step_;

	return vart_ml_error::SUCCESS;
}

int EmbeddedStats::update_step(int hash_, EmbeddedStats::step_t step_)
{
	if (_initDone == false || _statsView == "none")
		return vart_ml_error::SUCCESS;

	PerGraphInfo* perGraphInfo_ = this->findPerGraphInfo(hash_);

	clock_gettime(CLOCK_REALTIME, &perGraphInfo_->curr_run.timeStop);
	struct timespec diff = diff_time(perGraphInfo_->curr_run.timeStart, perGraphInfo_->curr_run.timeStop);

	switch (step_)
	{
	case EmbeddedStats::QUANTIZE:
		perGraphInfo_->curr_run.endQuantize = add_time(perGraphInfo_->curr_run.endQuantize, diff);
		break;
	case EmbeddedStats::UPLOAD:
		perGraphInfo_->curr_run.endWrite = add_time(perGraphInfo_->curr_run.endWrite, diff);
		break;
	case EmbeddedStats::REORDER_IN:
		perGraphInfo_->curr_run.endReorderIn = add_time(perGraphInfo_->curr_run.endReorderIn, diff);
		break;
	case EmbeddedStats::INFERENCE:
		perGraphInfo_->curr_run.endRun = add_time(perGraphInfo_->curr_run.endRun, diff);
		break;
	case EmbeddedStats::DOWNLOAD:
		perGraphInfo_->curr_run.endRead = add_time(perGraphInfo_->curr_run.endRead, diff);
		break;
	case EmbeddedStats::REORDER_OUT:
		perGraphInfo_->curr_run.endReorderOut = add_time(perGraphInfo_->curr_run.endReorderOut, diff);
		break;
	case EmbeddedStats::DEQUANTIZE:
		perGraphInfo_->curr_run.endDequantize = add_time(perGraphInfo_->curr_run.endDequantize, diff);
		break;
	case EmbeddedStats::VART_FULL:
		perGraphInfo_->curr_run.endVartML = add_time(perGraphInfo_->curr_run.endVartML, diff);
		break;
	case EmbeddedStats::FRAMEWORK:
		perGraphInfo_->curr_run.endFramework = add_time(perGraphInfo_->curr_run.endFramework, diff);
		break;
	case EmbeddedStats::WHOLEGRAPH:
		perGraphInfo_->curr_run.endGraph = add_time(perGraphInfo_->curr_run.endGraph, diff);
		break;
	default:
		return vart_ml_log_err_msg(vart_ml_error::CONFIG_UNSUPPORTED_FEATURE, "Not implemented yet.\n");
	}

	return vart_ml_error::SUCCESS;
}
