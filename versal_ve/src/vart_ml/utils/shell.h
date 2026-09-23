/* Copyright(C) 2023-2025 Advanced Micro Devices Inc.  All Rights Reserved. */

#ifndef SHELL_H
#define SHELL_H

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>

// Copied from sw/rt/libTools/libGeneric/Functions.h

template <typename Callback>
std::optional<int> runCommand(const std::string& cmd, Callback stdout_callback);
std::optional<int> runCommand(const std::string& cmd);

template <typename Callback>
std::optional<int> runCommand(const std::string& cmd, [[maybe_unused]] Callback callback)
{
	FILE* fp = nullptr;
	errno    = 0;
	if ((fp = popen(cmd.c_str(), "r")) == nullptr)
		throw std::runtime_error("Cannot pipe stream from process \"" + cmd + "\".");

	// initialized to null to let getline allocate.
	char*  line       = nullptr;
	size_t lineLength = 0;
	while (getline(&line, &lineLength, fp) != -1)
	{
		if constexpr (not std::is_null_pointer_v<Callback>)
			callback(line);
	}
	free(line);

	int status = pclose(fp);
	return WEXITSTATUS(status);
}

// Auto detect hex starting with 0x when base == 0
uint64_t parse_uint(const std::string& num, int base = 0);
uint64_t parse_uint_c(const char* str, char** end, int base = 0);
int64_t  parse_int_c(const char* str, char** end, int base = 0);

#endif // SHELL_H
