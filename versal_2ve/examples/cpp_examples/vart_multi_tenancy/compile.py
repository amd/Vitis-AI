import onnxruntime
import argparse
import os

def main():
    # Set up argument parser
    parser = argparse.ArgumentParser(description='Run ONNX model inference with VitisAI')
    parser.add_argument('--model', '-m', 
                        type=str, 
                        required=True,
                        help='Path to ONNX model file')
    parser.add_argument('--cache_key', '-k',
                        type=str,
                        default=None,
                        help='Cache key for the model (default: model filename without extension)')
    parser.add_argument('--config', '-c',
                        type=str,
                        default='vitisai_config.json',
                        help='Path to VitisAI config file (default: vitisai_config.json)')
    parser.add_argument('--cache_dir', '-d',
                        type=str,
                        default='my_cache_dir',
                        help='Cache directory (default: my_cache_dir)')
    parser.add_argument('--target', '-t',
                        type=str,
                        default='VAIML',
                        help='Target device (default: VAIML)')
    
    args = parser.parse_args()
    
    # If cache_key not provided, use the model filename without extension
    if args.cache_key is None:
        model_basename = os.path.basename(args.model)
        args.cache_key = os.path.splitext(model_basename)[0]
    
    # Set up provider options
    provider_options_dict = {
        "config_file": args.config,
        "cache_dir":   args.cache_dir,
        "cache_key":   args.cache_key,
        "target":      args.target
    }
    
    print(f"Creating ORT inference session for model {args.model}")
    print(f"Cache key: {args.cache_key}")
    print(f"Config file: {args.config}")
    print(f"Cache directory: {args.cache_dir}")
    print(f"Target: {args.target}")
    
    session = onnxruntime.InferenceSession(
        args.model,
        providers=["VitisAIExecutionProvider"],
        provider_options=[provider_options_dict]
    )
    
    print("Session created successfully!")
    return session

if __name__ == "__main__":
    session = main()

