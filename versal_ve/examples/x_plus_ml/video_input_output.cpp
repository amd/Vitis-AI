/*
 * Copyright (C) 2024 Advanced Micro Devices, Inc.
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
 * EVENT SHALL XILINX BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT
 * OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE. Except as contained in this notice, the name of the Xilinx shall
 * not be used in advertising or otherwise to promote the sale, use or other
 * dealings in this Software without prior written authorization from Xilinx.
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

#include "x_plus_ml_app.hpp"

/* Dump video frame data into a file */
bool dump_video_frame(AppContext *ctx, ofstream &fp,
                      shared_ptr<vart::VideoFrame> video_frame) {
  AppLogLevel log_level = ctx->log_level;

  const vart::VideoFrameMapInfo *map_info = nullptr;

  try {
    map_info = &video_frame->map(vart::DataMapFlags::READ);
  } catch (const exception &e) {
    APP_LOG_MESSAGE(APP_LOG_LEVEL_ERROR, log_level, "Failed to map memory : %s",
                e.what());
    return false;
  }
  /* Map the video frame memory for writing */

  /* Log information about the video frame */
  APP_LOG_MESSAGE(APP_LOG_LEVEL_DEBUG, log_level, "number of planes : %d",
              map_info->nplanes);
  APP_LOG_MESSAGE(APP_LOG_LEVEL_DEBUG, log_level, "size of the frame : %ld",
              map_info->size);
  APP_LOG_MESSAGE(APP_LOG_LEVEL_DEBUG, log_level, "color format of the frame : %d",
              static_cast<int>(map_info->fmt));
  APP_LOG_MESSAGE(APP_LOG_LEVEL_DEBUG, log_level, "width of the frame : %d",
              map_info->width);
  APP_LOG_MESSAGE(APP_LOG_LEVEL_DEBUG, log_level, "height of the frame : %d",
              map_info->height);
  APP_LOG_MESSAGE(APP_LOG_LEVEL_DEBUG, log_level, "Stride: %d",
              map_info->planes[0].stride);

  /* Dumping RGB/BGR data */
  if ((vart::VideoFormat::RGB == map_info->fmt) ||
      (vart::VideoFormat::BGR == map_info->fmt)) {
    for (int h = 0; h < map_info->height; h++) {
      uint8_t *src =
          map_info->planes[0].data + (h * map_info->planes[0].stride);
      fp.write(reinterpret_cast<const char *>(src), (map_info->width * 3));
    }
  }

  /* Dumping RGBx/BGRx data */
  if ((vart::VideoFormat::RGBx == map_info->fmt) ||
      (vart::VideoFormat::BGRx == map_info->fmt)) {
    for (int h = 0; h < map_info->height; h++) {
      uint8_t *src =
        map_info->planes[0].data + (h * map_info->planes[0].stride);
      fp.write(reinterpret_cast<const char *>(src), (map_info->width * 4));
    }
  }

  /* Dumping BGR_FLOAT, BGR in float data */
  if (vart::VideoFormat::BGR_FLOAT == map_info->fmt) {
    uint8_t *src = map_info->planes[0].data;
    fp.write(reinterpret_cast<const char *>(src), (map_info->planes[0].size));
  }

  /* Dumping NV12 data */
  if (vart::VideoFormat::Y_UV8_420 == map_info->fmt) {
    for (int h = 0; h < map_info->height; h++) {
      uint8_t *src =
          map_info->planes[0].data + (h * map_info->planes[0].stride);
      fp.write(reinterpret_cast<const char *>(src), (map_info->width));
    }

    for (int h = 0; h < map_info->height / 2; h++) {
      uint8_t *src =
          map_info->planes[1].data + (h * map_info->planes[1].stride);
      fp.write(reinterpret_cast<const char *>(src), (map_info->width));
    }
  }

  /* Dumping RGBx_BF16/BGRx_BF16 data */
  if ((vart::VideoFormat::RGBx_BF16 == map_info->fmt) ||
      (vart::VideoFormat::BGRx_BF16 == map_info->fmt)) {
    for (int h = 0; h < map_info->height; h++) {
      uint8_t *src =
        map_info->planes[0].data + (h * map_info->planes[0].stride);
      fp.write(reinterpret_cast<const char *>(src), (map_info->width * 4 * 2));
    }
  }

  /* Unmap video frame data */
  video_frame->unmap();
  return true;
}

/* Get the file extension from a given filename */
string get_file_extension(const string &filename) {
  size_t dotIndex = filename.find_last_of(".");
  if (dotIndex != string::npos) {
    /* Return the substring after the last dot as the file extension */
    return filename.substr(dotIndex + 1);
  }
  /* Return an empty string if no extension is found */
  return "";
}

string extract_npz(const string &npz_file, const string &temp_dir) {
  string command = "unzip -o " + npz_file + " -d " + temp_dir;
  int result = system(command.c_str());
  if (result != 0) {
    cerr << "Failed to unzip file: " << npz_file << endl;
    return "";
  }

  /* Assuming there's only one .npy file in npz, find its name */
  string npy_file_path;
  string find_command = "find " + temp_dir + " -name '*.npy'";
  FILE *pipe = popen(find_command.c_str(), "r");
  if (!pipe) {
    cerr << "Failed to run find command" << endl;
    return "";
  }

  char buffer[128];
  while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
    npy_file_path = buffer;
    npy_file_path.erase(npy_file_path.find_last_not_of(" \n\r\t") +
                        1); /* Trim whitespace */
  }
  pclose(pipe);

  return npy_file_path;
}

/* Function to convert shape to string */
string shape_to_string(const vector<uint32_t> &shape) {
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

/* Function to convert NCHW to NHWC for float data with batch dimension */
void nchw_to_nhwc(const float *src, float *dst, uint32_t batch, uint32_t width,
                  uint32_t height, uint32_t channels) {
  for (uint32_t b = 0; b < batch; ++b) {
    for (uint32_t h = 0; h < height; ++h) {
      for (uint32_t w = 0; w < width; ++w) {
        for (uint32_t c = 0; c < channels; ++c) {
          dst[b * height * width * channels + h * width * channels +
              w * channels + c] = src[b * channels * height * width +
                                      c * height * width + h * width + w];
        }
      }
    }
  }
}

/* Function to dump data to a binary file */
void dump_data_to_file(const string &filename, const float *data, size_t size) {
  ofstream file(filename, ios::binary);
  if (file.is_open()) {
    file.write(reinterpret_cast<const char *>(data), size * sizeof(float));
    file.close();
  } else {
    cerr << "Failed to open file " << filename << endl;
  }
}

/*
 * Function to parse .npy file and handle shape comparison and convert float to
 * flaot and conversion
 */
void parse_npy_file(AppContext *ctx, const string &file_path,
                    AppLogLevel log_level, uint32_t *width, uint32_t *height,
                    uint8_t *dst, const vector<uint32_t> &expected_shape) {
  bool shape_matched = true;
  FILE *file = fopen(file_path.c_str(), "rb");
  if (!file) {
    APP_LOG_MESSAGE(APP_LOG_LEVEL_ERROR, log_level, "Failed to open file %s",
                file_path.c_str());
    return;
  }

  /* Read the magic string and version number */
  char magic[6];
  if (fread(magic, 1, 6, file) != 6) {
    APP_LOG_MESSAGE(APP_LOG_LEVEL_ERROR, log_level, "Failed to read magic string");
    fclose(file);
    return;
  }
  if (strncmp(magic, "\x93NUMPY", 6) != 0) {
    APP_LOG_MESSAGE(APP_LOG_LEVEL_ERROR, log_level, "Invalid .npy file");
    fclose(file);
    return;
  }

  uint8_t major_version, minor_version;
  if (fread(&major_version, 1, 1, file) != 1 ||
      fread(&minor_version, 1, 1, file) != 1) {
    APP_LOG_MESSAGE(APP_LOG_LEVEL_ERROR, log_level, "Failed to read version number");
    fclose(file);
    return;
  }
  APP_LOG_MESSAGE(APP_LOG_LEVEL_DEBUG, log_level, "NumPy version %d.%d",
              (int)major_version, (int)minor_version);

  /* Read the header length */
  uint16_t header_len;
  if (fread(&header_len, 2, 1, file) != 1) {
    APP_LOG_MESSAGE(APP_LOG_LEVEL_ERROR, log_level, "Failed to read header length");
    fclose(file);
    return;
  }
  APP_LOG_MESSAGE(APP_LOG_LEVEL_DEBUG, log_level, "Header length: %d", header_len);

  /* Read the header */
  vector<char> header(header_len);
  if (fread(header.data(), 1, header_len, file) != header_len) {
    APP_LOG_MESSAGE(APP_LOG_LEVEL_ERROR, log_level, "Failed to read header");
    fclose(file);
    return;
  }
  string header_str(header.begin(), header.end());
  APP_LOG_MESSAGE(APP_LOG_LEVEL_DEBUG, log_level, "Header: %s", header_str.c_str());

  /* Extract shape from the header */
  regex shape_regex("'shape': \\((\\d+), (\\d+), (\\d+), (\\d+)\\)");
  smatch match;
  vector<uint32_t> parsed_shape;
  if (regex_search(header_str, match, shape_regex)) {
    for (size_t i = 1; i < match.size(); ++i) {
      parsed_shape.push_back(stoi(match[i].str()));
    }
    APP_LOG_MESSAGE(APP_LOG_LEVEL_DEBUG, log_level, "Parsed shape: %s",
                shape_to_string(parsed_shape).c_str());

    /* Determine if the format is NCHW or NHWC */
    if (parsed_shape[0] == 1 && parsed_shape[1] == 3) {
      /* Likely NCHW format */
      *height = parsed_shape[2];
      *width = parsed_shape[3];
    } else if (parsed_shape[0] == 1 && parsed_shape[3] == 3) {
      /* Likely NHWC format */
      *height = parsed_shape[1];
      *width = parsed_shape[2];
    } else {
      APP_LOG_MESSAGE(APP_LOG_LEVEL_ERROR, log_level, "Unexpected shape format");
      fclose(file);
      return;
    }
    APP_LOG_MESSAGE(APP_LOG_LEVEL_DEBUG, log_level, "Width: %d, Height: %d", *width,
                *height);
  } else {
    APP_LOG_MESSAGE(APP_LOG_LEVEL_ERROR, log_level,
                "Failed to parse shape from header");
    fclose(file);
    return;
  }

  /* Read the data type */
  regex dtype_regex("'descr': '([<>=|])([ifucb])(\\d+)'");
  smatch dtype_match;
  if (!regex_search(header_str, dtype_match, dtype_regex)) {
    APP_LOG_MESSAGE(APP_LOG_LEVEL_ERROR, log_level,
                "Failed to parse data type from header");
    fclose(file);
    return;
  }
  char endian = dtype_match[1].str()[0];
  char type = dtype_match[2].str()[0];
  int type_size = stoi(dtype_match[3].str());

  APP_LOG_MESSAGE(APP_LOG_LEVEL_DEBUG, log_level, "Data type: %c%c%d", endian, type,
              type_size);

  if (type != 'f' || (type_size != 4 && type_size != 8)) {
    APP_LOG_MESSAGE(APP_LOG_LEVEL_ERROR, log_level, "Unsupported data type");
    fclose(file);
    return;
  }

  /* Read the data */
  size_t data_size = 1;
  for (auto dim : parsed_shape) {
    data_size *= dim;
  }
  vector<float> data(data_size);
  if (type_size == 8) {
    vector<double> temp_data(data_size);
    APP_LOG_MESSAGE(APP_LOG_LEVEL_DEBUG, log_level, "Convert double to float");
    if (fread(temp_data.data(), sizeof(double), data_size, file) != data_size) {
      APP_LOG_MESSAGE(APP_LOG_LEVEL_ERROR, log_level, "Failed to read data");
      fclose(file);
      return;
    }
    for (size_t i = 0; i < data_size; ++i) {
      data[i] = static_cast<float>(temp_data[i]);
    }
  } else {
    APP_LOG_MESSAGE(APP_LOG_LEVEL_DEBUG, log_level, "Convert float to float");
    if (fread(data.data(), sizeof(float), data_size, file) != data_size) {
      APP_LOG_MESSAGE(APP_LOG_LEVEL_ERROR, log_level, "Failed to read data");
      fclose(file);
      return;
    }
  }
  fclose(file);
  APP_LOG_MESSAGE(APP_LOG_LEVEL_DEBUG, log_level, "Data size: %ld", data.size());

  /* Dump initial data to file */
  if (ctx->dump_all_inputs)
    dump_data_to_file("/tmp/input.1_initial_input.bin", data.data(),
                      data.size());

  if (dst != NULL) {
    /* Compare shapes */
    APP_LOG_MESSAGE(APP_LOG_LEVEL_DEBUG, log_level, "Expected shape: %s",
                shape_to_string(expected_shape).c_str());
    vector<uint32_t> modified_expected_shape = {1};
    modified_expected_shape.insert(modified_expected_shape.end(),
                                   expected_shape.begin(),
                                   expected_shape.end());
    APP_LOG_MESSAGE(APP_LOG_LEVEL_DEBUG, log_level,
                "Expected shape after prepend batch size: %s",
                shape_to_string(modified_expected_shape).c_str());

    if (parsed_shape == modified_expected_shape) {
      APP_LOG_MESSAGE(APP_LOG_LEVEL_DEBUG, log_level, "Shapes match!");
      shape_matched = true;
    } else {
      APP_LOG_MESSAGE(APP_LOG_LEVEL_WARNING, log_level,
                  "Shapes do not match, converting NCHW to NHWC!");
      shape_matched = false;
    }

    if (shape_matched) {
      memcpy(dst, data.data(), data.size() * sizeof(float));
    } else {
      uint32_t batch = 1;
      uint32_t frame_height = expected_shape[0];
      uint32_t frame_width = expected_shape[1];
      uint32_t channels = expected_shape[2];
      vector<float> dst_data(batch * frame_height * frame_width * channels);
      nchw_to_nhwc(data.data(), reinterpret_cast<float *>(dst), batch, frame_width,
                   frame_height, channels);

      /* Dump transformed data to file */
      if (ctx->dump_all_inputs)
        dump_data_to_file("/tmp/input.1_after_transform.bin",
                          reinterpret_cast<float *>(dst),
                          batch * frame_height * frame_width * channels);
    }
  }
}
/* Get input frame resolution, in case of raw input user has to
 * provide in -d option of application
 */
static bool extract_input_resolution(AppContext *ctx) {
  AppLogLevel log_level = ctx->log_level;
  uint32_t in_frame_width = 0;
  uint32_t in_frame_height = 0;
  size_t in_frame_size = 0;

  /* Get the file extension from the input file path */
  string fileExtension = get_file_extension(ctx->input_file_path);

  /* Convert the file extension to lowercase for case-insensitive comparison */
  transform(fileExtension.begin(), fileExtension.end(), fileExtension.begin(),
            ::tolower);

  /* Process based on the file extension */
  if (fileExtension == "mp4") {
    /* For now mp4 not supported */
    APP_LOG_MESSAGE(APP_LOG_LEVEL_ERROR, log_level,
                "Input file should be in jpg/jpeg/nv12/raw formats only");
    return false;
    /* Open video file for capturing properties */
    cv::VideoCapture videoCapture(ctx->input_file_path);

    if (!videoCapture.isOpened()) {
      APP_LOG_MESSAGE(APP_LOG_LEVEL_ERROR, log_level, "Unable to open video file");
      return false;
    }

    /* Get video properties */
    in_frame_width =
        static_cast<uint32_t>(videoCapture.get(cv::CAP_PROP_FRAME_WIDTH));
    in_frame_height =
        static_cast<uint32_t>(videoCapture.get(cv::CAP_PROP_FRAME_HEIGHT));
    in_frame_size =
        in_frame_width * in_frame_height * 3; /* Assuming BGR format; */
    ctx->input_fmt = APP_VIDEO_INPUT_FORMAT_MP4;
  } else if (fileExtension == "jpg" || fileExtension == "jpeg") {
    /* Read image file for capturing properties */
    cv::Mat Frame;
    Frame = cv::imread(ctx->input_file_path);

    if (Frame.empty()) {
      APP_LOG_MESSAGE(APP_LOG_LEVEL_ERROR, log_level, "Unable to open image file");
      return false;
    }

    /* Get image properties */
    in_frame_width = static_cast<uint32_t>(Frame.cols);
    in_frame_height = static_cast<uint32_t>(Frame.rows);
    in_frame_size =
        static_cast<size_t>(in_frame_width * in_frame_height *
                            static_cast<uint32_t>(Frame.channels()));
    ctx->input_fmt = APP_VIDEO_INPUT_FORMAT_JPEG;
  } else if (fileExtension == "nv12") {
    if (!ctx->input_width || !ctx->input_height) {
      APP_LOG_MESSAGE(
          APP_LOG_LEVEL_ERROR, log_level,
          "For NV12 format, input the width and height using \"-d\" option");
      cout << "For NV12 format, input the width and height using \"-d\" option";
      return false;
    }
    ctx->input_fmt = APP_VIDEO_INPUT_FORMAT_NV12;
    in_frame_height = ctx->input_height;
    in_frame_width = ctx->input_width;
    in_frame_size = ctx->input_height * ctx->input_width * 1.5;
  } else if (fileExtension == "raw") {
    if (!ctx->input_width || !ctx->input_height) {
      APP_LOG_MESSAGE(
          APP_LOG_LEVEL_ERROR, log_level,
          "For raw format, input the width and height using \"-d\" option");
      cout << "For raw format, input the width and height using \"-d\" option";
      return false;
    }
    APP_LOG_MESSAGE(APP_LOG_LEVEL_DEBUG, log_level,
                "For raw format, input is treated as quantized data");
    in_frame_height = ctx->input_height;
    in_frame_width = ctx->input_width;
    if(ctx->input_fmt == APP_VIDEO_INPUT_FORMAT_RGBX) {
      in_frame_size = ctx->input_height * ctx->input_width * 4;
    } else if(ctx->input_fmt == APP_VIDEO_INPUT_FORMAT_RGB) {
      in_frame_size = ctx->input_height * ctx->input_width * 3;
    } else if(ctx->input_fmt == APP_VIDEO_INPUT_FORMAT_BGRX) {
      in_frame_size = ctx->input_height * ctx->input_width * 4;
    } else {
      ctx->input_fmt = APP_VIDEO_INPUT_FORMAT_BGR;
      in_frame_size = ctx->input_height * ctx->input_width * 3;
    }
  } else if (fileExtension == "npz") {
    /* Assume only one input file for now */
    string temp_dir = "/tmp/npz_extract";
    string npy_file_path = extract_npz(ctx->input_file_path, temp_dir);
    if (!npy_file_path.empty()) {
      APP_LOG_MESSAGE(APP_LOG_LEVEL_DEBUG, log_level, "npz has %s",
                  npy_file_path.c_str());
      parse_npy_file(ctx, npy_file_path, log_level, &in_frame_width,
                     &in_frame_height, NULL,
                     ctx->model_info.in_tensors[0].meta.shape);
    } else {
      APP_LOG_MESSAGE(APP_LOG_LEVEL_ERROR, log_level, "npz do not have valid file");
      return false;
    }
    /* Assume it is RGB data for now and 4 is for float data in npz */
    in_frame_size = in_frame_height * in_frame_width * 3 * 4;
    ctx->input_fmt = APP_VIDEO_INPUT_FORMAT_BGR_FLOAT;
  } else {
    APP_LOG_MESSAGE(APP_LOG_LEVEL_ERROR, log_level,
                "Input file should be in jpg/jpeg/nv12/raw formats only");
    return false;
  }

  /* Log and update the input resolution in AppContext */
  APP_LOG_MESSAGE(
      APP_LOG_LEVEL_INFO, log_level,
      "in_frame_width = %d , in_frame_height = %d , in_frame_size = %ld",
      in_frame_width, in_frame_height, in_frame_size);

  ctx->input_height = in_frame_height;
  ctx->input_width = in_frame_width;
  ctx->in_frame_size = in_frame_size;
  return true;
}

/* Close files and release associated resources. */
void close_files(AppContext *ctx) {
#ifdef DUMP_INPUTS
  /* Close and reset debug-related file streams if they are open */
  if (ctx->dump_input_fp.is_open()) {
    ctx->dump_input_fp.close();
    /* Reset the string objects to empty strings */
    ctx->dump_input_path.clear();
  }
  if (ctx->dump_infer_input_fp.is_open()) {
    ctx->dump_infer_input_fp.close();
    ctx->dump_infer_input_path.clear();
  }
#endif

  /* Close and reset input and output file streams if they are open */
  if (ctx->input_file.is_open()) {
    ctx->input_file.close();
    ctx->input_file_path.clear();
  }
  if (ctx->output_file.is_open()) {
    ctx->output_file.close();
    ctx->out_file_path.clear();
  }
}

/* Open files required for processing. */
bool open_files(AppContext *ctx) {
  string file_extension;
  AppLogLevel log_level = ctx->log_level;

  /* Extract input resolution */
  if (extract_input_resolution(ctx) != true) {
    APP_LOG_MESSAGE(APP_LOG_LEVEL_ERROR, log_level, "Failed to get input resolution");
    goto failure;
  }

  if (ctx->input_fmt == APP_VIDEO_INPUT_FORMAT_NV12 ||
      ctx->input_fmt == APP_VIDEO_INPUT_FORMAT_BGR_FLOAT ||
      ctx->input_fmt == APP_VIDEO_INPUT_FORMAT_RGB ||
      ctx->input_fmt == APP_VIDEO_INPUT_FORMAT_BGR ||
      ctx->input_fmt == APP_VIDEO_INPUT_FORMAT_RGBX ||
      ctx->input_fmt == APP_VIDEO_INPUT_FORMAT_BGRX) {
    /* Open input file */
    ctx->input_file.open(ctx->input_file_path, ios::binary | ios::in);
    if (!ctx->input_file.is_open()) {
      APP_LOG_MESSAGE(APP_LOG_LEVEL_ERROR, log_level, "Can't open file: %s",
                  ctx->input_file_path.c_str());
      goto failure;
    }
  } else if (ctx->input_fmt == APP_VIDEO_INPUT_FORMAT_MP4) {
    /* Code block for reading from a video file */
    ctx->vid_capture = new cv::VideoCapture(ctx->input_file_path);
    if (!ctx->vid_capture->isOpened()) {
      APP_LOG_MESSAGE(APP_LOG_LEVEL_ERROR, log_level, "Unable to open video file");
      goto failure;
    }
  }

  if (ctx->input_fmt == APP_VIDEO_INPUT_FORMAT_NV12) {
    file_extension = "nv12";
  } else if (ctx->input_fmt == APP_VIDEO_INPUT_FORMAT_JPEG ||
             ctx->input_fmt == APP_VIDEO_INPUT_FORMAT_MP4) {
    file_extension = "bgr";
  } else if (ctx->input_fmt == APP_VIDEO_INPUT_FORMAT_BGR_FLOAT ||
             ctx->input_fmt == APP_VIDEO_INPUT_FORMAT_RGB ||
             ctx->input_fmt == APP_VIDEO_INPUT_FORMAT_BGR ||
             ctx->input_fmt == APP_VIDEO_INPUT_FORMAT_RGBX ||
             ctx->input_fmt == APP_VIDEO_INPUT_FORMAT_BGRX) {
    file_extension = "bin";
  } else {
    goto failure;
  }

  /* Open output file */
  if (!ctx->out_file_path.empty()) {
    ctx->output_file.open(ctx->out_file_path, ios::binary | ios::out);
    if (!ctx->output_file.is_open()) {
      APP_LOG_MESSAGE(APP_LOG_LEVEL_ERROR, log_level, "Can't open file: %s",
                  ctx->out_file_path.c_str());
      goto failure;
    }
  } else {
    APP_LOG_MESSAGE(
        APP_LOG_LEVEL_INFO, log_level,
        "As the output file is not provided, video frame with overlayed infer "
        "results will not be dummped. You can see the inference results by "
        "enabling output logs by setting the log level to 3.");
  }
#ifdef DUMP_INPUTS
  ctx->dump_input_path = "/tmp/dumped_input_" + to_string(ctx->input_width) +
                         "_" + to_string(ctx->input_height) + "_snap_" + to_string(ctx->snap_id) + "." +
                         file_extension;
  ctx->dump_infer_input_path = "/tmp/dumped_infer_input_snap_" + to_string(ctx->snap_id);

  /* Open debug files */
  ctx->dump_input_fp.open(ctx->dump_input_path, ios::binary | ios::out);
  if (!ctx->dump_input_fp.is_open()) {
    APP_LOG_MESSAGE(APP_LOG_LEVEL_ERROR, log_level, "Can't open file: %s",
                ctx->dump_input_path.c_str());
    goto failure;
  }

  ctx->dump_infer_input_fp.open(ctx->dump_infer_input_path,
                                ios::binary | ios::out);
  if (!ctx->dump_infer_input_fp.is_open()) {
    APP_LOG_MESSAGE(APP_LOG_LEVEL_ERROR, log_level, "Can't open file: %s",
                ctx->dump_infer_input_path.c_str());
    goto failure;
  }
#endif

  return true;

failure:
  close_files(ctx);
  return false;
}

/* Read input data into a video frame */
AppReadStatus read_input(AppContext *ctx, vart::VideoFrame *video_frame) {
  AppLogLevel log_level = ctx->log_level;
  size_t width, height;
  size_t bytes = 0;
  size_t bytes_to_read = 0;

  /* Map the video frame memory for writing */
  const vart::VideoFrameMapInfo *map_info = nullptr;

  try {
    map_info = &video_frame->map(vart::DataMapFlags::WRITE);
  } catch (const exception &e) {
    APP_LOG_MESSAGE(APP_LOG_LEVEL_ERROR, log_level, "Failed to map memory : %s",
                e.what());
    return APP_READ_FAILED;
  }

  /* Read input data from a video capture source (e.g., webcam or video file) */
  if (ctx->input_fmt == APP_VIDEO_INPUT_FORMAT_MP4) {
    cv::Mat frame(ctx->input_height, ctx->input_width, CV_8UC3);

    /* Copy decoded data into frame */
    if (!ctx->vid_capture->read(frame)) {
      APP_LOG_MESSAGE(APP_LOG_LEVEL_INFO, log_level,
                  "Video capture read returned 0, sending End-of-file");
      video_frame->unmap();
      /* Check if the end of the video file is reached */
      if (ctx->vid_capture->get(cv::CAP_PROP_POS_FRAMES) ==
          ctx->vid_capture->get(cv::CAP_PROP_FRAME_COUNT)) {
        return APP_EOF;
      } else
        return APP_READ_FAILED;
    }

    /* As there is no method from cv::VideoCapture to get pointer to the
     * decoded data, copied decoded data into frame above and now copying it to
     * vart::VideoFrame, hence copy is happening twice, this'll impact
     * performance. */
    for (uint32_t h = 0; h < ctx->input_height; h++) {
      /* cv::videoCapture reads data in BGR format */
      uint8_t *dst =
          map_info->planes[0].data + (h * map_info->planes[0].stride);
      uint8_t *src = frame.data + (h * frame.cols * 3);
      memcpy(dst, src, (h * frame.cols * 3));
    }
  } else if (ctx->input_fmt == APP_VIDEO_INPUT_FORMAT_JPEG) {
    auto read_frame = cv::imread(ctx->input_file_path);
    if (read_frame.empty()) {
      video_frame->unmap();
      APP_LOG_MESSAGE(APP_LOG_LEVEL_ERROR, log_level, "Unable to open image file");
      return APP_READ_FAILED;
    }
    /* currently coping image into user buffer */
    /* it is not an optimized solution */

    for (int h = 0; h < read_frame.rows; h++) {
      /* cv::imread() is reading image in color, hence the order of data will be
       * BGR in 1 plane */
      uint8_t *dst =
          map_info->planes[0].data + (h * map_info->planes[0].stride);
      uint8_t *src = read_frame.data + (h * read_frame.cols * 3);
      memcpy(dst, src, (read_frame.cols * 3));
    }

    video_frame->unmap();
    return APP_READ_SUCCESS;
  } else if (ctx->input_fmt == APP_VIDEO_INPUT_FORMAT_RGB ||
             ctx->input_fmt == APP_VIDEO_INPUT_FORMAT_BGR ||
             ctx->input_fmt == APP_VIDEO_INPUT_FORMAT_RGBX ||
             ctx->input_fmt == APP_VIDEO_INPUT_FORMAT_BGRX ) {
    bytes = 0;
    width = ctx->input_width;
    height = ctx->input_height;
    bytes_to_read = ctx->in_frame_size; // either w*h*3 or w*h*4 based on (RGB,BGR) or (RGBX,BGRX)
    uint8_t *data_ptr = map_info->planes[0].data;
    bytes =
        ctx->input_file.read(reinterpret_cast<char *>(data_ptr), bytes_to_read)
            .gcount();
    APP_LOG_MESSAGE(APP_LOG_LEVEL_DEBUG, log_level,
                "Number of bytes to read: %lu, bytes read: %lu", bytes_to_read, bytes);
    if (bytes != bytes_to_read) {
      if (bytes != 0) {
        APP_LOG_MESSAGE(APP_LOG_LEVEL_WARNING, log_level,
                    "Read less data than expected");
      }
      video_frame->unmap();
      /* Check if the end of the file is reached */
      if (ctx->input_file.eof()) {
        return APP_EOF;
      } else {
        return APP_READ_FAILED;
      }
    }
  } else if (ctx->input_fmt == APP_VIDEO_INPUT_FORMAT_BGR_FLOAT) {
    uint8_t *data_ptr = map_info->planes[0].data;
    /* Assume only one input file for now */
    string temp_dir = "/tmp/npz_extract";
    string npy_file_path = extract_npz(ctx->input_file_path, temp_dir);
    if (!npy_file_path.empty()) {
      APP_LOG_MESSAGE(APP_LOG_LEVEL_DEBUG, log_level, "npz has %s",
                  npy_file_path.c_str());
      parse_npy_file(ctx, npy_file_path, log_level, &ctx->input_width,
                     &ctx->input_height, data_ptr,
                     ctx->model_info.in_tensors[0].meta.shape);
    }
  }
  /* Check input format for NV12 */
  else if (ctx->input_fmt == APP_VIDEO_INPUT_FORMAT_NV12) {
    bytes = 0;
    width = ctx->input_width;
    height = ctx->input_height;

    bytes_to_read = width * height;

    /* read luminance (Y) plane data */
    for (size_t h = 0; h < height; h++) {
      uint8_t *dst =
          map_info->planes[0].data + (h * map_info->planes[0].stride);
      bytes +=
          ctx->input_file.read(reinterpret_cast<char *>(dst), map_info->width)
              .gcount();
    }

    APP_LOG_MESSAGE(APP_LOG_LEVEL_DEBUG, log_level,
                "Read %lu bytes for plane 0 for NV12", bytes);
    if (bytes != bytes_to_read) {
      if (bytes != 0) {
        APP_LOG_MESSAGE(APP_LOG_LEVEL_WARNING, log_level,
                    "Read less data than expected");
      }
      video_frame->unmap();
      /* Check if the end of the file is reached */
      if (ctx->input_file.eof()) {
        return APP_EOF;
      } else {
        return APP_READ_FAILED;
      }
    }

    bytes_to_read = width * height * 0.5;
    bytes = 0;

    /* Read chrominance (U and V plane interleaved) plane data */
    for (size_t h = 0; h < height / 2; h++) {
      uint8_t *dst =
          map_info->planes[1].data + (h * map_info->planes[1].stride);
      bytes +=
          ctx->input_file.read(reinterpret_cast<char *>(dst), map_info->width)
              .gcount();
    }

    APP_LOG_MESSAGE(APP_LOG_LEVEL_DEBUG, log_level,
                "Read %lu bytes for plane 1 for NV12", bytes);
    if (bytes != bytes_to_read) {
      APP_LOG_MESSAGE(APP_LOG_LEVEL_WARNING, log_level, "Read less data than expected");
      video_frame->unmap();
      /* Check if the end of the file is reached */
      if (ctx->input_file.eof()) {
        return APP_EOF;
      } else {
        return APP_READ_FAILED;
      }
    }
  }

  /* Unmap the video frame memory */
  video_frame->unmap();
  APP_LOG_MESSAGE(APP_LOG_LEVEL_DEBUG, log_level,
              "Read data in preprocess input buffer");
  return APP_READ_SUCCESS;
}
