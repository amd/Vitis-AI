/**
 * @file log.cpp
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

#include <cstdio>
#include <cstring>

#include "io/io.h"
#include "log.h"

static vart_ml_log_level curr_log_lvl = LOG_INFO;

void set_log_lvl(vart_ml_log_level lvl) { curr_log_lvl = lvl; }

void set_log_lvl_from_str(const char* lvl)
{
	if (lvl == NULL)
		return;

	if (strcmp(lvl, "LOG_DBG") == 0)
		curr_log_lvl = LOG_DBG;
	else if (strcmp(lvl, "LOG_INFO") == 0)
		curr_log_lvl = LOG_INFO;
	else if (strcmp(lvl, "LOG_WARN") == 0)
		curr_log_lvl = LOG_WARN;
	else if (strcmp(lvl, "LOG_ERR") == 0)
		curr_log_lvl = LOG_ERR;
}

vart_ml_log_level get_log_lvl() { return curr_log_lvl; }

static void vart_ml_log_helper(vart_ml_log_level lvl, const char* fmt, va_list args)
{
	if (lvl >= curr_log_lvl)
		vart_ml_vprintf(fmt, args);
}

void vart_ml_log(vart_ml_log_level lvl, const char* fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	vart_ml_log_helper(lvl, fmt, args);
	va_end(args);
}

int vart_ml_log_err(vart_ml_error::vart_ml_error_id err)
{
	vart_ml_log(LOG_ERR,
	            "[VART] \033[1;31m[ERROR]\033[0m %s was caught: %s\n",
	            vart_ml_error::get_error_names(err),
	            vart_ml_error::get_error_messages(err));
	return err;
}

int vart_ml_log_err_msg(vart_ml_error::vart_ml_error_id err, const char* fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	vart_ml_log_err(err);
	vart_ml_log_helper(LOG_ERR, fmt, args);
	va_end(args);
	fflush(stdout);
	return err;
}
