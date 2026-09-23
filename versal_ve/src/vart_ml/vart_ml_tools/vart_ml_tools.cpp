/**
 * @file vart_ml_tools.cpp
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

#include <cstdlib>
#include <functional>
#include <iostream>
#include <map>
#include <sstream>
#include <unistd.h>

#include "clocks.h"
#include "ctrlbus_poke.h"
#include "ddr_poke.h"
#include "downloader.h"
#include "io/io.h"
#include "tester.h"
#include "uploader.h"
#include "utils/log.h"
#include "ddr_debug.h"

std::map<std::string, std::function<int(int, const char**)> > modes{
	{ "help", NULL },
	{ "config", config },
	{ "fpgaStatus", fpgaStatus },
	{ "resource_info", print_resource_info },
	{ "tester", tester },
	{ "ctrlbus", ctrlbus_poke },
	{ "ddr", ddr_poke },
	{ "ddr_index", ddr_index_poke },
	{ "ddr_idx_range", ddr_index_poke_range },
	{ "ddr_cmp_range", ddr_index_compare_range },
	{ "memtile", memtile_poke },
	{ "memtile_index", memtile_index_poke },
	{ "noc", noc_poke },
	{ "upload", ddr_upload },
	{ "download", ddr_download },
	{ "timestamp", timestamp },
	{ "reset_mutex", reset_mutex },
	{ "frequency", frequency },
	{ "aieFrequency", aieFrequency },
	{ "checkCores", check_cores },
	{ "start_clocks", start_clocks },
	{ "stop_clocks", stop_clocks },
	{ "ddr_debug", ddr_debug },
	{ "temperature", temperature },
	{ "overtemp", overtemp }
};

void usage(std::string reason, int exit_type)
{
	std::ostringstream usage;
	usage << reason << std::endl;
	usage << "Usage:" << std::endl;
	usage << "vart_ml_tools --timestamp     [ip_idx]" << std::endl;
	usage << "vart_ml_tools --config        [ip_idx]" << std::endl;
	usage << "vart_ml_tools --fpgaStatus    [ip_idx]" << std::endl;
	usage << "vart_ml_tools --resource_info" << std::endl;
	usage << "vart_ml_tools --tester        [ip_idx] [--test-ctrl] [--test-ddr]" << std::endl;
	usage << std::endl;
	if (npu_is_embedded())
		usage << "vart_ml_tools --checkCores    [ip_idx]" << std::endl;
	usage << "vart_ml_tools --reset_mutex   [ip_idx]" << std::endl;
	usage << "vart_ml_tools --frequency     [ip_idx] [target frequency]" << std::endl;
	if (npu_is_embedded())
		usage << "vart_ml_tools --aieFrequency  [target frequency]" << std::endl;
	usage << std::endl;

	usage << "vart_ml_tools --ctrlbus       [ip_idx] <address> [word to write]" << std::endl;
	if (npu_is_embedded())
	{
		usage << "vart_ml_tools --start_clocks  [ip_idx]" << std::endl;
		usage << "vart_ml_tools --stop_clocks   [ip_idx]" << std::endl;
	}
	usage << "vart_ml_tools --ddr           <address> [word to write]" << std::endl;
	usage << "vart_ml_tools --ddr_index     <index> <offset> [word to write]" << std::endl;
	usage << "vart_ml_tools --ddr_idx_range <index> <offset> <size> [pattern to write]" << std::endl;
	usage << "vart_ml_tools --ddr_cmp_range <index> <offset> <size> [pattern to write]" << std::endl;
	if (npu_is_embedded())
		usage << "vart_ml_tools --ddr_debug     <ip_idx> <address> [512 bits to write, as hex]" << std::endl;
	usage << "vart_ml_tools --memtile       <address> [word to write]" << std::endl;
	usage << "vart_ml_tools --memtile_index <index> <offset> [word to write]" << std::endl;
	usage << "vart_ml_tools --noc           <address> [word to write]" << std::endl;
	usage << "vart_ml_tools --upload        <address> <binary file>" << std::endl;
	usage << "vart_ml_tools --upload        <address> <size> <binary file>" << std::endl;
	usage << "vart_ml_tools --download      <address> <size> <binary file>" << std::endl;
	if (npu_is_embedded())
	{
		usage << "vart_ml_tools --temperature   [time interval] [timeout]" << std::endl;
		usage << "vart_ml_tools --overtemp      [new overtemp value]" << std::endl;
	}
	vart_ml_log(LOG_INFO, "%s", usage.str().c_str());
	exit(exit_type);
}

/**
 * @brief Simple function to access different functionality or test.
 *
 * @param argc Number of argument
 * @param argv Argument list.
 * @return int Return 0 if ok, error code otherwise.
 */
int main(int argc, char** argv)
{
	set_log_lvl(LOG_INFO);

	if (argc == 1)
		usage("Not enough arguments supplied", EXIT_FAILURE);

	char* arg = argv[1];
	if (arg[0] == '-' and arg[1] == '-') // Remove --
		arg = arg + 2;

	int err = npu_create_ip_context();
	if (err)
		return err;

	auto handler = modes.find(arg);
	if (handler == modes.end())
		usage(std::string("Invalid command ") + argv[1], EXIT_FAILURE);

	if (handler->first == "help")
		usage(std::string("The command line is: vart_ml_tools --help"), EXIT_SUCCESS);

	return handler->second(argc - 1, (const char**)&argv[1]);
}
