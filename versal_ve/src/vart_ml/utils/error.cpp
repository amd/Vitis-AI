/**
 * @file error.cpp
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

#include "error.h"

static const char* vart_ml_error_names[] = {
#define ERROR(err, msg) #err
#include "error.inc"
#undef ERROR
};

static const char* vart_ml_error_messages[] = {
#define ERROR(err, msg) msg
#include "error.inc"
#undef ERROR
};

const char* vart_ml_error::get_error_names(int err) { return vart_ml_error_names[err]; }

const char* vart_ml_error::get_error_messages(int err) { return vart_ml_error_messages[err]; }

std::string vart_ml_error::exception_message(int err)
{
	std::string err_msg = std::string() + vart_ml_error::get_error_names(err) + ": "
	                      + vart_ml_error::get_error_messages(err) + "\n";
	return err_msg;
}
