/**
 * @file ctrlbus_poke.h
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

#ifndef CTRLBUS_POKE_H
#define CTRLBUS_POKE_H

/**
 * @brief Read or Write in the Service Bus.
 *
 * @param argc Read or Write.
 * @param argv For write, new value.
 * @return int The Vart ML error (vart_ml_error_id) code of the operation.
 */
int ctrlbus_poke(int argc, const char** argv);

/**
 * @brief Read the FPGA timestamp.
 *
 * @return int The Vart ML error (vart_ml_error_id) code of the operation.
 */
int timestamp(int argc, const char** argv);

/**
 * @brief Display the temperature.
 *
 * It displays the current temperature of the FPGA, the maximum temperature since last power on and the
 * maximum temperature allowed.
 *
 * The time interval between two display and a timeout can be configured.
 *
 * @param argv Set time interval and timeout.
 * @return int The Vart ML error (vart_ml_error_id) code of the operation.
 */
int temperature(int argc, const char** argv);

/**
 * @brief Read or Write the maximum allowed temperature.
 *
 * @param argc Read or Write.
 * @param argv For write, new value.
 * @return int The Vart ML error (vart_ml_error_id) code of the operation.
 */
int overtemp(int argc, const char** argv);

/**
 * @brief Check all cores status.
 *
 * @return int The Vart ML error (vart_ml_error_id) code of the operation.
 */
int check_cores(int argc, const char** argv);

/**
 * @brief Read or Write in the NOC.
 *
 * @param argc Read or Write.
 * @param argv For write, new value.
 * @return int The Vart ML error (vart_ml_error_id) code of the operation.
 */
int noc_poke(int argc, const char** argv);

/**
 * @brief Force reset of CTRLBus mutexes.
 *
 * @return int The Vart ML error (vart_ml_error_id) code of the operation.
 */
int reset_mutex(int argc, const char** argv);

#endif
