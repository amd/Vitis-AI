/*
 *
 * Copyright (C) 2026 Advanced Micro Devices, Inc.
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

#include <vvas_core/vvas_common.h>
#include <vvas_core/vvas_log.h>
#include <vvas_core/vvas_postprocess.h>
#include <vvas_utils/vvas_utils.h>

/* Compatibility shim: this yolox source uses the unprefixed VVAS logging names
 * (LOG_*), but the vvas-core shipped in this SDK exposes them with the VVAS_
 * prefix. Map them here so the file builds unchanged against this vvas-core.
 * Guarded so a vvas-core that already provides the unprefixed names is unaffected. */
#ifndef LOG_ERROR
#define LOG_ERROR(set_level, ...)       VVAS_LOG_ERROR(set_level, ##__VA_ARGS__)
#endif
#ifndef LOG_ERROR_OBJ
#define LOG_ERROR_OBJ(OBJ, ...)         VVAS_LOG_ERROR_OBJ(OBJ, ##__VA_ARGS__)
#endif
#ifndef LOG_WARNING_OBJ
#define LOG_WARNING_OBJ(OBJ, ...)       VVAS_LOG_WARNING_OBJ(OBJ, ##__VA_ARGS__)
#endif
#ifndef LOG_INFO_OBJ
#define LOG_INFO_OBJ(OBJ, ...)          VVAS_LOG_INFO_OBJ(OBJ, ##__VA_ARGS__)
#endif
#ifndef LOG_DEBUG_OBJ
#define LOG_DEBUG_OBJ(OBJ, ...)         VVAS_LOG_DEBUG_OBJ(OBJ, ##__VA_ARGS__)
#endif
#ifndef LOG_LEVEL_NONE
#define LOG_LEVEL_NONE   VVAS_LOG_LEVEL_NONE
#endif
#ifndef LOG_LEVEL_DEBUG
#define LOG_LEVEL_DEBUG  VVAS_LOG_LEVEL_DEBUG
#endif
#include <boost/lexical_cast.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <iostream>
#include <sstream>
#include <atomic>
#include <cinttypes>
#include <vector>
#include <algorithm>
#include <cmath>
#include <stdfloat>
#include <optional>
#include <limits>
#include <type_traits>
#include <cstring>
#include <fstream>
#include <cctype>

// Global logger/frame state for this postprocess module.
// NOTE: profiling is optional, but frame logging uses the frame counter regardless.
static VvasLogLevel g_yolo_pp_log_level = DEFAULT_VVAS_LOG_LEVEL;
static VvasLogger* g_yolo_pp_logger_handle = nullptr;
static std::atomic<uint64_t> g_yolo_pp_frame_counter {0};
/*
* WARNING this profiling code if used along with START_PROFILE
* and STOP_PROFILE macro's is only useful if there is a single
* instance of Yolo Post processing in the application, as the
* profilers are stored in a static variable, do not expect sane
* results if enabled in a process with multiple instances of
* yolo processing.
* Enable only for development testing.
*/
//#define PROFILE

#ifdef PROFILE


#include <chrono>
#include <numeric>
class YoloProfiler {

    vector<std::chrono::high_resolution_clock::time_point> m_start_times;
    vector<std::chrono::high_resolution_clock::time_point> m_stop_times;
    vector<long> m_durations;
    string m_log;
    bool m_detailed;
public:
    YoloProfiler(const std::string &log, bool detailed=true): m_log{log}, m_detailed{detailed} {}
    YoloProfiler(YoloProfiler&& p) = default;
    YoloProfiler(const YoloProfiler& p) = default;

    void Start(){
        m_start_times.push_back(std::chrono::high_resolution_clock::now());
    }
    void Stop(){
        m_stop_times.push_back(std::chrono::high_resolution_clock::now());
        if(m_detailed){
            auto us = std::chrono::duration_cast<std::chrono::microseconds>
                    (m_stop_times.back() - m_start_times.back()).count();
            // Prefer *_OBJ logging so module-specific env var overrides (VVAS_CORE_DEBUG) are respected.
            if (g_yolo_pp_logger_handle) {
                vvas_logger_log_obj(LOG_LEVEL_NONE, g_yolo_pp_logger_handle, __FILENAME__,__func__, __LINE__, "%s took %ld us", m_log.c_str(), (long)us);
            } else {
                vvas_log(LOG_LEVEL_NONE, g_yolo_pp_log_level, __FILENAME__,__func__, __LINE__, "%s took %ld us", m_log.c_str(), (long)us);
            }
            m_durations.push_back(us);
        }
    }
    ~YoloProfiler(){
        if(!m_detailed){
            std::transform(m_start_times.begin(), m_start_times.end(),
            m_stop_times.begin(), std::back_inserter(m_durations),
            []( const std::chrono::high_resolution_clock::time_point& start,
                const std::chrono::high_resolution_clock::time_point& stop)
                {
                  return std::chrono::duration_cast<std::chrono::microseconds>(stop - start).count();
                });
        }

        if (!m_durations.empty()) {
            auto avg = std::accumulate(m_durations.begin(), m_durations.end(), 0L) / (long)m_durations.size();
            if (g_yolo_pp_logger_handle) {
                vvas_logger_log_obj(LOG_LEVEL_NONE, g_yolo_pp_logger_handle, __FILENAME__,__func__, __LINE__, "%s on average took %ld us", m_log.c_str(), avg);
            } else {
                vvas_log(LOG_LEVEL_NONE, g_yolo_pp_log_level, __FILENAME__,__func__, __LINE__, "%s on average took %ld us", m_log.c_str(), avg);
            }
        }

    }
};

    static std::map<string, YoloProfiler> profilers;
#define START_PROFILE(name, log) do {                                      \
    const std::string _key = (name);                                       \
    auto it = profilers.find(_key);                                        \
    if (it == profilers.end()) {                                           \
        it = profilers.emplace(_key, YoloProfiler{(log), false}).first;    \
    }                                                                      \
    it->second.Start();                                                    \
} while(0)

#define STOP_PROFILE(name) do {                                            \
    const std::string _key = (name);                                       \
    auto it = profilers.find(_key);                                        \
    if (it != profilers.end()) {                                           \
        it->second.Stop();                                                 \
    }                                                                      \
} while(0)
#else
    #define START_PROFILE(name, log) ;
    #define STOP_PROFILE(name) ;
#endif

using std::unique_ptr;
using std::vector;
using std::string;

constexpr double default_conf_thresh = 0.25;
constexpr double default_iou_thresh = 0.45;
constexpr int32_t default_max_detections = 300;
constexpr size_t image_input_width = 640;
constexpr size_t image_input_height = 640;


// Built-in fallback labels (COCO). If `class_label_file` is provided in JSON, we’ll load labels
// from that file (one class per line) instead of using this list.
const std::vector<std::string> coco_names = {
    "person","bicycle","car","motorcycle","airplane","bus","train","truck","boat",
    "traffic light","fire hydrant","stop sign","parking meter","bench","bird","cat","dog",
    "horse","sheep","cow","elephant","bear","zebra","giraffe","backpack","umbrella",
    "handbag","tie","suitcase","frisbee","skis","snowboard","sports ball","kite",
    "baseball bat","baseball glove","skateboard","surfboard","tennis racket","bottle",
    "wine glass","cup","fork","knife","spoon","bowl","banana","apple","sandwich","orange",
    "broccoli","carrot","hot dog","pizza","donut","cake","chair","couch","potted plant",
    "bed","dining table","toilet","tv","laptop","mouse","remote","keyboard","cell phone",
    "microwave","oven","toaster","sink","refrigerator","book","clock","vase","scissors",
    "teddy bear","hair drier","toothbrush"
};

struct Point {
    float x, y;
    Point(): x{0}, y{0} {}
    Point(float x, float y): x{x}, y{y} {}
};
struct BBox {
    Point tl, br;
    BBox(): tl{0, 0}, br{0, 0} {}
    BBox(const Point& tl, const Point& br): tl{tl}, br{br} {}
    BBox(float tlx, float tly, float brx, float bry): tl{tlx, tly}, br{brx, bry} {}
    BBox(const Point& center, float width, float height): tl{center.x - width/2, center.y - height/2}, br{center.x + width/2, center.y + height/2} {}
    Point getTL() const { return tl; }
    Point getBR() const { return br; }
    float getWidth() const { return br.x - tl.x; }
    float getHeight() const { return br.y - tl.y; }
    BBox addXOffset(float xoffset) const {
        return BBox(tl.x + xoffset, tl.y, br.x + xoffset, br.y);
    }
};

struct Detection {
    BBox box;
    float conf;
    int32_t class_id;
    Detection(): box{0, 0, 0, 0}, conf{0}, class_id{0} {}
    Detection(const BBox& box, float conf, int32_t class_id): box{box}, conf{conf}, class_id{class_id} {}
};

struct GridAndStride {
    int32_t gridx;
    int32_t gridy;
    int32_t stride;
};

struct YoloPostProcessParams{
  double conf_thresh {default_conf_thresh};
  double iou_thresh {default_iou_thresh};
  bool apply_sigmoid {false};
  bool class_agnostic {false};
  bool multi_label {false};
  bool box_grid_decode {false};
  bool has_objectness_score {false};
  uint32_t num_preds {0};
  uint32_t row_stride {0};
  bool transposed {false}; // output layout: false=[preds,attrs], true=[attrs,preds]
  std::vector<GridAndStride> grid_strides;
  int32_t preds_per_location {1};
  int32_t max_detections {default_max_detections};
  bool dequantize {false};
  float dequantize_scale {1.0f};
  float dequantize_offset {0.0f};
};

static inline std::string trim_copy(std::string s) {
    const auto not_space = [](unsigned char c) { return !std::isspace(c); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_space));
    s.erase(std::find_if(s.rbegin(), s.rend(), not_space).base(), s.end());
    return s;
}

static std::vector<std::string>
read_class_labels_file(const std::string& path, VvasLogger* logger_handle)
{
    std::vector<std::string> labels;
    if (path.empty()) return labels;

    std::ifstream f(path);
    if (!f.is_open()) {
        LOG_WARNING_OBJ(logger_handle,
                        "Failed to open class_label_file='%s' (falling back to built-in COCO labels)",
                        path.c_str());
        return labels;
    }

    std::string line;
    while (std::getline(f, line)) {
        // Handle Windows CRLF.
        if (!line.empty() && line.back() == '\r') line.pop_back();
        line = trim_copy(std::move(line));
        if (line.empty()) continue;
        if (!line.empty() && line[0] == '#') continue;
        labels.emplace_back(std::move(line));
    }

    if (labels.empty()) {
        LOG_WARNING_OBJ(logger_handle,
                     "class_label_file='%s' contained no labels (falling back to built-in COCO labels)",
                     path.c_str());
    } else {
        LOG_INFO_OBJ(logger_handle, "Loaded %zu class labels from '%s'", labels.size(), path.c_str());
    }
    return labels;
}

static std::vector<GridAndStride>
generate_grids_and_strides(int32_t input_w, int32_t input_h,
    int32_t preds_per_loc, const std::vector<int32_t>& strides)
{
    std::vector<GridAndStride> grid_strides;
    for (int32_t s : strides) {
        int32_t num_grid_w = input_w / s;
        int32_t num_grid_h = input_h / s;
        for (int32_t gy = 0; gy < num_grid_h; ++gy) {
            for (int32_t gx = 0; gx < num_grid_w; ++gx) {
                for(int32_t pr = 0; pr < preds_per_loc; ++pr){
                    grid_strides.push_back(GridAndStride{gx, gy, s});
                }
            }
        }
    }
    return grid_strides;
}

template<typename T>
static inline T sigmoid(const T& x)
{
    // Avoid ambiguous overload resolution for float16/bfloat16 by doing math in float.
    const float xf = static_cast<float>(x);
    if (xf >= 0.0f) {
        const float z = std::exp(-xf);
        return static_cast<T>(1.0f / (1.0f + z));
    } else {
        const float z = std::exp(xf);
        return static_cast<T>(z / (1.0f + z));
    }
}

static inline float iou_xyxy(const BBox& a, const BBox& b) {
    const float xx1 = std::max(a.getTL().x, b.getTL().x);
    const float yy1 = std::max(a.getTL().y, b.getTL().y);
    const float xx2 = std::min(a.getBR().x, b.getBR().x);
    const float yy2 = std::min(a.getBR().y, b.getBR().y);
    const float w = std::max(0.0f, xx2 - xx1);
    const float h = std::max(0.0f, yy2 - yy1);
    const float inter = w * h;
    const float areaA = std::max(0.0f, a.getBR().x - a.getTL().x) * std::max(0.0f, a.getBR().y - a.getTL().y);
    const float areaB = std::max(0.0f, b.getBR().x - b.getTL().x) * std::max(0.0f, b.getBR().y - b.getTL().y);
    return inter / (areaA + areaB - inter + 1e-7f);
}

// Single-pass NMS (offset trick handles class separation)
static std::vector<Detection>
 nms(const std::vector<Detection>& dets,
            float iou_thres,
            int32_t max_det)
{
    std::vector<Detection> out;
    if (dets.empty()) return out;

    // Sort indices by confidence descending
    std::vector<int32_t> idx(dets.size());
    for (size_t i = 0; i < dets.size(); ++i)
        idx[i] = static_cast<int>(i);

    std::sort(idx.begin(), idx.end(), [&](int32_t a, int32_t b) {
        return dets[a].conf > dets[b].conf;
    });

    std::vector<uint8_t> removed(dets.size(), 0);
    for (size_t _i = 0; _i < idx.size(); ++_i) {
        int32_t i = idx[_i];
        if (removed[i]) continue;

        out.push_back(dets[i]);
        if ((int32_t)out.size() >= max_det) break;

        for (size_t _j = _i + 1; _j < idx.size(); ++_j) {
            int32_t j = idx[_j];
            if (removed[j]) continue;
            if (iou_xyxy(dets[i].box, dets[j].box) > iou_thres) {
                removed[j] = 1;
            }
        }
    }
    return out;
}

template<bool Decode, typename T>
BBox get_box(const T& cx, const T& cy, const T& w, const T& h, const GridAndStride& gs)
{
    if constexpr (Decode) {
        // Cast before exp() to avoid ambiguous overloads for float16/bfloat16.
        const float wf = std::exp(static_cast<float>(w)) * static_cast<float>(gs.stride);
        const float hf = std::exp(static_cast<float>(h)) * static_cast<float>(gs.stride);
        return BBox(Point(static_cast<float>(cx + gs.gridx * gs.stride),
                          static_cast<float>(cy + gs.gridy * gs.stride)),
                    wf, hf);
    } else {
        return BBox(Point(static_cast<float>(cx), static_cast<float>(cy)),
                    static_cast<float>(w), static_cast<float>(h));
    }
}

template <typename T>
static inline float to_f32(const T v) {
    if constexpr (std::is_same_v<T, int8_t>) {
        return static_cast<float>(static_cast<int>(v));
    } else {
        return static_cast<float>(v);
    }
}

template <typename T>
struct RowReader {
    const T* base {nullptr};
    int32_t pred_idx {0};
    int32_t num_preds {0};
    int32_t row_stride {0}; // num attrs
    float inv_scale {1.0f};   // 1 / dequantize_scale
    bool dequantize {false};

    template <bool Transposed>
    inline float raw(const int32_t idx) const {
        // Non-transposed: [pred, attr] => contiguous per prediction
        // Transposed:     [attr, pred] => contiguous per attribute
        float v;
        if constexpr (!Transposed) {
            v = to_f32(base[pred_idx * row_stride + idx]);
        } else {
            v = to_f32(base[idx * num_preds + pred_idx]);
        }
        if (dequantize) v *= inv_scale;
        return v;
    }

    template <bool ApplySigmoid, bool Transposed>
    inline float score(const int32_t idx) const {
        const float v = raw<Transposed>(idx);
        if constexpr (ApplySigmoid) {
            return sigmoid(v);
        } else {
            return v;
        }
    }
};

template <typename T,
          bool ApplySigmoid,
          bool MultiLabel,
          bool BoxGridDecode,
          bool HasObjectness,
          bool Transposed>
static std::vector<Detection>
yolo_postprocess_impl(const T* input_tensor,
                      int32_t num_preds,
                      const int32_t num_classes,
                      const int32_t row_stride,
                      YoloPostProcessParams& params)
{
    const float max_wh = 7680.0f; // must exceed max image dimension
    std::vector<Detection> candidates;
    candidates.reserve(std::min<int32_t>(num_preds, 1024));

    const float conf_thresh = static_cast<float>(params.conf_thresh);

    const bool deq = params.dequantize;
    const float inv_scale =
        (deq && params.dequantize_scale != 0.0f) ? (1.0f / params.dequantize_scale) : 1.0f;

    START_PROFILE("yolo_postprocess_prepcand_v2", "Post process :: candidate preparation (v2)");
    for (int32_t i = 0; i < num_preds; ++i) {
        RowReader<T> rr;
        rr.base = input_tensor;
        rr.pred_idx = i;
        rr.num_preds = num_preds;
        rr.row_stride = row_stride;
        rr.inv_scale = inv_scale;
        rr.dequantize = deq;

        float obj_conf = 1.0f;
        if constexpr (HasObjectness) {
            obj_conf = rr.template score<ApplySigmoid, Transposed>(4);
            // Optimization: if obj_conf itself is below threshold, no class can lift it above threshold.
            // (This assumes class scores are in [0, 1] when ApplySigmoid==true; matches typical YOLO heads.)
            if (obj_conf < conf_thresh) continue;
        }

        // Boxes: dequantize if needed, never sigmoid.
        const float cx = rr.template raw<Transposed>(0);
        const float cy = rr.template raw<Transposed>(1);
        const float w  = rr.template raw<Transposed>(2);
        const float h  = rr.template raw<Transposed>(3);

        BBox base_box;
        if constexpr (BoxGridDecode) {
            // Only touch grid_strides if decoding is enabled (avoids invalid access when disabled).
            base_box = get_box<true>(cx, cy, w, h, params.grid_strides[i]);
        } else {
            // Avoid indexing params.grid_strides when not decoding.
            const GridAndStride dummy{0, 0, 0};
            base_box = get_box<false>(cx, cy, w, h, dummy);
        }

        constexpr int32_t score_offset = HasObjectness ? 1 : 0;
        const int32_t cls_base = 4 + score_offset;

        if constexpr (MultiLabel) {
            for (int32_t c = 0; c < num_classes; ++c) {
                const float cls_score = rr.template score<ApplySigmoid, Transposed>(cls_base + c);
                const float conf = obj_conf * cls_score;
                if (conf < conf_thresh) continue;

                BBox box = base_box;
                candidates.emplace_back(box, conf, c);
            }
        } else {
            // Best class only
            int32_t best_cls = -1;
            float best_raw_score = -std::numeric_limits<float>::infinity();
            for (int32_t c = 0; c < num_classes; ++c) {
                // If ApplySigmoid==true, sigmoid is monotonic so argmax is same on raw scores.
                const float v = rr.template raw<Transposed>(cls_base + c);
                if (v > best_raw_score) {
                    best_raw_score = v;
                    best_cls = c;
                }
            }

            if (best_cls >= 0) {
                float best_score;
                if constexpr (ApplySigmoid) {
                    best_score = sigmoid(best_raw_score);
                } else {
                    best_score = best_raw_score;
                }
                const float conf = obj_conf * best_score;
                if (conf >= conf_thresh) {
                    BBox box = base_box;
                    candidates.emplace_back(box, conf, best_cls);
                }
            }
        }
    }
    STOP_PROFILE("yolo_postprocess_prepcand_v2");

    if (!params.class_agnostic) {
        for (auto& d : candidates) {
            d.box = d.box.addXOffset(max_wh * d.class_id);
        }
    }

    START_PROFILE("yolo_postprocess_nms_v2", "Post process :: nms (v2)");
    auto results = nms(candidates, static_cast<float>(params.iou_thresh), params.max_detections);
    if (!params.class_agnostic) {
        for (auto& d : results) {
            d.box = d.box.addXOffset(-max_wh * d.class_id);
        }
    }
    STOP_PROFILE("yolo_postprocess_nms_v2");

    return results;
}

enum : uint8_t {
    kSigmoid  = 1u << 0,
    kMulti    = 1u << 1,
    kDecode   = 1u << 2,
    kObj      = 1u << 3,
    kTransposed = 1u << 4,
};

template <typename T, uint8_t Mask>
static std::vector<Detection>
yolo_postprocess_dispatch_mask(const T* input_tensor,
                               int32_t num_preds,
                               const int32_t num_classes,
                               const int32_t row_stride,
                               YoloPostProcessParams& params)
{
    constexpr bool ApplySigmoid  = (Mask & kSigmoid) != 0;
    constexpr bool MultiLabel    = (Mask & kMulti) != 0;
    constexpr bool BoxGridDecode = (Mask & kDecode) != 0;
    constexpr bool HasObjectness = (Mask & kObj) != 0;
    constexpr bool Transposed    = (Mask & kTransposed) != 0;

    return yolo_postprocess_impl<T, ApplySigmoid, MultiLabel, BoxGridDecode, HasObjectness, Transposed>(
        input_tensor, num_preds, num_classes, row_stride, params);
}

template <typename T>
static std::vector<Detection>
 yolo_postprocess_v2(const T* input_tensor,
                     int32_t num_preds,
                     const int32_t num_classes,
                     const int32_t row_stride,
                     YoloPostProcessParams& params)
{
    const uint8_t mask =
        (params.apply_sigmoid ? kSigmoid : 0) |
        (params.multi_label ? kMulti : 0) |
        (params.box_grid_decode ? kDecode : 0) |
        (params.has_objectness_score ? kObj : 0) |
        (params.transposed ? kTransposed : 0);

    switch (mask) {
#define YPP_CASE(M)                                                                                 \
        case (uint8_t)(M):                                                                          \
            return yolo_postprocess_dispatch_mask<T, (uint8_t)(M)>(                                 \
                input_tensor, num_preds, num_classes, row_stride, params)
        YPP_CASE(0);  YPP_CASE(1);  YPP_CASE(2);  YPP_CASE(3);
        YPP_CASE(4);  YPP_CASE(5);  YPP_CASE(6);  YPP_CASE(7);
        YPP_CASE(8);  YPP_CASE(9);  YPP_CASE(10); YPP_CASE(11);
        YPP_CASE(12); YPP_CASE(13); YPP_CASE(14); YPP_CASE(15);
        YPP_CASE(16); YPP_CASE(17); YPP_CASE(18); YPP_CASE(19);
        YPP_CASE(20); YPP_CASE(21); YPP_CASE(22); YPP_CASE(23);
        YPP_CASE(24); YPP_CASE(25); YPP_CASE(26); YPP_CASE(27);
        YPP_CASE(28); YPP_CASE(29); YPP_CASE(30); YPP_CASE(31);
        default:
            // Should be unreachable (mask is 5 bits), but keep a safe fallback.
            return yolo_postprocess_dispatch_mask<T, 0>(
                input_tensor, num_preds, num_classes, row_stride, params);
#undef YPP_CASE
    }
}

static vector<VvasTensorInfo>
read_tensor_info(uint32_t num_tensors, VvasTensorInfo** t_info, VvasLogger* logger_handle)
{
    vector<VvasTensorInfo> info;
    LOG_DEBUG_OBJ(logger_handle, "Num tensors = %u", num_tensors);
    for(uint32_t i=0; i < num_tensors; i++){
        VvasTensorInfo& tmp = info.emplace_back();

        tmp.size = t_info[i]->size;
        tmp.scale_coeff = t_info[i]->scale_coeff;
        tmp.valid_shapes = t_info[i]->valid_shapes;
        tmp.data_type = t_info[i]->data_type;
        tmp.name = t_info[i]->name ? strdup(t_info[i]->name) : nullptr;
        for(uint32_t j=0; j<tmp.valid_shapes; j++)
            tmp.shape[j] = t_info[i]->shape[j];
        std::ostringstream shapes;
        shapes << "[ ";
        for (int32_t d = 0; d < MAX_SHAPE_SIZE; ++d) {
            shapes << tmp.shape[d] << " ";
        }
        shapes << "]";
        LOG_DEBUG_OBJ(logger_handle,
                      "Tensor %u :: name=%s data_type=%d size=%u scale_coeff=%f valid_shapes=%u shape=%s",
                      i, tmp.name ? tmp.name : "(null)", (int)tmp.data_type, tmp.size, tmp.scale_coeff, tmp.valid_shapes, shapes.str().c_str());
    }
    return info;
}

static YoloPostProcessParams
read_postprocess_config(char *json_conf,
                        VvasLogger* &logger_handle,
                        VvasLogLevel &log_level,
                        std::string& class_label_file)
{
    YoloPostProcessParams params;

    boost::property_tree::ptree pt;
    try {
        std::istringstream json_stream(json_conf);
        boost::property_tree::read_json(json_stream, pt);
    } catch (const std::exception& e) {
        // Logger is not set up yet, fall back to process-wide VVAS logging.
        LOG_ERROR(DEFAULT_VVAS_LOG_LEVEL, "Error reading JSON : %s", e.what());
        throw;
    }

    // Create logger handle early so we can route all logs through VVAS logging.
    int json_log_level = pt.get<int>("log_level", (int)DEFAULT_VVAS_LOG_LEVEL);
    if (json_log_level < (int)LOG_LEVEL_NONE) json_log_level = (int)LOG_LEVEL_NONE;
    if (json_log_level > (int)LOG_LEVEL_DEBUG) json_log_level = (int)LOG_LEVEL_DEBUG;
    log_level = (VvasLogLevel)json_log_level;

    char *module_name = nullptr;
    logger_handle = vvas_logger_register(CORE_POST_PROCESS, log_level, &module_name);
    if (!logger_handle) {
        LOG_ERROR(log_level, "Failed to register YOLO postprocess with VVAS logger");
        throw std::runtime_error("vvas_logger_register failed");
    }
    g_yolo_pp_log_level = vvas_logger_get_log_level(logger_handle);
    g_yolo_pp_logger_handle = logger_handle;

    params.conf_thresh = pt.get<double>("conf_thresh", default_conf_thresh);
    params.iou_thresh = pt.get<double>("iou_thresh", default_iou_thresh);
    params.class_agnostic = pt.get<bool>("class_agnostic", false);
    params.multi_label = pt.get<bool>("multi_label", false);
    params.box_grid_decode = pt.get<bool>("box_grid_decode", false);
    params.preds_per_location = pt.get<int>("preds_per_location", 1);
    params.apply_sigmoid = pt.get<bool>("apply_sigmoid", false);
    params.has_objectness_score = pt.get<bool>("has_objectness_score", true);
    params.max_detections= pt.get<int>("max_detections", default_max_detections);
    class_label_file = pt.get<std::string>("class_label_file", "");
    LOG_DEBUG_OBJ(logger_handle, "conf_thresh=%f", params.conf_thresh);
    LOG_DEBUG_OBJ(logger_handle, "iou_thresh=%f", params.iou_thresh);
    LOG_DEBUG_OBJ(logger_handle, "class_agnostic=%d", (int)params.class_agnostic);
    LOG_DEBUG_OBJ(logger_handle, "multi_label=%d", (int)params.multi_label);
    LOG_DEBUG_OBJ(logger_handle, "box_grid_decode=%d", (int)params.box_grid_decode);
    LOG_DEBUG_OBJ(logger_handle, "preds_per_location=%d", (int)params.preds_per_location);
    LOG_DEBUG_OBJ(logger_handle, "apply_sigmoid=%d", (int)params.apply_sigmoid);
    LOG_DEBUG_OBJ(logger_handle, "has_objectness_score=%d", (int)params.has_objectness_score);
    LOG_DEBUG_OBJ(logger_handle, "max_detections=%d", (int)params.max_detections);
    LOG_DEBUG_OBJ(logger_handle, "log_level=%d", (int)log_level);
    if (!class_label_file.empty()) {
        LOG_INFO_OBJ(logger_handle, "class_label_file=%s", class_label_file.c_str());
    }

    return params;
}
struct YoloPriv{
  uint32_t num_tensors;
  vector<VvasTensorInfo> info;
  YoloPostProcessParams params;
  std::string class_label_file;
  std::vector<std::string> class_labels; // one label per class_id
  VvasLogger* logger_handle {nullptr};
  VvasLogLevel log_level {DEFAULT_VVAS_LOG_LEVEL};
};

struct ParsedYoloOutputShape {
    uint32_t num_preds {0};
    uint32_t num_attrs {0};
    bool transposed {false}; // true if [attrs, preds], false if [preds, attrs]
    uint32_t preds_dim_index {0};
    uint32_t attrs_dim_index {0};
};

static std::optional<ParsedYoloOutputShape>
parse_yolo_output_shape(const VvasTensorInfo& info,
                        const uint32_t min_expected_attrs,
                        VvasLogger* logger_handle)
{
    // Supported shapes:
    // - [preds, attrs, 0, 0, 0]
    // - [1, preds, attrs, 0, 0]
    // - [attrs, preds, 0, 0, 0]   (transposed)
    // - [1, attrs, preds, 0, 0]   (transposed)
    struct Dim { uint32_t idx; uint32_t val; };
    std::vector<Dim> dims;
    dims.reserve(MAX_SHAPE_SIZE);
    for (uint32_t d = 0; d < MAX_SHAPE_SIZE; ++d) {
        const uint32_t v = info.shape[d];
        if (v > 1) dims.push_back({d, v}); // ignore 0 and 1 (batch)
    }

    if (dims.size() < 2) {
        std::ostringstream shapes;
        for (int32_t d = 0; d < MAX_SHAPE_SIZE; d++) shapes << info.shape[d] << " ";
        LOG_ERROR_OBJ(logger_handle, "Invalid/unsupported shape: %s", shapes.str().c_str());
        return std::nullopt;
    }

    ParsedYoloOutputShape out;
    // Pick the two largest dims as [preds, attrs] and validate attrs size.
    // This supports padding, e.g. attrs=88 while min_expected_attrs=85.
    std::sort(dims.begin(), dims.end(), [](const Dim& a, const Dim& b){ return a.val > b.val; });
    const Dim preds_dim = dims[0];
    const Dim attrs_dim = dims[1];
    if (attrs_dim.val < min_expected_attrs) {
        std::ostringstream shapes;
        for (int32_t d = 0; d < MAX_SHAPE_SIZE; d++) shapes << info.shape[d] << " ";
        LOG_ERROR_OBJ(logger_handle,
                      "Unsupported attrs dimension: expected_at_least=%u got=%u (shape: %s). "
                      "If your model has different class count, update num_classes/labels accordingly.",
                      min_expected_attrs, attrs_dim.val, shapes.str().c_str());
        return std::nullopt;
    }
    out.num_preds = preds_dim.val;
    out.num_attrs = attrs_dim.val;
    out.attrs_dim_index = attrs_dim.idx;
    out.preds_dim_index = preds_dim.idx;
    out.transposed = out.attrs_dim_index < out.preds_dim_index;
    return out;
}

#ifdef __cplusplus
extern "C" {
#endif

void* postprocess_init(char* json_conf,
        VvasTensorInfo** t_info,
        uint32_t num_valid_tensors)
{
    std::unique_ptr<YoloPriv> pp_private = std::make_unique<YoloPriv>();

    pp_private->num_tensors = num_valid_tensors;
    auto cleanup_logger = [&]() {
        if (pp_private->logger_handle) {
            vvas_logger_deregister(pp_private->logger_handle);
            pp_private->logger_handle = nullptr;
        }
    };
    try {
        pp_private->params = read_postprocess_config(json_conf,
                                                     pp_private->logger_handle,
                                                     pp_private->log_level,
                                                     pp_private->class_label_file);
    } catch (const std::exception&) {
        cleanup_logger();
        return nullptr;
    }

    pp_private->info = read_tensor_info(num_valid_tensors, t_info, pp_private->logger_handle);

    // Load class labels (optional override). If missing/invalid, fall back to built-in COCO labels.
    pp_private->class_labels = coco_names;
    if (!pp_private->class_label_file.empty()) {
        auto labels = read_class_labels_file(pp_private->class_label_file, pp_private->logger_handle);
        if (!labels.empty()) {
            pp_private->class_labels = std::move(labels);
        }
    }

    // Determine output layout and dims
    const uint32_t expected_attrs =
        4u + (pp_private->params.has_objectness_score ? 1u : 0u) + (uint32_t)pp_private->class_labels.size();
    auto parsed = parse_yolo_output_shape(pp_private->info[0], expected_attrs, pp_private->logger_handle);
    if (!parsed) {
        LOG_ERROR_OBJ(pp_private->logger_handle, "Failed to parse output tensor shape");
        cleanup_logger();
        return nullptr;
    }
    pp_private->params.num_preds = parsed->num_preds;
    pp_private->params.row_stride = parsed->num_attrs;
    pp_private->params.transposed = parsed->transposed;
    LOG_INFO_OBJ(pp_private->logger_handle,
                 "YOLO output layout: %s, num_preds=%u, num_attrs=%u (attrs_dim_index=%u preds_dim_index=%u)",
                 pp_private->params.transposed ? "[attrs,preds]" : "[preds,attrs]",
                 pp_private->params.num_preds,
                 pp_private->params.row_stride,
                 parsed->attrs_dim_index,
                 parsed->preds_dim_index);

    // Prepare grid/stride list
    if(pp_private->params.box_grid_decode){
        // TODO :: Make this a parameter and read from json configuration file.
        std::vector<int> strides {8, 16, 32};
        pp_private->params.grid_strides = generate_grids_and_strides(image_input_width, image_input_height,
                            pp_private->params.preds_per_location, strides);

        if ((uint32_t)pp_private->params.grid_strides.size() != pp_private->params.num_preds) {
            // Safety check in case shapes don’t align
            LOG_ERROR_OBJ(pp_private->logger_handle,
                          "Mismatch: grid_strides.size()=%zu vs num_preds=%u",
                          pp_private->params.grid_strides.size(),
                          pp_private->params.num_preds);
            cleanup_logger();
            return nullptr;
        }
    }

    return pp_private.release();
}

VvasReturnType postprocess_run(void* pp_private,
                VvasMemory** tensor_memory,
                uint32_t cur_batch_size,
                VvasList** res)
{
    YoloPriv* pp_handle = static_cast<YoloPriv*>(pp_private);

    for (uint32_t i = 0; i < cur_batch_size; i++) {
        const uint64_t frame_id = g_yolo_pp_frame_counter.fetch_add(1, std::memory_order_relaxed);
        VvasMemoryMapInfo map_info = {};
        if (VVAS_RET_SUCCESS !=
            vvas_memory_map(tensor_memory[i], VVAS_DATA_MAP_READ, &map_info)) {
            LOG_ERROR_OBJ(pp_handle->logger_handle, "Failed to map tensor memory for read");
            return VVAS_RET_ERROR;
        }

        START_PROFILE("postprocess_run_inpcpy", "Post process :: input copy");

        STOP_PROFILE("postprocess_run_inpcpy");
        START_PROFILE("postprocess_run_pptotal", "Post process :: yolo_postprocess function total");

        std::vector<Detection> detections;
        const auto& class_labels = pp_handle->class_labels.empty() ? coco_names : pp_handle->class_labels;
        if (pp_handle->info[0].data_type == VVAS_TENSOR_DATA_TYPE_FLOAT32) {
            detections = yolo_postprocess_v2(reinterpret_cast<const float*>(map_info.data),
                pp_handle->params.num_preds, class_labels.size(),
                pp_handle->params.row_stride, pp_handle->params);
        } else if (pp_handle->info[0].data_type == VVAS_TENSOR_DATA_TYPE_INT8) {
            pp_handle->params.dequantize = true;
            pp_handle->params.dequantize_scale = pp_handle->info[0].scale_coeff;
            pp_handle->params.dequantize_offset = 0.0f;
            detections = yolo_postprocess_v2(reinterpret_cast<const int8_t*>(map_info.data),
                pp_handle->params.num_preds, class_labels.size(),
                pp_handle->params.row_stride, pp_handle->params);
        }else if (pp_handle->info[0].data_type == VVAS_TENSOR_DATA_TYPE_BF16) {
            detections = yolo_postprocess_v2(reinterpret_cast<const std::bfloat16_t*>(map_info.data),
                pp_handle->params.num_preds, class_labels.size(),
                pp_handle->params.row_stride, pp_handle->params);
        } else if (pp_handle->info[0].data_type == VVAS_TENSOR_DATA_TYPE_FP16) {
            detections = yolo_postprocess_v2(reinterpret_cast<const std::float16_t*>(map_info.data),
                pp_handle->params.num_preds, class_labels.size(),
                pp_handle->params.row_stride, pp_handle->params);
        } else {
            LOG_ERROR_OBJ(pp_handle->logger_handle, "Unknown VvasTensorDataType %d", (int)pp_handle->info[0].data_type);
            vvas_memory_unmap(tensor_memory[i], &map_info);
            return VVAS_RET_ERROR;
        }

        STOP_PROFILE("postprocess_run_pptotal");
        START_PROFILE("postprocess_run_prepout", "Post process :: Results copy");

        LOG_DEBUG_OBJ(pp_handle->logger_handle,
                      "Frame %" PRIu64 " (batch_idx=%u): selected_boxes=%zu BEGIN",
                      frame_id, i, detections.size());

        size_t det_idx = 0;
        for (const auto& d : detections) {
            if (((d.box.getBR().x - d.box.getTL().x) > 0) && (d.box.getBR().y - d.box.getTL().y > 0)) {
                LOG_DEBUG_OBJ(pp_handle->logger_handle,
                              "  [%zu] cls=%d (%s) conf=%f box=[%f,%f,%f,%f] w=%f h=%f",
                              det_idx,
                              d.class_id,
                              (d.class_id >= 0 && (size_t)d.class_id < class_labels.size())
                                  ? class_labels[d.class_id].c_str()
                                  : "(unknown)",
                              d.conf,
                              d.box.getTL().x, d.box.getTL().y, d.box.getBR().x, d.box.getBR().y,
                              (d.box.getBR().x - d.box.getTL().x),
                              (d.box.getBR().y - d.box.getTL().y));
                det_idx++;
                VvasInferResult* infer_result = vvas_infer_result_detection_create();
                VvasInferDetection* det = (VvasInferDetection*)infer_result->data;
                det->bbox.x = std::max(0, (int32_t)std::round(d.box.getTL().x));
                det->bbox.y = std::max(0, (int32_t)std::round(d.box.getTL().y));
                det->bbox.width = std::max(0, (int32_t)std::round(d.box.getBR().x - d.box.getTL().x));
                det->bbox.height = std::max(0, (int32_t)std::round(d.box.getBR().y - d.box.getTL().y));
                det->class_id = d.class_id;
                det->probability = d.conf;
                const char* lbl =
                    (d.class_id >= 0 && (size_t)d.class_id < class_labels.size())
                        ? class_labels[d.class_id].c_str()
                        : "unknown";
                det->label = strdup(lbl);
                infer_result->infer_result_type = VVAS_INFER_RESULT_DETECTION;
                res[i] = vvas_list_append(res[i], infer_result);
            }
        }
        LOG_DEBUG_OBJ(pp_handle->logger_handle,
                      "Frame %" PRIu64 " (batch_idx=%u): appended_boxes=%zu END",
                      frame_id, i, det_idx);

        STOP_PROFILE("postprocess_run_prepout");

        vvas_memory_unmap(tensor_memory[i], &map_info);
    }
    return VVAS_RET_SUCCESS;
}

VvasReturnType postprocess_deinit(void* pp_private)
{
    YoloPriv* pp_handle = static_cast<YoloPriv*>(pp_private);
    if (pp_handle) {
        for (auto &ti : pp_handle->info) {
            if (ti.name) {
                free(ti.name);
                ti.name = nullptr;
            }
        }
    }
    if (pp_handle && pp_handle->logger_handle) {
        VvasLogger* old = pp_handle->logger_handle;
        vvas_logger_deregister(pp_handle->logger_handle);
        pp_handle->logger_handle = nullptr;
        if (g_yolo_pp_logger_handle == old) {
            g_yolo_pp_logger_handle = nullptr;
        }
    }
    delete pp_handle;

#ifdef PROFILE
    profilers.clear();
#endif

    return VVAS_RET_SUCCESS;
}
#ifdef __cplusplus
}  //extern "C"
#endif
