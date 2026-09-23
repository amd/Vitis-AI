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

#ifndef REPORTING_H
#define REPORTING_H

#include <mutex>
#include <string>

#include "json_parser.h"
using json         = nlohmann::json;
using ordered_json = nlohmann::ordered_json;

class EmbeddedReporting
{
  public:
	static EmbeddedReporting& getInstance();

  private:
	EmbeddedReporting& operator=(const EmbeddedReporting&) = delete;
	EmbeddedReporting(const EmbeddedReporting&)            = delete;

	/**
	 * @brief Construct a new Embedded Reporting object
	 *
	 */
	EmbeddedReporting();

	/**
	 * @brief Destroy the Embedded Reporting object
	 *
	 */
	~EmbeddedReporting();

	/**
	 * @brief Write the reporting Json file.
	 *
	 * @return int The Vart ML error (vart_ml_error_id) code of the operation.
	 */
	int write();

  public:
	/**
	 * @brief Check if the reporting is enable.
	 *
	 * @return bool Return true if the reporting is enable, false otherwise.
	 */
	bool isEnabled();

	/**
	 * @brief Add a new entry to the reporting Json and set its value.
	 *
	 * @param key_ New key to be created in the reporting Json.
	 * @param value_ Value of the new key.
	 * @return int The Vart ML error (vart_ml_error_id) code of the operation.
	 */
	int addData(const std::string& key_, ordered_json value_);

	/**
	 * @brief Remove an entry from the reporting Jsone.
	 *
	 * @param key_ Key to be removed from the reporting Json.
	 * @return int The Vart ML error (vart_ml_error_id) code of the operation.
	 */
	int removeData(const std::string& key_);

	/**
	 * @brief Update an existing entry of the reporting Json with a new value.
	 *
	 * @param key_ Key to be updated in the reporting Json.
	 * @param value_ New value of the key.
	 * @return int The Vart ML error (vart_ml_error_id) code of the operation.
	 */
	int updateData(const std::string& key_, ordered_json value_);

	/**
	 * @brief Check if an entry already exist in the reporting Json.
	 *
	 * @param key_ The key which the existence is being checked in the reporting Json.
	 * @param is_found Holds returned boolean: true if the key is present in the reporting Json, false
	 * otherwise.
	 * @return int The Vart ML error (vart_ml_error_id) code of the operation.
	 */
	int isValueExist(const std::string& key_, bool& is_found);

	// Helper to report fallbacks

	/**
	 * @brief Helper to report fallbacks in the reporting Json.
	 *
	 * @param s Fallback message.
	 */
	void addFallback(const std::string& s);

  private:
	ordered_json  _root;
	json::array_t _fallbacks;
	std::mutex    _mtx;
	std::string   _file;
	std::string   _symlink       = "reporting.json";
	bool          _enable        = false;
	bool          _createSymLink = true;
};

#endif // REPORTING_H
