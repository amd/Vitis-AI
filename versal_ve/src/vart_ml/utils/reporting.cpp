/**
 * @file reporting.cpp
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

#include "reporting.h"

#include <chrono>
#include <ctime>
#include <dirent.h>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <list>
#include <pwd.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

#include "fpga_info.h"
#include "log.h"
#include "shell.h"

static int rm(const std::string& path_);

static std::string extractDirname(const std::string& path)
{
	size_t pos = path.find_last_of('/');
	if (pos == std::string::npos)
		return "";
	else
		return path.substr(0, pos);
}

static std::vector<std::string> getFiles(const std::string& pathDir)
{
	std::vector<std::string> files;
	auto                     dirp = opendir(pathDir.c_str());
	if (dirp == nullptr)
		return {};

	dirent* dp = nullptr;
	while ((dp = readdir(dirp)) != nullptr)
	{
		if (strcmp(dp->d_name, ".") and strcmp(dp->d_name, ".."))
			files.push_back(std::string(dp->d_name));
	}
	closedir(dirp);
	std::sort(files.begin(), files.end());
	return files;
}

static bool isDirectory(const char* dir)
{
	DIR* d = nullptr;

	d = opendir(dir);
	if (!d)
		return false;

	closedir(d);
	return true;
}

static bool isLink(const char* path)
{
	struct stat buf;

	lstat(path, &buf);
	if (S_ISLNK(buf.st_mode))
		return true;

	return false;
}

static bool isFile(const char* path, const char* mode)
{
	FILE* f = nullptr;

	f = fopen(path, mode);
	if (!f)
		return false;

	fclose(f);
	return true;
}

static std::list<std::string> splitString(const std::string& str, char sep, size_t max)
{
	size_t                 pos = 0, cnt = 0, i;
	std::list<std::string> list;

	while (++cnt != max and (i = str.find(sep, pos)) != str.npos)
	{
		if (i > pos)
			list.push_back(str.substr(pos, i - pos));
		pos = i + 1;
	}

	if (pos != str.size())
		list.push_back(str.substr(pos, str.size() - pos + 1));

	return list;
}

static int recmkdir(const std::string& dir, unsigned int mode)
{
	std::ostringstream     path;
	std::list<std::string> dirnames = splitString(dir, '/', 0);

	if (dir.c_str()[0] == '/')
		path << "/";

	for (auto& dirname : dirnames)
	{
		path << dirname << "/";

		if (access(path.str().c_str(), F_OK) == -1)
		{
			int ret = mkdir(path.str().c_str(), mode);
			if (ret != 0 && errno != EEXIST)
				return errno;
		}
	}

	return 0;
}

static int rmFile(const std::string& path_)
{
	if (unlink(path_.c_str()) != 0)
		return vart_ml_log_err_msg(
		    vart_ml_error::FILE_ACCESS_DELETE_FAILURE, "Failed to delete file \"%s\".\n", path_.c_str());

	return vart_ml_error::SUCCESS;
}

static int rmDir(const std::string& path_)
{
	for (const auto& file : getFiles(path_))
	{
		if (file != "." and file != "..")
		{
			std::string fullpath = path_ + "/" + file;
			int         err      = rm(fullpath);
			if (err)
				return err;
		}
	}

	if (rmdir(path_.c_str()) != 0)
		return vart_ml_log_err_msg(
		    vart_ml_error::FILE_ACCESS_DELETE_FAILURE, "Failed to delete directory \"%s\".\n", path_.c_str());

	return vart_ml_error::SUCCESS;
}

static int rm(const std::string& path_)
{
	if (isFile(path_.c_str(), "r+") or isLink(path_.c_str()))
		return rmFile(path_);
	else if (isDirectory(path_.c_str()))
		return rmDir(path_);
	else
		return vart_ml_log_err_msg(
		    vart_ml_error::SYSTEM_ERROR_INVALID_PATH, "Invalid path \"%s\".\n", path_.c_str());
}

static std::string removeTilde(const std::string& path)
{
	if (path[0] == '~')
	{
		if (path[1] == '/')
			return std::string(getenv("HOME")) + "/" + path.substr(2);
		else
			return std::string("/home/") + path.substr(1);
	}
	else
		return path;
}

static bool endsWith(const std::string& data, const std::string& end)
{
	if (data.size() >= end.size() && data.substr(data.size() - end.size()) == end)
		return true;
	return false;
}

static bool fileOlderThan(const std::string& filePath, size_t days)
{
	struct stat t_stat = {};

	stat(filePath.c_str(), &t_stat);
	time_t now     = time(nullptr);
	double seconds = difftime(now, t_stat.st_mtime);
	if (seconds > (days * 86400)) // NB_SECONDS_PER_DAY
		return true;
	return false;
}

static void deleteOldFiles(const std::string& dir, size_t days, const std::string& extension)
{
	std::vector<std::string> files = getFiles(dir);
	for (const auto& file : files)
	{
		if (endsWith(file, extension) && fileOlderThan(file, days))
			remove(file.c_str());
	}
}

static int writeTxtFile(const std::string& filename, const std::string& str, std::ofstream::openmode mode)
{
	const auto directory  = extractDirname(filename);
	auto       dirCreated = false;
	if (!isDirectory(directory.c_str()))
	{
		dirCreated = recmkdir(directory, 0777) == 0;
	}

	std::ofstream file(filename, mode);
	if (!file.is_open())
	{
		int e = errno;
		vart_ml_log(LOG_ERR, "Cannot open file \"%s\". %s.\n", filename.c_str(), strerror(e));
		if (dirCreated)
		{
			int err = rm(directory);
			if (err)
				return err;
		}
		return vart_ml_log_err(vart_ml_error::FILE_ACCESS_OPEN_FAILURE);
	}

	file << str;
	file.close();
	return vart_ml_error::SUCCESS;
}

static std::string
replace(const std::string& input_, const std::string& pattern_, const std::string& replacement_)
{
	std::string  result{ input_ };
	size_t       start_idx{ 0 };
	const size_t pattern_size{ pattern_.size() };
	const size_t replacement_size{ replacement_.size() };
	while ((start_idx = result.find(pattern_, start_idx)) != std::string::npos)
	{
		result.replace(start_idx, pattern_size, replacement_);
		start_idx += replacement_size;
	}
	return result;
}

static pthread_mutex_t mx_localtime_r = PTHREAD_MUTEX_INITIALIZER;

struct tm* lg_localtime_r(const time_t* timer, struct tm* result)
{
	struct tm* r = nullptr;
	if (pthread_mutex_lock(&mx_localtime_r) == 0)
	{
		r = std::localtime(timer);
		if (r != nullptr)
		{
			*result = *r;
			r       = result;
		}
		if (pthread_mutex_unlock(&mx_localtime_r) != 0)
			r = nullptr;
	}
	return r;
}

std::string getFormattedCurrentTime(const std::string& format_)
{
	using namespace std::string_literals;
	auto        tm_info      = tm{};
	static auto ms_specifier = "%ms"s;

	const auto now          = std::chrono::system_clock::now();
	const auto current_time = std::chrono::system_clock::to_time_t(now);

	auto tm = lg_localtime_r(&current_time, &tm_info);

	using namespace std::chrono_literals;
	const auto milli_sec = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1s;
	std::ostringstream ms;
	ms << std::setfill('0') << std::setw(3) << milli_sec.count();

	auto format = replace(format_, ms_specifier, ms.str());

	std::ostringstream oss;
	oss << std::put_time(tm, format.c_str());

	return oss.str();
}

EmbeddedReporting::EmbeddedReporting()
{
	const char* info;
	int         err;
	err = get_user_config("reporting.enable", &info);
	if (err)
		throw std::runtime_error(vart_ml_error::exception_message(err));

	if (info != NULL && strcmp(info, "true") == 0)
		_enable = true;

	err = get_user_config("reporting.createSymLink", &info);
	if (err)
		throw std::runtime_error(vart_ml_error::exception_message(err));

	if (info != NULL && strcmp(info, "false") == 0)
		_createSymLink = false;

	if (_enable == true)
	{
		std::string directory;
		err = get_user_config("reporting.directory", &info);
		if (err)
			throw std::runtime_error(vart_ml_error::exception_message(err));

		if (info != NULL)
			directory = removeTilde(info);
		else
			directory = removeTilde("~/.amd/vaisw/reporting");

		std::string currentTime = getFormattedCurrentTime("%Y%m%d-%H%M%S%ms");

		int days;
		err = get_user_config("reporting.expire", &info);
		if (err)
			throw std::runtime_error(vart_ml_error::exception_message(err));

		if (info != NULL)
			days = parse_uint(info);
		else
			days = 30;

		if (access(directory.c_str(), R_OK) == 0)
			deleteOldFiles(directory, days, ".json");
		recmkdir(directory, 0777);

		_file = directory + "/reporting" + "." + currentTime + std::to_string(getpid()) + ".json";

		err = get_user_config("reporting.symLinkName", &info);
		if (err)
			throw std::runtime_error(vart_ml_error::exception_message(err));

		if (info != NULL)
			_symlink = info;

		_fallbacks = json::array();
		// we don't name it "fallbacks" inside the json because reporting.json can be opened by customers
		addData("execution info", _fallbacks);
	}
}

EmbeddedReporting::~EmbeddedReporting()
{
	/* TODO: handle the potential failure of write(). */
	if (_enable == true)
		this->write();
}

EmbeddedReporting& EmbeddedReporting::getInstance()
{
	static EmbeddedReporting instance;
	return instance;
}

void EmbeddedReporting::addFallback(const std::string& s) { _fallbacks.push_back(json::string_t(s)); }

bool EmbeddedReporting::isEnabled() { return (_enable); }

int EmbeddedReporting::write()
{
	if (_enable == true)
	{
		vart_ml_log(LOG_INFO, "Create reporting file %s\n", _file.c_str());

		int err = writeTxtFile(_file, _root.dump(1, '\t'), std::ofstream::out | std::ofstream::trunc);
		if (err)
			return err;

		unlink(_symlink.c_str());

		if (_createSymLink == true && symlink(_file.c_str(), _symlink.c_str()) == -1)
			return vart_ml_log_err_msg(vart_ml_error::SYSTEM_ERROR_SYMLINK_CREATION_FAILURE,
			                           "Cannot create symlink %s to %s.\n",
			                           _symlink.c_str(),
			                           _file.c_str());

		return vart_ml_error::SUCCESS;
	}

	return vart_ml_log_err_msg(vart_ml_error::CONFIG_REPORTING_DISABLED,
	                           "Reporting is disabled, %s should not be called.\n",
	                           __func__);
}

int EmbeddedReporting::addData(const std::string& key_, ordered_json value_)
{
	if (_enable == true)
	{
		std::lock_guard<std::mutex> lock(_mtx);
		_root[key_] = value_;
		return vart_ml_error::SUCCESS;
	}

	return vart_ml_log_err_msg(vart_ml_error::CONFIG_REPORTING_DISABLED,
	                           "Reporting is disabled, %s should not be called.\n",
	                           __func__);
}

int EmbeddedReporting::removeData(const std::string& key_)
{
	if (_enable == true)
	{
		std::lock_guard<std::mutex> lock(_mtx);
		_root.erase(key_);
		return vart_ml_error::SUCCESS;
	}

	return vart_ml_log_err_msg(vart_ml_error::CONFIG_REPORTING_DISABLED,
	                           "Reporting is disabled, %s should not be called.\n",
	                           __func__);
}

int EmbeddedReporting::updateData(const std::string& key_, ordered_json value_)
{
	if (_enable == true)
	{
		std::lock_guard<std::mutex> lock(_mtx);
		if (_root[key_].is_number())
			_root[key_] = value_;
		else
			_root[key_].update(value_);

		return vart_ml_error::SUCCESS;
	}

	return vart_ml_log_err_msg(vart_ml_error::CONFIG_REPORTING_DISABLED,
	                           "Reporting is disabled, %s should not be called.\n",
	                           __func__);
}

int EmbeddedReporting::isValueExist(const std::string& key_, bool& is_found)
{
	if (_enable)
	{
		std::lock_guard<std::mutex> lock(_mtx);
		is_found = (_root.count(key_) > 0);
		return vart_ml_error::SUCCESS;
	}

	return vart_ml_log_err_msg(vart_ml_error::CONFIG_REPORTING_DISABLED,
	                           "Reporting is disabled, %s should not be called.\n",
	                           __func__);
}
