
# Copyright (C) 2024 Advanced Micro Devices, Inc.

WEIGHT_FILE=resnet34_tf.22.5.nhwc.pb
SAMPLES_DIR=../../samples/samples
VAISW_SNAPSHOT_DIRECTORY?=snapshot
BATCH_SIZE=1
NB_IMAGES=10

SNAPSHOT_LIST_FILE=.snapshot.list

$(VAISW_SNAPSHOT_DIRECTORY): $(WEIGHT_FILE) $(SAMPLES_DIR)
	@VAISW_SNAPSHOT_DIRECTORY=$(VAISW_SNAPSHOT_DIRECTORY) python3 demo_tf2.py $(SAMPLES_DIR)/ssd/images $(BATCH_SIZE) $(NB_IMAGES)
	@echo $(VAISW_SNAPSHOT_DIRECTORY) >> $(SNAPSHOT_LIST_FILE)


$(SAMPLES_DIR):
	@make -C $(dir $(SAMPLES_DIR)) $(notdir $(SAMPLES_DIR))

$(WEIGHT_FILE):
	../../../bin/download.bash tf_mlperf_ssdresnet34_3.5.zip
	@rm -rf tf_mlperf_ssdresnet34_3.5
	@unzip tf_mlperf_ssdresnet34_3.5.zip tf_mlperf_ssdresnet34_3.5/float/resnet34_tf.22.5.nhwc.pb
	@mv tf_mlperf_ssdresnet34_3.5/float/resnet34_tf.22.5.nhwc.pb .
	@rm -rf tf_mlperf_ssdresnet34_3.5 tf_mlperf_ssdresnet34_3.5.zip
	@echo "Download weight file $(WEIGHT_FILE) completed."


.PHONY: clean
clean:
	@$(RM) $(WEIGHT_FILE)
	@-[ -f "$(SNAPSHOT_LIST_FILE)" ] && $(RM) -r $$( cat $(SNAPSHOT_LIST_FILE) ) ; true
	@$(RM) $(SNAPSHOT_LIST_FILE)
	@$(RM) -r __pycache__ output_images
	@$(RM) vaisw_execution.log reporting.json

