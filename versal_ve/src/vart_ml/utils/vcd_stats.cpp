/**
 * @file vcd_stats.cpp
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

#include <algorithm>
#include <assert.h>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <queue>
#include <vector>

#include "npu_runner/npu_runner.h"
#include "utils/log.h"
#include "vcd_stats.h"

using vcd_event_t = std::pair<long int, std::string>;

struct VcdStatsPrivate
{
	/* Are stats enabled. */
	bool enable = false;

	/* Has the stats been initialized (i.e.: has at least one network been registered). */
	bool init = false;

	/* Has the run started (i.e.: has at least one call to vcd_event been made). */
	bool          run_started = false;
	std::ofstream vcd_file;

	/* Number of threads vart_ml_runner will be allowed to run simultaneously. */
	size_t nb_threads_ = 1;

	/* Vector of registered network names. */
	std::vector<std::string> network_names;

	/* Starting time of the run. All timestamps use this time as reference. */
	struct timespec starting_time;

	/* User defined global event names. */
	std::vector<std::string> user_defined_global_event_names;

	/* User defined event names. */
	std::vector<std::string> user_defined_event_names;

	/* Header section of the VCD file, containing global information. */
	std::string vcd_header;

	/* Variable definition section of the VCD file, each element of the vector contains one module (i.e.: the
	 * variables defined for a single network). At index 0 is the module for global variables. */
	std::vector<std::string> vcd_var_def;

	/* Dumpvars section of the VCD file, containing the initial values of all variables. */
	std::string vcd_dumpvars;

	/* A queue of events (pair of timestamp and string to print) for each network. At index 0 is the queue for
	 * global events. */
	std::vector<std::queue<vcd_event_t>> vcd_value_change;
};

static std::mutex vcd_mutex;
VcdStatsPrivate   vcd_stats;

static const std::string vcd_event_names[] = {
#define EVENT(event) std::to_string(event) + '_' + #event,
#include "vcd_event.inc"
#undef EVENT
};

static const size_t vcd_events_count = sizeof(vcd_event_names) / sizeof(vcd_event_names[0]);

static inline std::string get_wire_prefix(int8_t network_id)
{
	return std::string(1, (char)('A' + network_id));
}

static void vcd_add_var(int8_t network_id, size_t event_id, std::string event_name)
{
	for (size_t tid = 0; tid < vcd_stats.nb_threads_; ++tid)
	{
		std::string var_short_name =
		    get_wire_prefix(network_id) + "_" + std::to_string(event_id) + "_t" + std::to_string(tid);
		std::string var_long_name =
		    vcd_stats.network_names[network_id] + "_" + event_name + "_t" + std::to_string(tid);

		// Define new variable for this network.
		vcd_stats.vcd_var_def[network_id] +=
		    "$var wire 1 " + var_short_name + " " + var_long_name + " $end\n";

		// Set variable's initial value to 0.
		vcd_stats.vcd_dumpvars += "b0 " + var_short_name + "\n";
	}
}

static long int diff_usec(const struct timespec& start, const struct timespec& end)
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

	return diff.tv_sec * 1e6 + diff.tv_nsec / 1e3;
};

static int vcd_init()
{
	vcd_stats.init = true;

	// Check if vcd stats are enabled.
	vcd_stats.enable = getenv("VAISW_PROFILING_VCD") != NULL;
	if (!vcd_stats.enable)
		return -1;

	// Get vcd log path. If given path is a directory, append a file name.
	std::string log_file_path = getenv("VAISW_PROFILING_VCD");
	if (std::filesystem::is_directory(log_file_path))
	{
		log_file_path += "/vcd.log";
		vart_ml_log(LOG_INFO, "Given vcd log path is a directory.\n");
	}

	vcd_stats.vcd_file.open(log_file_path);

	if (vcd_stats.vcd_file.fail())
		vart_ml_log(LOG_WARN, "Could not open vcd output file `%s'.\n", log_file_path.c_str());
	else
		vart_ml_log(LOG_INFO, "Writing vcd logs to `%s'.\n", log_file_path.c_str());

	vcd_stats.vcd_header += "$version 1.0 $end\n";
	vcd_stats.vcd_header += "$comment VAISW profiler from AMD tool $end\n";
	vcd_stats.vcd_header += "$timescale 1us $end\n";

	// Add global module.
	vcd_stats.network_names.push_back(std::string("global"));
	vcd_stats.vcd_var_def.push_back(std::string("$scope module global $end\n"));
	vcd_stats.vcd_value_change.push_back(std::queue<vcd_event_t>());

	vcd_stats.vcd_dumpvars += "$dumpvars\n";

	return 0;
}

int8_t vcd_register_network(std::string network_name)
{
	const std::lock_guard<std::mutex> lock(vcd_mutex);

	// Initialize stats if not done before.
	if (!vcd_stats.init)
	{
		if (vcd_init() == -1)
			return -1;
	}
	else if (!vcd_stats.enable)
		return -1;

	// Check if logging has already started.
	if (vcd_stats.run_started)
		vart_ml_log(LOG_WARN,
		            "Loading a snapshot while inference of other snapshot has started. VCD dump may not "
		            "work\n.If possible, load all snapshot before starting inference.\n");

	// Get network id.
	int8_t network_id = vcd_stats.network_names.size();

	// Set network name.
	vcd_stats.network_names.push_back(network_name);

	// Create a new queue for this network's events.
	vcd_stats.vcd_value_change.push_back(std::queue<vcd_event_t>());

	// Add pre-defined events to network.
	vcd_stats.vcd_var_def.push_back(std::string("$scope module " + network_name + " $end\n"));
	for (size_t i = 0; i < vcd_events_count; ++i)
		vcd_add_var(network_id, i, vcd_event_names[i]);

	return network_id;
}

int8_t vcd_register_global_event(const std::string& event_name)
{
	// If VCD stats are not enabled, do nothing.
	if (not vcd_stats.enable)
		return -1;

	int8_t event_id = vcd_stats.user_defined_global_event_names.size();
	vcd_stats.user_defined_global_event_names.push_back(event_name);

	vcd_add_var(VCD_GLOBAL_ID, event_id, event_name);

	return event_id;
}

int8_t vcd_register_event(const std::string& event_name)
{
	// If VCD stats are not enabled, do nothing.
	if (not vcd_stats.enable)
		return -1;

	int8_t event_id = vcd_stats.user_defined_event_names.size() + vcd_events_count;
	vcd_stats.user_defined_event_names.push_back(event_name);

	// Start at 1 to ignore the global module.
	for (size_t network_id = 1; network_id < vcd_stats.network_names.size(); ++network_id)
		vcd_add_var(network_id, event_id, event_name);

	return event_id;
}

void vcd_set_nb_threads(size_t nb_threads)
{
	// If VCD stats are not enabled, do nothing.
	if (!vcd_stats.enable)
		return;

	if (vcd_stats.nb_threads_ >= nb_threads)
		return;

	vcd_stats.nb_threads_ = nb_threads;

	// Clear dumpvars.
	vcd_stats.vcd_dumpvars.clear();
	vcd_stats.vcd_dumpvars += "$dumpvars\n";

	// Clear global var defs and re add global variables.
	vcd_stats.vcd_var_def[VCD_GLOBAL_ID].clear();
	vcd_stats.vcd_var_def[VCD_GLOBAL_ID] += std::string("$scope module global $end\n");
	for (size_t event_id = 0; event_id < vcd_stats.user_defined_global_event_names.size(); ++event_id)
		vcd_add_var(VCD_GLOBAL_ID, event_id, vcd_stats.user_defined_global_event_names[event_id]);

	// Clear networks var defs and re add network variables.
	for (size_t network_id = 1; network_id < vcd_stats.network_names.size(); network_id++)
	{
		vcd_stats.vcd_var_def[network_id].clear();
		vcd_stats.vcd_var_def[network_id] +=
		    std::string("$scope module " + vcd_stats.network_names[network_id] + " $end\n");
		for (size_t event_id = 0; event_id < vcd_events_count; ++event_id)
			vcd_add_var(network_id, event_id, vcd_event_names[event_id]);
		for (size_t event_id = 0; event_id < vcd_stats.user_defined_event_names.size(); ++event_id)
			vcd_add_var(
			    network_id, event_id + vcd_events_count, vcd_stats.user_defined_event_names[event_id]);
	}
}

void vcd_event(const struct vcd_context& vcd_context, int8_t event_id, bool state)
{
	// If VCD stats are not enabled, do nothing.
	if (!vcd_stats.enable)
		return;

	// Check if this is the first call to vcd_event.
	long int event_timestamp;
	if (vcd_stats.run_started)
	{
		// Compute event timestamp from starting time.
		struct timespec t;
		clock_gettime(CLOCK_REALTIME, &t);
		event_timestamp = diff_usec(vcd_stats.starting_time, t);
	}
	else
	{
		// This is the first event.
		event_timestamp = 0;

		const std::lock_guard<std::mutex> lock(vcd_mutex);

		vcd_stats.run_started = true;

		// Set starting time.
		clock_gettime(CLOCK_REALTIME, &vcd_stats.starting_time);

		// Dump preliminary sections.
		vcd_stats.vcd_file << vcd_stats.vcd_header;
		vcd_stats.vcd_file << "$comment Starting time: " << vcd_stats.starting_time.tv_sec << std::setw(9)
		                   << std::setfill('0') << vcd_stats.starting_time.tv_nsec << "ns $end\n";
		for (const std::string& var_def : vcd_stats.vcd_var_def)
			vcd_stats.vcd_file << var_def << "$upscope $end\n";
		vcd_stats.vcd_file << "$enddefinitions $end\n";
		vcd_stats.vcd_file << vcd_stats.vcd_dumpvars;
		vcd_stats.vcd_file << "$end\n";
		vcd_stats.vcd_file.flush();
	}

	// If network does not exist, warn and quit.
	if ((vcd_context.network_id < 0) || ((size_t)vcd_context.network_id >= vcd_stats.network_names.size()))
	{
		vart_ml_log(LOG_WARN,
		            "Logging an event for non-existent network: %d.\nPlease ensure you are calling %s with a "
		            "registered network.\n",
		            vcd_context.network_id,
		            __func__);
		return;
	}
	// If event does not exist, warn and quit.
	else if ((event_id < 0)
	         || ((vcd_context.network_id == VCD_GLOBAL_ID)
	             && ((size_t)event_id >= vcd_stats.user_defined_global_event_names.size()))
	         || ((vcd_context.network_id > VCD_GLOBAL_ID)
	             && ((size_t)event_id >= (vcd_events_count + vcd_stats.user_defined_event_names.size()))))
	{
		vart_ml_log(
		    LOG_WARN,
		    "Logging non-existent event: %d.\nPlease ensure you are calling %s with a registered event.\n",
		    event_id,
		    __func__);
		return;
	}

	std::queue<vcd_event_t>& network_queue = vcd_stats.vcd_value_change.at(vcd_context.network_id);

	// Push event in network's queue.
	const std::string event_str("#" + std::to_string(event_timestamp) + "\nb" + std::to_string(state) + " "
	                            + get_wire_prefix(vcd_context.network_id) + "_" + std::to_string(event_id)
	                            + "_t" + std::to_string(vcd_context.thread_id) + "\n");
	network_queue.push({ event_timestamp, event_str });

	// If this is the end of a run.
	if (event_id == EXECUTE && !state)
	{
		const std::lock_guard<std::mutex> lock(vcd_mutex);

		// Dump events in order until the network's queue is empty.
		while (!network_queue.empty())
		{
			// Get the earliest event in the queues.
			std::vector<std::queue<vcd_event_t>>::iterator earliest_event_queue =
			    std::min_element(vcd_stats.vcd_value_change.begin(),
			                     vcd_stats.vcd_value_change.end(),
			                     [](const auto& a, const auto& b) {
				                     long int x = std::numeric_limits<long int>::max();
				                     long int y = std::numeric_limits<long int>::max();
				                     if (!a.empty())
					                     x = std::get<0>(a.front());
				                     if (!b.empty())
					                     y = std::get<0>(b.front());
				                     return x < y;
			                     });
			vcd_event_t earliest_event = earliest_event_queue->front();
			earliest_event_queue->pop();

			// Dump the event.
			vcd_stats.vcd_file << std::get<1>(earliest_event);
		}
	}
}
