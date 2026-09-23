/**
 * @file fpga_info.cpp
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
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <pwd.h>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unistd.h>
#include <vector>
#include <wordexp.h>

#include "fpga_info.h"
#include "log.h"
#include "shell.h"

#define STRINGTOUL(x) (parse_uint(x))

static std::map<std::string, std::string> vaiswIni;

static std::string extractHeader(const std::string& src) { return src.substr(0, src.find('.')); }

static std::string currentDirectory()
{
	std::ostringstream currentDir;

	char* dirChar = get_current_dir_name();
	currentDir << dirChar << "/";
	free(dirChar);

	return (currentDir.str());
}

std::vector<size_t> convertToVector(const std::string& src)
{
	std::vector<size_t> ret;
	if (src[0] != '[')
	{
		ret.push_back(STRINGTOUL(src));
		return ret;
	}
	else
	{
		std::string data = src.substr(1, src.find(']') - 1);

		while (data.find(',') != std::string::npos)
		{
			ret.push_back(STRINGTOUL(data.substr(0, data.find(','))));
			data.erase(0, data.find(',') + 1);
		}
		if (!data.empty())
			ret.push_back(STRINGTOUL(data));

		return ret;
	}
}

int map_from_file(const std::string& file_path, std::map<std::string, std::string>& map)
{
	std::ifstream f(file_path);
	if (!f.is_open())
		return vart_ml_log_err_msg(vart_ml_error::FILE_ACCESS_REFERENCE_FAILURE,
		                           "FPGA info file %s not found.\n",
		                           file_path.c_str());

	std::string line;
	std::string section;
	while (std::getline(f, line))
	{
		if (line[0] == '[')
		{
			line.erase(std::remove(line.begin(), line.end(), '['), line.end());
			line.erase(std::remove(line.begin(), line.end(), ']'), line.end());
			section = line;
			continue;
		}
		line.erase(std::remove(line.begin(), line.end(), ' '), line.end());
		line.erase(std::remove(line.begin(), line.end(), '\t'), line.end());
		std::stringstream ss(line);

		std::string index;
		std::getline(ss, index, '=');

		std::string data;
		std::getline(ss, data);
		data.erase(std::remove(data.begin(), data.end(), '"'), data.end());

		map[section + '.' + index] = data;
	}

	f.close();
	return vart_ml_error::SUCCESS;
}

static int shellExpansion(const std::string& src, std::string& dst)
{
	wordexp_t express;
	if (wordexp(src.c_str(), &express, 0) != 0)
		return vart_ml_log_err_msg(
		    vart_ml_error::FILE_ACCESS_WORD_EXP_FAILURE, "Cannot call wordexp on \"%s\".\n", src.c_str());

	for (size_t i = 0; i < express.we_wordc; ++i)
	{
		dst.append(express.we_wordv[i]);
		if (i + 1 < express.we_wordc)
			dst.append(" ");
	}
	wordfree(&express);

	return vart_ml_error::SUCCESS;
}

static int parse_vaiswIni()
{
	if (vaiswIni.empty())
	{
		/* System directory*/
		char* system_file_path = getenv("VAISW_INSTALL_DIR");

		std::ostringstream path;
		if (system_file_path == NULL)
			return vart_ml_log_err_msg(vart_ml_error::CONFIG_MISSING_VAISW_INSTALL_DIR,
			                           "VAISW_INSTALL_DIR is not set.\n");
		else
			path << system_file_path << "/vaisw.ini";

		int err;
		if (access(path.str().c_str(), 0) == 0)
		{
			auto dst = std::string{};
			err      = shellExpansion(path.str(), dst);
			if (err)
				return err;

			err = map_from_file(dst, vaiswIni);
			if (err)
				return err;
		}

		/* Global directory*/
		path.str("");
		path << getenv("HOME") << "/.amd/vaisw/vaisw.ini";

		if (access(path.str().c_str(), 0) == 0)
		{
			auto dst = std::string{};
			err      = shellExpansion(path.str(), dst);
			if (err)
				return err;

			err = map_from_file(dst, vaiswIni);
			if (err)
				return err;
		}

		/* Local directory*/
		path.str("");
		path << currentDirectory() << "vaisw.ini";

		if (access(path.str().c_str(), 0) == 0)
		{
			auto dst = std::string{};
			err      = shellExpansion(path.str(), dst);
			if (err)
				return err;

			err = map_from_file(dst, vaiswIni);
			if (err)
				return err;
		}
	}

	return vart_ml_error::SUCCESS;
}

int get_user_config(const char* cfg, const char** info)
{
	size_t      dot         = std::string(cfg).find('.');
	std::string section     = std::string(cfg).substr(0, dot);
	std::string sub_section = std::string(cfg).substr(dot + 1, std::string(cfg).npos);
	std::string env_cfg     = "VAISW_" + section + "_" + sub_section;

	transform(env_cfg.begin(), env_cfg.end(), env_cfg.begin(), ::toupper);

	*info = getenv(env_cfg.c_str());
	if (*info != NULL)
		return vart_ml_error::SUCCESS;

	int err = parse_vaiswIni();
	if (err)
		return err;

	if (vaiswIni.contains(cfg))
		*info = vaiswIni[cfg].c_str();
	else
		*info = NULL;

	return vart_ml_error::SUCCESS;
}

bool check_user_config(const char* cfg)
{
	const char* info;

	int err = get_user_config(cfg, &info);
	if (err)
		throw std::invalid_argument("Could not find User Config " + std::string(cfg) + ".\n");

	if (info != NULL && (strcasecmp(info, "true") == 0 || strcasecmp(info, "1") == 0))
		return true;

	return false;
}

static int get_fpga_info_path(uint32_t timestamp, std::string& path)
{
	std::string fpga_info_path;

	int err = parse_vaiswIni();
	if (err)
		return err;

	if (getenv("VAISW_FPGA_INFOFILE") == NULL && !vaiswIni.contains("fpga.infoFile"))
	{
		/* The following code may seem convoluted, but it tries to mirror
		   the logic libBackend/device uses.
		   fpga.infoFile's default value is not usable, which is why even
		   libDevice doesn't use its default value when it's not set. See
		   getConfigFile in:
		   sw/Vaisw/libBackend/dparams/Parameters.cpp.
		   In that case, use fpga.baseDirectory instead.
		*/

		std::ostringstream info;
		if (vaiswIni.contains("fpga.baseDirectory"))
			info << vaiswIni["fpga.baseDirectory"];
		else
		{
			char* release = getenv("VAISW_INSTALL_DIR");
			if (release == NULL)
				return vart_ml_log_err_msg(vart_ml_error::CONFIG_MISSING_VAISW_INSTALL_DIR,
				                           "VAISW_INSTALL_DIR is not set.\n");
			info << release << "/bitstream";
		}
		fpga_info_path = info.str();
	}
	else if (getenv("VAISW_FPGA_INFOFILE") != NULL)
		fpga_info_path = getenv("VAISW_FPGA_INFOFILE");
	else
		fpga_info_path = vaiswIni["fpga.infoFile"];

	auto dst = std::string{};
	err      = shellExpansion(fpga_info_path, dst);
	if (err)
		return err;

	fpga_info_path = dst;

	std::ostringstream info2;
	info2 << fpga_info_path << "/fpga_info_" << std::hex << std::uppercase << std::setw(8)
	      << std::setfill('0') << timestamp << std::nouppercase << std::dec << ".txt";

	path = info2.str();
	return vart_ml_error::SUCCESS;
}

int parse_fpga_info(uint32_t timestamp, std::map<std::string, std::string>& fpga_info)
{
	std::stringstream ss;
	ss << "0x" << std::hex << timestamp;

	std::string fpga_info_path;
	int         err;
	err = get_fpga_info_path(timestamp, fpga_info_path);
	if (err)
		return err;

	fpga_info["fpga.timestamp"] = ss.str();

	err = map_from_file(fpga_info_path, fpga_info);
	if (err)
		return err;

	return vart_ml_error::SUCCESS;
}

void print_fpga_info(const std::map<std::string, std::string>& fpga_info)
{
	bool needExtraTable = false;
	vart_ml_log(LOG_INFO, "╔═══════════════════════════════════╤═══════════════════════════════════╗\n");
	vart_ml_log(LOG_INFO, "║ %-33s │ %33s ║\n", "clocks.clk4xRunning", fpga_info.at("clocks.clk4x").c_str());
	for (const auto& info : fpga_info)
	{
		if (extractHeader(info.first) == "mapping" || extractHeader(info.first) == "noc"
		    || extractHeader(info.first) == "externalMemoryOrganization")
		{
			needExtraTable = true;
			continue;
		}
		vart_ml_log(LOG_INFO, "║ %-33s │ %33s ║\n", info.first.c_str(), info.second.c_str());
	}
	vart_ml_log(LOG_INFO, "╚═══════════════════════════════════╧═══════════════════════════════════╝\n");

	if (needExtraTable)
	{
		vart_ml_log(LOG_INFO,
		            "╔════════════════════════════════════════════════════════════════════╤═════════════════"
		            "════════════════"
		            "═══════════════════════════╗\n");
		for (const auto& info : fpga_info)
		{
			if (extractHeader(info.first) != "mapping" && extractHeader(info.first) != "noc"
			    && extractHeader(info.first) != "externalMemoryOrganization")
				continue;
			vart_ml_log(
			    LOG_INFO, "║ %-66s │ %58s ║\n", info.first.c_str(), info.second.substr(0, 58).c_str());
		}
		vart_ml_log(LOG_INFO,
		            "╚════════════════════════════════════════════════════════════════════╧════════════════"
		            "═════════════════"
		            "═══════════════════════════╝\n");
	}
}

int print_fpga_info_path(uint32_t timestamp)
{
	std::string fpga_info_path;
	int         err = get_fpga_info_path(timestamp, fpga_info_path);
	if (err)
		return err;

	std::cout << fpga_info_path;
	return vart_ml_error::SUCCESS;
}

bool is_verbose()
{
	return (vaiswIni.contains("snapshot.showAllAccesses")) ? STRINGTOUL(vaiswIni["snapshot.showAllAccesses"])
	                                                       : false;
}
