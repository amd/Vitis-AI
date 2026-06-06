import onnxruntime

provider_options_dict = {
    "config_file": 'vitisai_config.json',
    "cache_dir":   'vek385_cache_dir',
    "cache_key":   'resnet50-v1-12',
    "log_level":   'info',
    "ai_analyzer_visualization": True,
    "ai_analyzer_profiling": True,
    "target": "VAIML"
}

session = onnxruntime.InferenceSession(
        'models/resnet50-v1-12.onnx',
        providers=["VitisAIExecutionProvider"],
        provider_options=[provider_options_dict]
)

