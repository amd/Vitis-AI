
# Copyright (C) 2024 Advanced Micro Devices, Inc.

SAMPLES=samples
SAMPLES_TGZ=vitis_ai_library_r1.4.0_images.tar.gz


$(SAMPLES): $(SAMPLES_TGZ)
	@rm -rf $(SAMPLES)
	@tar xzf vitis_ai_library_r1.4.0_images.tar.gz
	@touch $@
	@echo "Download sample images completed."

$(SAMPLES_TGZ):
	@../../bin/download.bash $@

.PHONY: clean
clean:
	@$(RM) -r $(SAMPLES_TGZ) $(SAMPLES)

