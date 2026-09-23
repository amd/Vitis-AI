/**
 * @file vcd_stats.h
 *
 * @copyright Copyright 2025 Advanced Micro Devices Inc.
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

#ifndef VCD_STATS_H
#define VCD_STATS_H

#define VCD_GLOBAL_ID 0

enum vcd_event
{
#define EVENT(event) event,
#include "vcd_event.inc"
#undef EVENT
};

struct vcd_context
{
	int8_t network_id;
	size_t thread_id;
};

/**
 * @brief Register a new network for VCD logging.
 *
 * @param std::string The name of the network.
 * @return int8_t The id of the registered network or -1 if the function failed.
 */
int8_t vcd_register_network(std::string network_name);

/**
 * @brief Register a new global event (not linked to any network).
 *
 * @param std::string The name of the event.
 * @return int8_t The id of the registered event, -1 if the function failed or VCD stats are not enabled.
 */
int8_t vcd_register_global_event(const std::string& event_name);

/**
 * @brief Register a new event for all networks.
 *
 * @details This event will be added for all registered networks. Register all networks before registering new
 * events.
 *
 * @param std::string The name of the event.
 * @return int8_t The id of the registered event, -1 if the function failed or VCD stats are not enabled.
 */
int8_t vcd_register_event(const std::string& event_name);

/**
 * @brief Set the number of threads vart_ml_runner may run simultaneously.
 *
 * @param size_t The number of threads to set.
 */
void vcd_set_nb_threads(size_t nb_threads);

/**
 * @brief Log a new event for VCD logging.
 *
 * @param struct vcd_context Context of the VCD event.
 * @param int8_t The id of the event to log.
 * @param bool The state of the event, true if event is ongoing, or false if the event has ended.
 */
void vcd_event(const struct vcd_context& vcd_context, int8_t event_id, bool state);

#endif
