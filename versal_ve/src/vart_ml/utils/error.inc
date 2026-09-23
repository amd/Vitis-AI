/**
 * @file error.inc
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

ERROR(SUCCESS                                , "Operation success.")                                            ,
ERROR(TEST_FAILURE                           , "Test failure.")                                                 ,
/* FILE ACCESS ERRORS */
ERROR(FILE_ACCESS_OPEN_FAILURE               , "Failed to open file.")                                          ,
ERROR(FILE_ACCESS_CLOSE_FAILURE              , "Failed to close file.")                                         ,
ERROR(FILE_ACCESS_DELETE_FAILURE             , "Failed to delete/unlink file.")                                 ,
ERROR(FILE_ACCESS_READ_FAILURE               , "Failed to read file.")                                          ,
ERROR(FILE_ACCESS_REFERENCE_FAILURE          , "Failed to reference file.")                                     ,
ERROR(FILE_ACCESS_NOT_FOUND                  , "File not found.")                                               ,
ERROR(FILE_ACCESS_WORD_EXP_FAILURE           , "Failed to expand expression with wordexp().")                   ,
/* LICENSE ERRORS */
ERROR(LICENSE_INVALID                        , "Invalid license.")                                              ,
/* SYSTEM ERRORS */
ERROR(SYSTEM_ERROR_ACCESS                    , "Can not modify runner while jobs are running.")                 ,
ERROR(SYSTEM_ERROR_JOB_NOT_FOUND             , "Could not find job slot.")                                      ,
ERROR(SYSTEM_ERROR_MEM_ALLOC_FAILURE         , "Memory allocation failure.")                                    ,
ERROR(SYSTEM_ERROR_MMAP_FAILURE              , "Memory-map operation failed.")                                  ,
ERROR(SYSTEM_ERROR_UNMAP_FAILURE             , "Memory unmap operation failed.")                                ,
ERROR(SYSTEM_ERROR_INVALID_ARG               , "Invalid argument.")                                             ,
ERROR(SYSTEM_ERROR_INVALID_PATH              , "Invalid path.")                                                 ,
ERROR(SYSTEM_ERROR_OPERATION_FAILURE         , "System operation failure.")                                     ,
ERROR(SYSTEM_ERROR_VART_ML_VERSION           , "VART ML version issue.")                                        ,
ERROR(SYSTEM_ERROR_SYMLINK_CREATION_FAILURE  , "Failed to create symbolic link.")                               ,
ERROR(SYSTEM_ERROR_UDEV_CREATION_FAILURE     , "Udev creation failure.")                                        ,
ERROR(SYSTEM_ERROR_UDEV_GET_ATTR_FAILURE     , "Failed to retrieve udev attribute.")                            ,
/* SYSTEM CONFIGURATION ERRORS */
ERROR(SYSTEM_CONFIG_BAD_PAGE_SIZE            , "_SC_PAGESIZE is undefined.")                                    ,
/* CONFIGURATION ERRORS */
ERROR(CONFIG_BAD_ARGUMENT                    , "Unrecognized argument.")                                        ,
ERROR(CONFIG_INVALID_QUANTIZATION_TYPE       , "Invalid (un)quantization data type.")                           ,
ERROR(CONFIG_IRIZ_MALFORMED                  , "Iriz graph malformation issue detected.")                       ,
ERROR(CONFIG_MISSING_FPGA_INFO               , "Failed to access fpga info since empty.")                       ,
ERROR(CONFIG_MISSING_PERIPHERALS             , "Failed to access peripherals since not connected.")             ,
ERROR(CONFIG_MISSING_SNAPSHOT_DIR            , "Snapshot directory is not set in environmental variable.")      ,
ERROR(CONFIG_MISSING_VAISW_INSTALL_DIR       , "VAISW install directory is not set in environmental variable.") ,
ERROR(CONFIG_REPORTING_DISABLED              , "Reporting is disabled.")                                        ,
ERROR(CONFIG_SNAPSHOT_MALFORMED              , "Snapshot malformation issue detected.")                         ,
ERROR(CONFIG_SNAPSHOT_MISMATCH               , "Mismatch between snapshot and available IPs.")                  ,
ERROR(CONFIG_TIMESTAMP_MATCH_NOT_FOUND       , "Failed to find an IP with matching timestamp.")                 ,
ERROR(CONFIG_UNEXPECTED_ARCH_FAMILY          , "Unexpected board architecture family.")                         ,
ERROR(CONFIG_UNEXPECTED_ARG_VALUE            , "Unexpected configuration argument.")                            ,
ERROR(CONFIG_UNEXPECTED_FPGA_FAMILY          , "Unexpected FPGA family.")                                       ,
ERROR(CONFIG_UNKNOWN_SHAPE_TYPE              , "Unknown shape type.")                                           ,
ERROR(CONFIG_UNSUPPORTED_DIMENSION           , "Unsupported input/output dimension.")                           ,
ERROR(CONFIG_UNSUPPORTED_FEATURE             , "Unsupported feature.")                                          ,
ERROR(CONFIG_UNSUPPORTED_MULTI_BOARD         , "The embedded software stack only supports one board.")          ,
ERROR(CONFIG_UNSUPPORTED_SNAPSHOT_LAYOUT     , "Unsupported snapshot layout.")                                  ,
ERROR(CONFIG_UNSUPPORTED_NPU_TENSOR_INFO     , "Invalid NpuTensorInfo provided.")                               ,
/* DEVICE ERRORS */
ERROR(DEVICE_ARG_NOT_FOUND_IN_DEV_TREE       , "Argument not found in device tree.")                            ,
ERROR(DEVICE_BAD_ADDR_ALIGNMENT              , "Bad address alignment.")                                        ,
ERROR(DEVICE_BAD_ARG                         , "Bad argument.")                                                 ,
ERROR(DEVICE_BAD_DDR_CLK_FREQ                , "Bad DDR block frequency.")                                      ,
ERROR(DEVICE_BAD_DDR_INDEX                   , "The DDR index is higher than the number of DDR.")               ,
ERROR(DEVICE_BAD_DDR_LEN                     , "Bad DDR length.")                                               ,
ERROR(DEVICE_BAD_CORE_STATUS                 , "Core status check failure.")                                    ,
ERROR(DEVICE_BAD_TIMESTAMP                   , "Bad timestamp.")                                                ,
ERROR(DEVICE_CLK_FREQ_MEASURE_FAILURE        , "Failed to measure clock frequency.")                            ,
ERROR(DEVICE_CLK_STOP_FAILURE                , "Failed to stop device clock.")                                  ,
ERROR(DEVICE_DDR_FREE_FAILURE                , "DDR memory free failure.")                                      ,
ERROR(DEVICE_DDR_MALLOC_FAILURE              , "DDR memory allocation failure.")                                ,
ERROR(DEVICE_DDR_READ_TIMEOUT                , "DDR read attempt timed-out.")                                   ,
ERROR(DEVICE_DDR_RW_FAILURE                  , "DDR read/write failure.")                                       ,
ERROR(DEVICE_DEV_ACCESS_FAILURE              , "Failed to access device.")                                      ,
ERROR(DEVICE_DEV_MEM_READ_FAILURE            , "Failed to read device memory.")                                 ,
ERROR(DEVICE_DEV_CHECK_FAILURE               , "Device check failure.")                                         ,
ERROR(DEVICE_FILE_DESC_CLOSE_FAILURE         , "Failed to close device file.")                                  ,
ERROR(DEVICE_INITIALIZATION_FAILURE          , "Device initialization failure.")                                ,
ERROR(DEVICE_INFERENCE_TIMEOUT               , "Inference timeout.")                                            ,
ERROR(DEVICE_INFERENCE_MUTEX_RELEASE_FAILURE , "Failed to release inference queue mutex.")                      ,
ERROR(DEVICE_JOB_EXCEPTION                   , "An exception was raised by a running job.")                     ,
ERROR(DEVICE_MMCM_CONFIG_TIMEOUT             , "MMCM configuration timeout.")                                   ,
ERROR(DEVICE_MUTEX_ACQUISITION_FAILURE       , "CTRLBus mutex acquisition failure.")                            ,
ERROR(DEVICE_NOC_RW_FAILURE                  , "Failed to perform read/write operation on NOC.")                ,
ERROR(DEVICE_OVERHEAT_ALERT                  , "Device overheat detected.")                                     ,
ERROR(DEVICE_READ_TIMEOUT                    , "Device read timeout.")                                          ,
ERROR(DEVICE_RW_TEST_FAILURE                 , "Device read/write failure.")                                    ,
ERROR(DEVICE_SYSTEM_CHECK_FAILURE            , "Device system check failure.")                                  ,
ERROR(DEVICE_TEMP_READ_FAILURE               , "Failed to retrieve device temperature.")                        ,
ERROR(DEVICE_UNEXPECTED_ARG_VALUE            , "Unexpected argument.")                                          ,
ERROR(DEVICE_UNSUPPORTED_ARCH                , "Unsupported board architecture.")                               ,
ERROR(DEVICE_UNSUPPORTED_FEATURE             , "Unsupported feature.")                                          ,
/* TOOLS ERRORS */
ERROR(TOOLS_BAD_ARG                          , "Bad argument passed to tool.")                                  ,
ERROR(TOOLS_BAD_USAGE                        , "Bad usage of tool.")                                            ,
/* LIBRARY ERRORS */
ERROR(LIBRARY_ERROR_ONNXRT                   , "OnnxRuntime specific error.")                                   ,
ERROR(LIBRARY_MISSING                        , "Missing external library.")                                     ,
ERROR(LIBRARY_IO_MISSING                     , "Missing internal io library.")                                  ,
/* SERVER ERRORS */
ERROR(DATA_RECEIVED_ERROR                    , "Error while receiving data")                                    ,
ERROR(CONNEXION_CLOSED                       , "Connexion closed by the client")                                ,
ERROR(SEND_FAILED                            , "Send to the socket failed")                                     ,
ERROR(BAD_ACCESS_TYPE                        , "Unknown access type")                                           ,
ERROR(BAD_ADDRESS                            , "Invalid address")                                               ,
/* XRT ERRORS */
ERROR(XCLBIN_FILE_ERROR                      , "Error while accessing the xclbin file.")                        ,
ERROR(XRT_INTERRUPT_ERROR                    , "issue with XRT interrupt.")                                     ,
