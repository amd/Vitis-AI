/*
 * Copyright (C) 2026 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software
 * is furnished to do so, subject to the following conditions:
 *
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

/*
 *
 *                               ╔════════════════════╗ ╔════════════════════╗ ╔════════════════════╗
 *                               ║ VideoFrame BGR IN  ║ ║ VideoFrame IFM OUT ║ ║ NpuTensor OFM +    ║
 *                               ║ read: preprocess   ║ ║ read: runner (fd)  ║ ║ vart::Memory view  ║
 *                               ║ write: host upload ║ ║ write: preprocess  ║ ║ read: post  (Mem)  ║
 *                               ║                    ║ ║ fmt: mode-specific ║ ║ write: runner      ║
 *                               ╚════════════════════╝ ╚════════════════════╝ ╚════════════════════╝
 *                                  ↑           |           ↑           |         ↑             |
 *                                  |           |           |           |         |             |
 *                              upload          |    pre writes IFM     |   runner writes OFM   |
 *                                  |           |           |           |         |             |
 *                                  |           |           |           |         |             |
 *                                  |           |           |           |         |             |
 *                                  |           |           |           |         |             |
 *                                  |     pre reads BGR     | runner reads IFM fd | post reads via Memory
 *                                  |           |           |           |         |             |
 *                                  |           ↓           |           ↓         |             ↓
 *  image ------> +--------------------+ +--------------------+ +--------------------+
 * +--------------------+---->Display | OpenCV decode      | | preprocess         | | runner             | | postprocess
 * |
 *                +--------------------+ +--------------------+ +--------------------+ +--------------------+
 *
 *  Two tensor-binding modes select the IFM VideoFormat in box 2 (HW and CPU IFM are sized
 *  differently so the dma-buf bridge expects the matching colour-format on the preprocess side):
 *    zero-copy (HW)     : RGBx / RGBx_BF16 / RGBx_FP16 (packed, channel-padded-to-4).
 *    non-zero-copy (CPU): RGBP_FP16 / RGBP_FLOAT (planar float).
 *  Box 3 OFM is allocated by the runner; its dma-buf fd is also imported as a vart::Memory so
 *  postprocess consumes the exact same physical buffer (no staging OFM copy in either mode).
 *  Runner::execute() syncs IFM/OFM internally — no explicit NpuTensor::sync_buffer() is needed.
 *
 */

#include <boost/program_options.hpp>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <string>

#include "vart_zerocopy.hpp"

namespace po = boost::program_options;

namespace {

struct Options {
  std::string image_path;
  std::string compiled_model_dir;
  bool non_zero_copy{false};
  int32_t runs{1};
};

void add_options(po::options_description& desc, Options& opt) {
  desc.add_options()("help,h", "Print help message")("image,i", po::value<std::string>(&opt.image_path)->required(),
                                                     "Input image path (jpg file)")(
      "model-dir,m", po::value<std::string>(&opt.compiled_model_dir)->required(),
      "VAIML model: path to .rai file, or a directory containing one .rai (e.g. .../resnet50_int8) or vaiml_par_0")(
      "non-zero-copy,c", po::bool_switch(&opt.non_zero_copy),
      "Use CPU tensor type for runner IFM/OFM (non-zero-copy demo). Default is HW tensor type (zero-copy).")(
      "runs,n", po::value<int32_t>(&opt.runs)->default_value(1),
      "Number of timed benchmark iterations on the same image (default 1; a fixed 1-iteration warmup "
      "is always run before timing).");
}

enum class ParseOutcome { Ok, Help, Error };

ParseOutcome parse_options(int argc, char* argv[], [[maybe_unused]] Options& opt, po::options_description& desc) {
  try {
    po::positional_options_description pos;
    pos.add("image", 1);
    pos.add("model-dir", 1);

    po::variables_map vm;
    po::store(po::command_line_parser(argc, argv).options(desc).positional(pos).run(), vm);

    if (vm.count("help")) {
      std::cout << desc << std::endl;
      return ParseOutcome::Help;
    }

    po::notify(vm);
    if (opt.runs < 1) {
      std::cerr << "Error: --runs must be >= 1 (got " << opt.runs << ")" << std::endl << std::endl;
      std::cerr << desc << std::endl;
      return ParseOutcome::Error;
    }
    return ParseOutcome::Ok;
  } catch (const po::error& ex) {
    std::cerr << "Error: " << ex.what() << std::endl << std::endl;
    std::cerr << desc << std::endl;
    return ParseOutcome::Error;
  }
}

}  // namespace

int main(int argc, char* argv[]) {
  Options opt{};
  po::options_description desc(
      "\nvart_zerocopy — ImageNet/ResNet-style zero-copy pipeline (defaults); "
      "see README to retarget preprocess/postprocess for other models.\n\nAllowed options");
  add_options(desc, opt);

  const ParseOutcome parsed = parse_options(argc, argv, opt, desc);

  if (parsed == ParseOutcome::Help) {
    return 0;
  }
  if (parsed == ParseOutcome::Error) {
    return 1;
  }

  try {
    const TensorMode mode = opt.non_zero_copy ? TensorMode::NonZerocopy : TensorMode::Zerocopy;
    const char* mode_label = opt.non_zero_copy ? "non-zero-copy (CPU tensor type)" : "zero-copy (HW tensor type)";
    std::cout << "\n"
                 "--------------------------------------------------------------------------------\n"
                 "vart_zerocopy — command line\n"
                 "--------------------------------------------------------------------------------\n";
    std::cout << "  " << std::left << std::setw(18) << "image (-i)" << opt.image_path << '\n';
    std::cout << "  " << std::left << std::setw(18) << "model (-m)" << opt.compiled_model_dir << '\n';
    std::cout << "  " << std::left << std::setw(18) << "mode" << mode_label << '\n';
    std::cout << "  " << std::left << std::setw(18) << "runs (-n)" << opt.runs << "  (+1 warmup)" << '\n';

    VartZerocopyPipeline pipeline(opt.compiled_model_dir, mode);

    std::cout << "\n"
                 "--------------------------------------------------------------------------------\n"
                 "1 - Load image (host)\n"
                 "--------------------------------------------------------------------------------\n";
    if (!pipeline.read_and_decode(opt.image_path)) {
      std::cerr << "[step 1] Failed to load/decode image: " << opt.image_path << '\n';
      return 1;
    }

    std::cout << "\n"
                 "--------------------------------------------------------------------------------\n"
                 "2 - Preprocess (device) + bind IFM\n"
                 "--------------------------------------------------------------------------------\n";
    if (!pipeline.preprocess()) {
      std::cerr << "[step 2] Preprocess failed (see error messages above).\n";
      return 1;
    }

    std::cout << "\n"
                 "--------------------------------------------------------------------------------\n"
                 "3 - Infer (NPU)\n"
                 "--------------------------------------------------------------------------------\n";
    if (!pipeline.infer()) {
      std::cerr << "[step 3] Inference (Runner::execute) failed (see error messages above).\n";
      return 1;
    }

    std::cout << "\n"
                 "--------------------------------------------------------------------------------\n"
                 "4 - Postprocess (OFM to labels)\n"
                 "--------------------------------------------------------------------------------\n";
    if (!pipeline.postprocess()) {
      std::cerr << "[step 4] Postprocess failed (see error messages above).\n";
      return 1;
    }

    std::cout << "\n"
                 "--------------------------------------------------------------------------------\n"
                 "5 - Results\n"
                 "--------------------------------------------------------------------------------\n";
    pipeline.display();

    // -----------------------------------------------------------------------------------------
    // Benchmark loop. Steps 2-4 above already ran one full untimed iteration of preprocess +
    // infer + postprocess, which doubles as the warmup (it allocated all buffers, uploaded the
    // BGR frame, bound the IFM dma-buf fd, allocated the OFM and exported the OFM->Memory bridge,
    // then ran HLS + Runner::execute + PostProcess::process once on a cold cache). The timed
    // loop below reuses those bindings, so each iteration only re-runs HLS preprocess ->
    // Runner::execute -> PostProcess::process. Pipeline logs are silenced before the loop;
    // only the aggregated averages are printed.
    // -----------------------------------------------------------------------------------------
    constexpr uint32_t kWarmupRuns = 1u;  // steps 2-4 above
    std::cout << "\n"
                 "--------------------------------------------------------------------------------\n"
                 "6 - Benchmark (mode: "
              << (opt.non_zero_copy ? "non-zero-copy" : "zero-copy") << ", runs: " << opt.runs
              << ", warmup: " << kWarmupRuns
              << ")\n"
                 "--------------------------------------------------------------------------------\n";

    pipeline.set_verbose(false);

    using clk = std::chrono::steady_clock;
    using us = std::chrono::duration<double, std::micro>;
    double pre_us = 0.0;
    double inf_us = 0.0;
    double pst_us = 0.0;
    for (int32_t i = 0; i < opt.runs; ++i) {
      auto t0 = clk::now();
      if (!pipeline.preprocess()) {
        std::cerr << "[step 6/benchmark] Preprocess failed at iteration " << i << ".\n";
        return 1;
      }
      auto t1 = clk::now();
      if (!pipeline.infer()) {
        std::cerr << "[step 6/benchmark] Inference failed at iteration " << i << ".\n";
        return 1;
      }
      auto t2 = clk::now();
      if (!pipeline.postprocess()) {
        std::cerr << "[step 6/benchmark] Postprocess failed at iteration " << i << ".\n";
        return 1;
      }
      auto t3 = clk::now();
      pre_us += us(t1 - t0).count();
      inf_us += us(t2 - t1).count();
      pst_us += us(t3 - t2).count();
    }
    const double n = static_cast<double>(opt.runs);
    const double pre_ms = (pre_us / n) / 1000.0;
    const double inf_ms = (inf_us / n) / 1000.0;
    const double pst_ms = (pst_us / n) / 1000.0;
    const double tot_ms = pre_ms + inf_ms + pst_ms;
    // Pipeline throughput covers all three stages combined. Infer-only throughput isolates
    // vart::Runner::execute, which is the call the zero-copy vs non-zero-copy mode actually
    // moves the needle on. Preprocess and postprocess also shift between modes because each
    // stage handles a different data format (preprocess emits a different VideoFormat per
    // mode; postprocess sees a different OFM dtype), but the dominant delta is on the infer line.
    const double pipeline_fps = tot_ms > 0.0 ? (1000.0 / tot_ms) : 0.0;
    const double infer_fps = inf_ms > 0.0 ? (1000.0 / inf_ms) : 0.0;

    constexpr int kBenchLabelWidth = 24;
    std::cout << std::fixed << std::setprecision(3);
    std::cout << "  " << std::left << std::setw(kBenchLabelWidth) << "preprocess" << pre_ms << " ms / frame\n";
    std::cout << "  " << std::left << std::setw(kBenchLabelWidth) << "infer" << inf_ms << " ms / frame\n";
    std::cout << "  " << std::left << std::setw(kBenchLabelWidth) << "postprocess" << pst_ms << " ms / frame\n";
    std::cout << "  " << std::left << std::setw(kBenchLabelWidth) << "total" << tot_ms << " ms / frame\n";
    std::cout << "  " << std::left << std::setw(kBenchLabelWidth) << "throughput (infer)" << infer_fps << " FPS\n";
    std::cout << "  " << std::left << std::setw(kBenchLabelWidth) << "throughput (pipeline)" << pipeline_fps
              << " FPS\n";
    std::cout << std::defaultfloat;

    std::cout << "--------------------------------------------------------------------------------\n"
              << "Finished successfully.\n";
  } catch (const std::exception& e) {
    std::cerr << "Error (stopped the demo): " << e.what() << '\n';
    return 1;
  }

  return 0;
}
