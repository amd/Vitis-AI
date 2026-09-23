/**
 * @file parsing_combinators.cpp
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

#include <cstdint>
#include <fstream>
#include <iostream>
#include <sstream>

#include "log.h"
#include "parsing_combinators.h"

void drop(std::stringstream& ss, const char* c)
{
	for (const char* cur = c; *cur; cur++)
		ss.get();
}

uint32_t read_uint32_hex(std::stringstream& ss)
{
	uint32_t val = 0;
	char     tmp[8];

	ss.read(tmp, 8);

	for (unsigned int i = 0; i < 8; i++)
	{
		val <<= 4;
		if (!isxdigit(tmp[i]) && tmp[i] == 'X')
			tmp[i] = '0';
		else if (!isxdigit(tmp[i]))
			throw std::range_error("Only hex digits are allowed.");

		if (isdigit(tmp[i]))
			val += (uint32_t)(tmp[i] - '0');
		else
			val += (uint32_t)(tolower(tmp[i]) - 'a' + 10);
	}

	return val;
}

uint8_t read_uint8_hex(std::stringstream& ss)
{
	uint8_t val = 0;
	char    tmp[2];

	ss.read(tmp, 2);

	for (unsigned int i = 0; i < 2; i++)
	{
		val <<= 4;
		if (!isxdigit(tmp[i]) && tmp[i] == 'X')
			tmp[i] = '0';
		else if (!isxdigit(tmp[i]))
			throw std::range_error("Only hex digits are allowed.");

		if (isdigit(tmp[i]))
			val += (uint8_t)(tmp[i] - '0');
		else
			val += (uint8_t)(tolower(tmp[i]) - 'a' + 10);
	}

	return val;
}

void read_uint512_t_hex(std::stringstream& ss, uint32_t* buf)
{
	for (int i = 15; i >= 0; i--)
		buf[i] = read_uint32_hex(ss);
}

void read_uint512_t_hex(std::stringstream& ss, uint8_t* buf)
{
	for (int i = 63; i >= 0; i--)
		buf[i] = read_uint8_hex(ss);
}

int readBinFile(const std::string& filename_, std::vector<float>& data_)
{
	std::streamsize sz = 0;

	std::ifstream file(filename_);
	if (!file.is_open())
		return vart_ml_log_err_msg(
		    vart_ml_error::FILE_ACCESS_OPEN_FAILURE, "Failed opening file %s.\n", filename_.c_str());

	file.seekg(0, file.end);
	sz = file.tellg();
	file.seekg(0, file.beg);

	// Numpy format check
	const std::string numpy_magic       = "\x93NUMPY";
	constexpr ssize_t numpy_header_size = 128;
	std::string       str(numpy_magic.size(), '\0');
	if (sz >= numpy_header_size)
		file.read(&str[0], (ssize_t)numpy_magic.size());
	if (str == numpy_magic)
	{
		sz -= numpy_header_size;
		file.seekg(numpy_header_size, file.beg);
	}
	else
		file.seekg(0, file.beg);

	data_.resize((size_t)sz / sizeof(float));

	file.read((char*)(data_.data()), sz);

	file.close();

	return vart_ml_error::SUCCESS;
}

int readBinFile(const std::string& filename_, std::vector<uint8_t>& data_)
{
	std::streamsize sz = 0;

	std::ifstream file(filename_);
	if (!file.is_open())
		return vart_ml_log_err_msg(
		    vart_ml_error::FILE_ACCESS_OPEN_FAILURE, "Failed opening file %s.\n", filename_.c_str());

	file.seekg(0, file.end);
	sz = file.tellg();
	file.seekg(0, file.beg);

	// Numpy format check
	const std::string numpy_magic       = "\x93NUMPY";
	constexpr ssize_t numpy_header_size = 128;
	std::string       str(numpy_magic.size(), '\0');
	if (sz >= numpy_header_size)
		file.read(&str[0], (ssize_t)numpy_magic.size());
	if (str == numpy_magic)
	{
		sz -= numpy_header_size;
		file.seekg(numpy_header_size, file.beg);
	}
	else
		file.seekg(0, file.beg);

	data_.resize((size_t)sz / sizeof(uint8_t));

	file.read((char*)(data_.data()), sz);

	file.close();

	return vart_ml_error::SUCCESS;
}
