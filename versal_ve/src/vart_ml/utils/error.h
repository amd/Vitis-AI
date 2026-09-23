/**
 * @file error.h
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

#ifndef ERROR_H
#define ERROR_H

#ifdef __cplusplus
#include <string>
namespace vart_ml_error
{
#endif

enum vart_ml_error_id
{
#define ERROR(err, msg) err
#include "error.inc"
#undef ERROR
};

#ifdef __cplusplus
/**
 * Retrieve the string name of the Vart ML error based on id.
 *
 * @param err The vart_ml_error_id for which string name is to be returned.
 */
const char* get_error_names(int err);

/**
 * Retrieve the string message of the Vart ML error based on id.
 *
 * @param err The vart_ml_error_id for which string message is to be returned.
 */
const char* get_error_messages(int err);

/**
 * Constructs and returns a string message based on Vart ML error id to be
 * returned alongside an exception.
 *
 * @param err The vart_ml_error_id that caused the exception.
 */
std::string exception_message(int err);

} // vart_ml_error
#endif

#endif
