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
