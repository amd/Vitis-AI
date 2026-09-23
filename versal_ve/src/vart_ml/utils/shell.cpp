/* Copyright(C) 2023-2025 Advanced Micro Devices Inc.  All Rights Reserved. */

#include "shell.h"

// Copied from sw/rt/libTools/libGeneric/Functions.cpp
std::optional<int> runCommand(const std::string& cmd) { return runCommand(cmd, nullptr); }

// Compatibility for isoc23 on older system
unsigned long long legacy_strtoull(const char* nptr, char** endptr, int base) __asm__("strtoull");
unsigned long long legacy_strtoll(const char* nptr, char** endptr, int base) __asm__("strtoll");

uint64_t parse_uint(const std::string& num, int base) { return legacy_strtoull(num.c_str(), nullptr, base); }
uint64_t parse_uint_c(const char* str, char** end, int base) { return legacy_strtoull(str, end, base); }
int64_t  parse_int_c(const char* str, char** end, int base) { return legacy_strtoll(str, end, base); }
