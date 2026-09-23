
# Copyright (C) 2024 Advanced Micro Devices, Inc.

SHELL=bash

# this is the default IP name, this can be overwritten using variable environment
TARGET_NPU?=VE2802_NPU_IP

package_prefix=vitis_ai_V6.3
ip=${package_prefix}_${TARGET_NPU}

IP_LIST_FILE=.np.list


$(TARGET_NPU): $(TARGET_NPU)/.dep.${package_prefix}

$(TARGET_NPU)/.dep.${package_prefix}:
	../bin/download.bash $(ip).tgz
	@rm -rf $(TARGET_NPU)
	@mkdir $(TARGET_NPU)
	@tar xzf $(ip).tgz -C $(TARGET_NPU)
	@[[ "$(TARGET_NPU)" != *NPU_IP* ]] || cp -f $(TARGET_NPU)/fpga_info* fpga_info
	@echo $(TARGET_NPU) >> $(IP_LIST_FILE)
	@../bin/download.bash $(ip).tgz -n > $(TARGET_NPU)/.dep.${package_prefix}
	@echo "Download and installation of $(TARGET_NPU) completed."



.PHONY: clean
clean:
	@$(RM) $(package_prefix)*tgz
	@$(RM) fpga_info/fpga_info*txt
	@-[ -f "$(IP_LIST_FILE)" ] && $(RM) -f -r $$( cat $(IP_LIST_FILE) ) ; true
	@$(RM) $(IP_LIST_FILE)
