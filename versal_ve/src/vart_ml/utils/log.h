/**
 * @file log.h
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

#ifndef LOG_H
#define LOG_H

#include "error.h"

#ifdef __cplusplus
extern "C"
{
#endif
	typedef enum
	{
		LOG_DBG,
		LOG_INFO,
		LOG_WARN,
		LOG_ERR,
	} vart_ml_log_level;

	/**
	 * @brief Set log level.
	 *
	 * @param lvl New log level.
	 */
	void set_log_lvl(vart_ml_log_level lvl);

	/**
	 * @brief Set log level.
	 *
	 * If the level received is unknown, the current level is not modified.
	 *
	 * @param lvl New log level.
	 */
	void set_log_lvl_from_str(const char* lvl);

	/**
	 * @brief Returns the current log level.
	 */
	vart_ml_log_level get_log_lvl(void);

	/**
	 * System logging function.
	 *
	 * @param lvl Logging level to be associated with the message.
	 * @param fmt C string that contains a format string that follows the same specifications as format in
	 * printf.
	 */
	void vart_ml_log(vart_ml_log_level lvl, const char* fmt, ...);

#ifdef __cplusplus
	using namespace vart_ml_error;
#endif
	/**
	 * System error logging function that only prints the generic error message.
	 *
	 * @param err The id of the Vart ML error to be reported.
	 */
	int vart_ml_log_err(enum vart_ml_error_id err);

	/**
	 * System error logging function. Calls vart_ml_log().
	 *
	 * @param err The id of the Vart ML error to be reported.
	 * @param fmt C string that contains a format string that follows the same specifications as format in
	 * printf.
	 */
	int vart_ml_log_err_msg(enum vart_ml_error_id err, const char* fmt, ...);

#ifdef __cplusplus
}
#endif

#endif
