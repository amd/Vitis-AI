#include <onnxruntime_cxx_api.h>
#include <chrono>
#include <fstream>
#include <iostream>
#include <random>
#include <vector>

// Read raw data
bool load_raw_float(const std::string& filename, std::vector<float>& data, std::vector<int64_t>& shape) {
  std::ifstream f(filename, std::ios::binary);
  if (!f)
    return false;
  f.seekg(0, std::ios::end);
  size_t size = f.tellg();
  f.seekg(0, std::ios::beg);
  data.resize(size / sizeof(float));
  f.read(reinterpret_cast<char*>(data.data()), size);
  // For demo, you must set shape manually
  // Example: shape = {1, 3, 224, 224};
  return true;
}

bool save_raw_float(const std::string& filename, const std::vector<float>& data) {
  std::ofstream f(filename, std::ios::binary);
  if (!f)
    return false;
  f.write(reinterpret_cast<const char*>(data.data()), data.size() * sizeof(float));
  return true;
}

int main(int argc, char* argv[]) {
  if (argc < 4) {
    std::cerr << "Usage: " << argv[0] << " <model>.onnx input0.bin output0.bin" << std::endl;
    return 1;
  }

  const char* model_path = argv[1];
  std::string input_file = argv[2];
  std::string output_file = argv[3];

  // Env + session
  Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "Default");
  Ort::SessionOptions session_options;

  // Set VitisAI-specific options
  std::unordered_map<std::string, std::string> options;
  options["config_file"] = "./vitisai_config.json";  // Config file
  options["cacheDir"] = "./vek385_cache_dir";  // Cache dir used to compile the model, should match compilation result.
  options["cacheKey"] = "resnet50-v1-12";      // Cache key used to compile the model, should match compilation result.
  options["ai_analyzer_visualization"] = "True";  // Visualization in AI analyzer
  options["ai_analyzer_profiling"] = "True";      // Profiling in AI analyzer
  options["target"] = "VAIML";                    // Target Platform

  // ORT Session with VitisAIExecutionProvider
  session_options.AppendExecutionProvider("VitisAI", options);
  Ort::Session session(env, model_path, session_options);

  // Load input (bin/raw assumed)
  std::vector<float> input_data;
  std::vector<int64_t> input_shape = {1, 3, 224, 224};  // Fill manually
  if (!load_raw_float(input_file, input_data, input_shape)) {
    std::cerr << "Failed to load input file" << std::endl;
    return 1;
  }

  // Create tensor
  Ort::MemoryInfo mem_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
  Ort::Value input_tensor = Ort::Value::CreateTensor<float>(mem_info, input_data.data(), input_data.size(),
                                                            input_shape.data(), input_shape.size());

  // Names
  Ort::AllocatorWithDefaultOptions allocator;
  auto input_name = session.GetInputNameAllocated(0, allocator);
  const char* input_names[] = {input_name.get()};

  size_t num_outputs = session.GetOutputCount();
  std::vector<const char*> output_names;
  std::vector<Ort::AllocatedStringPtr> output_name_ptrs;
  for (size_t i = 0; i < num_outputs; i++) {
    output_name_ptrs.emplace_back(session.GetOutputNameAllocated(i, allocator));
    output_names.push_back(output_name_ptrs.back().get());
  }

  // Run inference
  auto output_tensors = session.Run(Ort::RunOptions{nullptr},
                                    input_names,          // const char* const*
                                    &input_tensor,        // const Ort::Value*
                                    1,                    // input num
                                    output_names.data(),  // const char* const*
                                    num_outputs           // input num
  );

  // Save first output (as raw binary)
  float* out_data = output_tensors[0].GetTensorMutableData<float>();
  size_t out_size = output_tensors[0].GetTensorTypeAndShapeInfo().GetElementCount();
  std::vector<float> output_vec(out_data, out_data + out_size);

  if (!save_raw_float(output_file, output_vec)) {
    std::cerr << "Failed to save output" << std::endl;
    return 1;
  }

  std::cout << "Saved to " << output_file << std::endl;
  return 0;
}
