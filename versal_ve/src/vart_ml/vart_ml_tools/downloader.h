/**
 * @file downloader.h
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

#ifndef DOWNLOADER_H
#define DOWNLOADER_H

/**
 * @brief Download DDR data into a specific file.
 *
 * @param argv Address, size and file path.
 * @return int The Vart ML error (vart_ml_error_id) code of the operation.
 */
int ddr_download(int argc, const char** argv);

#endif
