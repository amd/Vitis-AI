<!--
Copyright (C) 2025-2026 Advanced Micro Devices, Inc.

Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except in compliance with the License. You may obtain a copy of the License at
http://www.apache.org/licenses/LICENSE-2.0.

Unless required by applicable law or agreed to in writing, software distributed under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the License for the specific language governing permissions and limitations under the License.
-->
## Custom IP Repository

This directory is a placeholder for user-defined Vivado IP cores.

Add your custom IP repositories here before building the hardware platform. `create_platform.tcl` registers this path with Vivado:

```tcl
set_property ip_repo_paths ./custom_ips [current_project]
update_ip_catalog
```

### Adding custom IPs

1. Place each IP repository in its own subdirectory under `custom_ips/`.
2. Ensure each repository contains a valid Vivado IP catalog (for example, a `component.xml` at the repository root).
3. Rebuild the platform with `create_pfm_hw.sh` so Vivado refreshes the IP catalog.

If you do not need custom IPs, leave this directory empty. The platform build will proceed without additional IP cores.
