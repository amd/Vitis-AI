/*
 * Copyright (C) 2024-2026 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software
 * is furnished to do so, subject to the following conditions:
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY
 * KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO
 * EVENT SHALL "AMD" BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT
 * OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE. Except as contained in this notice, the name of the AMD shall
 * not be used in advertising or otherwise to promote the sale, use or other
 * dealings in this Software without prior written authorization from AMD.
 */

/**
 * @file video_input_output.cpp
 * @brief This file contains the implementation of functions related to video
 * input and output.
 *
 * The functions in this file are responsible for reading input data from video
 * sources, dumping video frame data into files, and opening/closing files for
 * processing.
 *
 */

#include <filesystem>
#include "x_plus_ml_app.hpp"

using namespace vart;

namespace fs = std::filesystem;

/**
 *  @brief Dump video frame data into a jpeg image file
 *  @param log_level Application log level
 *  @param fp Output file stream
 *  @param video_frame Video frame to dump
 *  @return true if successful, false otherwise
 */
bool dump_video_frame_as_jpeg(AppLogLevel log_level, ofstream& fp, shared_ptr<vart::VideoFrame> video_frame) {
  const vart::VideoFrameMapInfo* map_info = nullptr;

  if (!fp.is_open()) {
    APP_LOG(AppLogLevel::ERROR, log_level, "Output file stream is not open");
    return false;
  }

  /* Map the video frame memory for reading */
  try {
    map_info = &video_frame->map(vart::DataMapFlags::READ);
  } catch (const exception& e) {
    APP_LOG(AppLogLevel::ERROR, log_level, "Failed to map memory : %s", e.what());
    return false;
  }

  /* Log information about the video frame */
  APP_LOG(AppLogLevel::DEBUG, log_level, "number of planes : %d", map_info->nplanes);
  APP_LOG(AppLogLevel::DEBUG, log_level, "size of the frame : %ld", map_info->size);
  APP_LOG(AppLogLevel::DEBUG, log_level, "color format of the frame : %d", static_cast<int>(map_info->fmt));
  APP_LOG(AppLogLevel::DEBUG, log_level, "width of the frame : %d", map_info->width);
  APP_LOG(AppLogLevel::DEBUG, log_level, "height of the frame : %d", map_info->height);
  APP_LOG(AppLogLevel::DEBUG, log_level, "Stride: %d", map_info->planes[0].stride);

  /* Write JPEG data directly from the mapped plane */
  if (map_info->fmt == vart::VideoFormat::BGR) {
    cv::Mat bgr_image(map_info->height, map_info->width, CV_8UC3, map_info->planes[0].data, map_info->planes[0].stride);

    /* Encode to JPEG in memory */
    std::vector<uchar> jpeg_buffer;
    /* Using imencode instead of imwrite to avoid opening the file twice as the filepointer with jpeg is already open at
     * start of the application and also to avoid potential file access conflicts
     */
    bool success = cv::imencode(".jpg", bgr_image, jpeg_buffer);

    if (!success) {
      APP_LOG(AppLogLevel::ERROR, log_level, "Failed to encode image to JPEG format");
      return false;
    }
    // Write JPEG buffer to file
    fp.write(reinterpret_cast<const char*>(jpeg_buffer.data()), jpeg_buffer.size());

  } else {
    APP_LOG(AppLogLevel::ERROR, log_level, "Unsupported video format for JPEG dump: %d",
            static_cast<int>(map_info->fmt));
    video_frame->unmap();
    return false;
  }

  /* Unmap video frame data */
  video_frame->unmap();
  return true;
}

/**
 *  @brief Dump video frame data into a file
 *  @param log_level Application log level
 *  @param fp Output file stream
 *  @param video_frame Video frame to dump
 *  @return true if successful, false otherwise
 */
bool dump_video_frame(AppLogLevel log_level, ofstream& fp, shared_ptr<vart::VideoFrame> video_frame) {
  const vart::VideoFrameMapInfo* map_info = nullptr;

  if (!fp.is_open()) {
    APP_LOG(AppLogLevel::ERROR, log_level, "Output file stream is not open");
    return false;
  }

  /* Map the video frame memory for reading */
  try {
    map_info = &video_frame->map(vart::DataMapFlags::READ);
  } catch (const exception& e) {
    APP_LOG(AppLogLevel::ERROR, log_level, "Failed to map memory : %s", e.what());
    return false;
  }

  /* Log information about the video frame */
  APP_LOG(AppLogLevel::DEBUG, log_level, "number of planes : %d", map_info->nplanes);
  APP_LOG(AppLogLevel::DEBUG, log_level, "size of the frame : %ld", map_info->size);
  APP_LOG(AppLogLevel::DEBUG, log_level, "color format of the frame : %d", static_cast<int>(map_info->fmt));
  APP_LOG(AppLogLevel::DEBUG, log_level, "width of the frame : %d", map_info->width);
  APP_LOG(AppLogLevel::DEBUG, log_level, "height of the frame : %d", map_info->height);
  APP_LOG(AppLogLevel::DEBUG, log_level, "Stride: %d", map_info->planes[0].stride);

  switch (map_info->fmt) {
    case vart::VideoFormat::RGB:
    case vart::VideoFormat::BGR:
      // 3 bytes per pixel (packed RGB/BGR)
      for (int h = 0; h < map_info->height; h++) {
        uint8_t* src = map_info->planes[0].data + (h * map_info->planes[0].stride);
        fp.write(reinterpret_cast<const char*>(src), (map_info->width * 3));
      }
      break;

    case vart::VideoFormat::BGR_FLOAT:
    case vart::VideoFormat::RGB_FLOAT:
      // 12 bytes per pixel (packed RGB/BGR float: 3 floats per pixel)
      for (int h = 0; h < map_info->height; h++) {
        uint8_t* src = map_info->planes[0].data + (h * map_info->planes[0].stride);
        fp.write(reinterpret_cast<const char*>(src), (map_info->width * 3 * sizeof(float)));
      }
      break;

    case vart::VideoFormat::RGBP:
      // Planar RGB: 1 byte per pixel per plane, dump line by line to skip padding
      for (uint8_t plane = 0; plane < map_info->nplanes; ++plane) {
        for (int h = 0; h < map_info->height; ++h) {
          uint8_t* src = map_info->planes[plane].data + (h * map_info->planes[plane].stride);
          fp.write(reinterpret_cast<const char*>(src), map_info->width);
        }
      }
      break;

    case vart::VideoFormat::RGBP_FLOAT:
      // Planar RGB float: 4 bytes per pixel per plane, dump line by line to skip padding
      for (uint8_t plane = 0; plane < map_info->nplanes; ++plane) {
        for (int h = 0; h < map_info->height; ++h) {
          uint8_t* src = map_info->planes[plane].data + (h * map_info->planes[plane].stride);
          fp.write(reinterpret_cast<const char*>(src), map_info->width * sizeof(float));
        }
      }
      break;

    case vart::VideoFormat::Y_UV8_420:
      // NV12: Y plane followed by interleaved UV plane
      for (int h = 0; h < map_info->height; h++) {
        uint8_t* src = map_info->planes[0].data + (h * map_info->planes[0].stride);
        fp.write(reinterpret_cast<const char*>(src), (map_info->width));
      }
      for (int h = 0; h < map_info->height / 2; h++) {
        uint8_t* src = map_info->planes[1].data + (h * map_info->planes[1].stride);
        fp.write(reinterpret_cast<const char*>(src), (map_info->width));
      }
      break;

    case vart::VideoFormat::RGBP_FP16:
    case vart::VideoFormat::RGBP_BF16:
      // Planar RGB half: 2 bytes per pixel per plane, dump line by line to skip padding
      for (uint8_t plane = 0; plane < map_info->nplanes; ++plane) {
        for (int h = 0; h < map_info->height; ++h) {
          uint8_t* src = map_info->planes[plane].data + (h * map_info->planes[plane].stride);
          fp.write(reinterpret_cast<const char*>(src), map_info->width * sizeof(uint16_t));
        }
      }
      break;

    case vart::VideoFormat::RGB_BF16:
    case vart::VideoFormat::RGB_FP16:
      // 6 bytes per pixel (packed RGB with BF16 or FP16)
      for (int h = 0; h < map_info->height; ++h) {
        uint8_t* src = map_info->planes[0].data + (h * map_info->planes[0].stride);
        fp.write(reinterpret_cast<const char*>(src), map_info->width * 3 * sizeof(uint16_t));
      }
      break;

    default:
      APP_LOG(AppLogLevel::ERROR, log_level, "Unsupported video format: %d", static_cast<int>(map_info->fmt));
      video_frame->unmap();
      return false;
  }

  /* Unmap video frame data */
  video_frame->unmap();
  return true;
}

/*@brief Get the file extension from a given filename
 * @param filename The input filename
 * @return The file extension, or an empty string if not found
 */
static string get_file_extension(const string& filename) {
  size_t dotIndex = filename.find_last_of(".");
  if (dotIndex != string::npos) {
    /* Return the substring after the last dot as the file extension */
    return filename.substr(dotIndex + 1);
  }
  /* Return an empty string if no extension is found */
  return "";
}

/**
 * @brief shape_to_string - Convert a shape vector to a string representation
 * @param shape Vector representing the shape
 * @return String representation of the shape
 */
string shape_to_string(const vector<uint32_t>& shape) {
  string shape_str = "Shape: (";
  for (size_t i = 0; i < shape.size(); ++i) {
    shape_str += to_string(shape[i]);
    if (i < shape.size() - 1) {
      shape_str += ", ";
    }
  }
  shape_str += ")";
  return shape_str;
}

/**
 * @brief Dump float data to a binary file
 * @param filename The name of the output file
 * @param data Pointer to the float data
 * @param size The number of elements to write
 */
void dump_data_to_file(const string& filename, const float* data, size_t size) {
  ofstream file(filename, ios::binary);
  if (file.is_open()) {
    file.write(reinterpret_cast<const char*>(data), size * sizeof(float));
    file.close();
  } else {
    cerr << "Failed to open file " << filename << endl;
  }
}

/**
 * @brief Extract input resolution from the input file and update the AppContext
 * @param ctx Application context
 * @return true if successful, false otherwise
 */
static bool extract_input_resolution(PipelineContext* pipeline,
                                     AppVideoInputFormat& input_fmt,
                                     uint32_t& input_height,
                                     uint32_t& input_width,
                                     AppLogLevel log_level) {
  uint32_t in_frame_width = 0;
  uint32_t in_frame_height = 0;
  size_t in_frame_size = 0;

  /* Get the file extension from the input file path */
  string fileExtension = get_file_extension(pipeline->input_file_path);

  /* Convert the file extension to lowercase for case-insensitive comparison */
  transform(fileExtension.begin(), fileExtension.end(), fileExtension.begin(), ::tolower);

  /* Process based on the file extension */
  if (fileExtension == "jpg" || fileExtension == "jpeg") {
    /* Read image file for capturing properties */
    cv::Mat Frame;
    Frame = cv::imread(pipeline->input_file_path);

    if (Frame.empty()) {
      APP_LOG(AppLogLevel::ERROR, log_level, "Unable to open image file");
      return false;
    }

    /* Get image properties */
    in_frame_width = static_cast<uint32_t>(Frame.cols);
    in_frame_height = static_cast<uint32_t>(Frame.rows);
    in_frame_size = static_cast<size_t>(in_frame_width * in_frame_height * static_cast<uint32_t>(Frame.channels()));
    input_fmt = APP_VIDEO_INPUT_FORMAT_JPEG;
  } else if (fileExtension == "nv12") {
    if (!input_width || !input_height) {
      APP_LOG(AppLogLevel::ERROR, log_level, "For NV12 format, input the width and height using \"--dim\" option");
      cout << "For NV12 format, input the width and height using \"--dim\" option";
      return false;
    }
    input_fmt = APP_VIDEO_INPUT_FORMAT_NV12;
    in_frame_height = input_height;
    in_frame_width = input_width;
    in_frame_size = input_height * input_width * 1.5;
  } else if (fileExtension == "bgr") {
    if (!input_width || !input_height) {
      APP_LOG(AppLogLevel::ERROR, log_level, "For raw format, input the width and height using \"--dim\" option");
      cout << "For raw format, input the width and height using \"--dim\" option";
      return false;
    }
    input_fmt = APP_VIDEO_INPUT_FORMAT_BGR;
    in_frame_height = input_height;
    in_frame_width = input_width;
    in_frame_size = input_height * input_width * 3;
  } else {
    APP_LOG(AppLogLevel::ERROR, log_level, "Unsupported input file format: %s", fileExtension.c_str());
    return false;
  }

  /* Log and update the input resolution in parameters */
  APP_LOG(AppLogLevel::INFO, log_level, "in_frame_width = %d , in_frame_height = %d , in_frame_size = %ld",
          in_frame_width, in_frame_height, in_frame_size);

  input_height = in_frame_height;
  input_width = in_frame_width;

  return true;
}

/**
 * @brief Create output file path based on input format and iteration number
 * @param output_dir_path Output directory path
 * @param pipeline_idx Pipeline index
 * @param input_fmt Input video format
 * @param max_iterations Maximum number of iterations
 * @param iter Iteration number
 * @param log_level Application log level
 * @return Constructed output file path
 */
std::string construct_output_path(const std::string& output_dir_path,
                                  uint32_t pipeline_idx,
                                  AppVideoInputFormat input_fmt,
                                  int64_t max_iterations,
                                  int64_t iter,
                                  AppLogLevel log_level) {
  std::string ext = "";
  std::string output_path = "";
  if (APP_VIDEO_INPUT_FORMAT_BGR == input_fmt) {
    ext = ".bgr";
  } else if (APP_VIDEO_INPUT_FORMAT_NV12 == input_fmt) {
    ext = ".nv12";
  } else if (APP_VIDEO_INPUT_FORMAT_JPEG == input_fmt) {
    ext = ".jpg";
  } else {
    APP_LOG(AppLogLevel::ERROR, log_level, "Unsupported input format for output file construction");
    return output_path;
  }

  output_path = output_dir_path + "/" + (max_iterations > 1 ? ("iter_" + std::to_string(iter) + "_") : "") +
                "postproc" + std::to_string(pipeline_idx) + "_overlay" + ext;

  return output_path;
}

/**
 * @brief close files
 * @param ctx Application context
 * @return void
 */
void close_files(PipelineContext* pipeline, AppLogLevel log_level) {
  (void)log_level;  // Unused parameter
#ifdef DUMP_INPUTS
  /* Close and reset debug-related file streams if they are open */
  if (pipeline->dump_input_fp.is_open()) {
    pipeline->dump_input_fp.close();
  }
  if (pipeline->dump_infer_input_fp.is_open()) {
    pipeline->dump_infer_input_fp.close();
  }
#endif

  /* Close and reset input and output file streams if they are open */
  if (pipeline->input_file.is_open()) {
    pipeline->input_file.close();
  }
  if (pipeline->output_file.is_open()) {
    pipeline->output_file.close();
  }
}

/**
 * @brief open files
 * @param ctx Application context
 * @return False if not successful
 */
bool open_files(PipelineContext* pipeline,
                const string& output_dir_path,
                int64_t max_iteration,
                int64_t iteration_counter,
                AppLogLevel log_level,
                bool dump_all_inputs) {
  (void)output_dir_path;  // Unused parameter
  string file_extension;

  /* Extract input resolution */
  if (extract_input_resolution(pipeline, pipeline->input_fmt, pipeline->input_height, pipeline->input_width,
                               log_level) != true) {
    APP_LOG(AppLogLevel::ERROR, log_level, "Failed to get input resolution");
    goto failure;
  }

  if (pipeline->input_fmt == APP_VIDEO_INPUT_FORMAT_NV12 || pipeline->input_fmt == APP_VIDEO_INPUT_FORMAT_BGR) {
    /* Open input file */
    pipeline->input_file.open(pipeline->input_file_path, ios::binary | ios::in);
    if (!pipeline->input_file.is_open()) {
      APP_LOG(AppLogLevel::ERROR, log_level, "Can't open file: %s", pipeline->input_file_path.c_str());
      goto failure;
    }
  }

  /* Determine output file extension based on input format */
  if (pipeline->input_fmt == APP_VIDEO_INPUT_FORMAT_NV12) {
    file_extension = "nv12";
  } else if (pipeline->input_fmt == APP_VIDEO_INPUT_FORMAT_JPEG || pipeline->input_fmt == APP_VIDEO_INPUT_FORMAT_BGR) {
    file_extension = "bgr";
  } else {
    goto failure;
  }

  /* Create separate output directory for each pipeline */
  if (!pipeline->is_benchmark_enabled) {
    pipeline->output_dir_path = DEFAULT_OUTPUT_DIR;
    std::filesystem::create_directories(pipeline->output_dir_path);

    /* Construct output file path */
    pipeline->out_file_path = construct_output_path(pipeline->output_dir_path, pipeline->pipeline_id,
                                                    pipeline->input_fmt, max_iteration, iteration_counter, log_level);
  }

  /* Open output file */
  if (!pipeline->is_benchmark_enabled && !pipeline->out_file_path.empty()) {
    pipeline->output_file.open(pipeline->out_file_path, ios::binary | ios::out);
    if (!pipeline->output_file.is_open()) {
      APP_LOG(AppLogLevel::ERROR, log_level, "Can't open file: %s", pipeline->out_file_path.c_str());
      goto failure;
    }
  } else {
    APP_LOG(AppLogLevel::INFO, log_level,
            "As the output file is not provided, video frame with overlayed infer "
            "results will not be dummped. You can see the inference results by "
            "enabling output logs by setting the log level to 3.");
  }
#ifdef DUMP_INPUTS
  if (dump_all_inputs) {
    pipeline->dump_input_path = "/tmp/dumped_input_" + to_string(pipeline->input_width) + "_" +
                                to_string(pipeline->input_height) + "." + file_extension;
    pipeline->dump_infer_input_path = "/tmp/dumped_infer_input";

    /* Open debug files */
    pipeline->dump_input_fp.open(pipeline->dump_input_path, ios::binary | ios::out);
    if (!pipeline->dump_input_fp.is_open()) {
      APP_LOG(AppLogLevel::ERROR, log_level, "Can't open file: %s", pipeline->dump_input_path.c_str());
      goto failure;
    }

    pipeline->dump_infer_input_fp.open(pipeline->dump_infer_input_path, ios::binary | ios::out);
    if (!pipeline->dump_infer_input_fp.is_open()) {
      APP_LOG(AppLogLevel::ERROR, log_level, "Can't open file: %s", pipeline->dump_infer_input_path.c_str());
      goto failure;
    }
  }
#endif

  return true;

failure:
  close_files(pipeline, log_level);
  return false;
}

/**
 * @brief Read input data into a video frame
 * @param pipeline Pipeline context containing file handles
 * @param input_fmt Video input format
 * @param input_height Input height
 * @param input_width Input width
 * @param log_level Application log level
 * @param video_frame Video frame to read data into
 * @return APP_READ_SUCCESS on success, APP_EOF on end of file, APP_READ_FAILED
 * on failure
 */
AppReadStatus read_input(PipelineContext* pipeline, AppLogLevel log_level, vart::VideoFrame* video_frame) {
  size_t width, height;
  size_t bytes = 0;
  size_t bytes_to_read = 0;

  /* Map the video frame memory for writing */
  const vart::VideoFrameMapInfo* map_info = nullptr;

  try {
    map_info = &video_frame->map(vart::DataMapFlags::WRITE);
  } catch (const exception& e) {
    APP_LOG(AppLogLevel::ERROR, log_level, "Failed to map memory : %s", e.what());
    return APP_READ_FAILED;
  }

  if (pipeline->input_fmt == APP_VIDEO_INPUT_FORMAT_JPEG) {
    auto read_frame = cv::imread(pipeline->input_file_path);
    if (read_frame.empty()) {
      video_frame->unmap();
      APP_LOG(AppLogLevel::ERROR, log_level, "Unable to open image file");
      return APP_READ_FAILED;
    }
    /* currently copying image into user buffer */
    /* it is not an optimized solution */

    for (int h = 0; h < read_frame.rows; h++) {
      /* cv::imread() is reading image in color, hence the order of data will be
       * BGR in 1 plane */
      uint8_t* dst = map_info->planes[0].data + (h * map_info->planes[0].stride);
      uint8_t* src = read_frame.data + (h * read_frame.cols * 3);
      memcpy(dst, src, (read_frame.cols * 3));
    }

    video_frame->unmap();
    return APP_READ_SUCCESS;
  } else if (pipeline->input_fmt == APP_VIDEO_INPUT_FORMAT_BGR) {
    bytes = 0;
    width = pipeline->input_width;
    height = pipeline->input_height;

    bytes_to_read = width * height * 3;
    uint8_t* data_ptr = map_info->planes[0].data;
    bytes = pipeline->input_file.read(reinterpret_cast<char*>(data_ptr), bytes_to_read).gcount();

    APP_LOG(AppLogLevel::DEBUG, log_level, "Read %lu bytes for plane 0 for BGR", bytes);
    if (bytes != bytes_to_read) {
      if (bytes != 0) {
        APP_LOG(AppLogLevel::WARNING, log_level, "Read less data than expected");
      }
      video_frame->unmap();
      /* Check if the end of the file is reached */
      if (pipeline->input_file.eof()) {
        return APP_EOF;
      } else {
        return APP_READ_FAILED;
      }
    }

    /* Check input format for NV12 */
  } else if (pipeline->input_fmt == APP_VIDEO_INPUT_FORMAT_NV12) {
    bytes = 0;
    width = pipeline->input_width;
    height = pipeline->input_height;

    bytes_to_read = width * height;

    /* read luminance (Y) plane data */
    for (size_t h = 0; h < height; h++) {
      uint8_t* dst = map_info->planes[0].data + (h * map_info->planes[0].stride);
      bytes += pipeline->input_file.read(reinterpret_cast<char*>(dst), map_info->width).gcount();
    }

    APP_LOG(AppLogLevel::DEBUG, log_level, "Read %lu bytes for plane 0 for NV12", bytes);
    if (bytes != bytes_to_read) {
      if (bytes != 0) {
        APP_LOG(AppLogLevel::WARNING, log_level, "Read less data than expected");
      }
      video_frame->unmap();
      /* Check if the end of the file is reached */
      if (pipeline->input_file.eof()) {
        return APP_EOF;
      } else {
        return APP_READ_FAILED;
      }
    }

    bytes_to_read = width * height * 0.5;
    bytes = 0;

    /* Read chrominance (U and V plane interleaved) plane data */
    for (size_t h = 0; h < height / 2; h++) {
      uint8_t* dst = map_info->planes[1].data + (h * map_info->planes[1].stride);
      bytes += pipeline->input_file.read(reinterpret_cast<char*>(dst), map_info->width).gcount();
    }

    APP_LOG(AppLogLevel::DEBUG, log_level, "Read %lu bytes for plane 1 for NV12", bytes);
    if (bytes != bytes_to_read) {
      APP_LOG(AppLogLevel::WARNING, log_level, "Read less data than expected");
      video_frame->unmap();
      /* Check if the end of the file is reached */
      if (pipeline->input_file.eof()) {
        return APP_EOF;
      } else {
        return APP_READ_FAILED;
      }
    }
  }

  /* Unmap the video frame memory */
  video_frame->unmap();
  APP_LOG(AppLogLevel::DEBUG, log_level, "Read data in preprocess input buffer");
  return APP_READ_SUCCESS;
}
