
# Copyright (C) 2024 Advanced Micro Devices, Inc.

INSTALL_DIR=VAI_NPU_SW

npu_sw=vitis_ai_V6.3_VAI_NPU_SW.tgz

LIST_FILE=.list
DEP_FILE=.dep.$(npu_sw)

all: links $(DEP_FILE)

links:
	@if [ -f $(INSTALL_DIR)/.link ] ;\
	then \
	  ../bin/download.bash $(npu_sw) -n > $(INSTALL_DIR)/.source ;\
	  diff -q $(INSTALL_DIR)/.link $(INSTALL_DIR)/.source  &>/dev/null || echo "WARNING: downloaded SW stack is not the latest version, run make clean -C $$PWD" ;\
	fi

$(DEP_FILE):
	@rm -f .dep.*
	../bin/download.bash $(npu_sw)
	@rm -rf $(INSTALL_DIR)
	@mkdir $(INSTALL_DIR)
	@tar xzf $(npu_sw) --strip-components 1 -C $(INSTALL_DIR)
	@../bin/download.bash $(npu_sw) -n > $(INSTALL_DIR)/.link
	@echo $(npu_sw) >> $(LIST_FILE)
	@touch $(DEP_FILE)
	@echo "Download and installation of $(INSTALL_DIR) completed."


.PHONY: clean
clean:
	@$(RM) -r $(INSTALL_DIR)
	@-[ -f "$(LIST_FILE)" ] && $(RM) -f -r $$( cat $(LIST_FILE) ) ; true
	@$(RM) $(LIST_FILE) .dep.*

