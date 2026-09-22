#!/usr/bin/env python3
"""
YOLOX Preprocessing Script
Takes an image ID and returns/saves the preprocessed numpy array based on model input shape.
"""

import argparse
import numpy as np
import cv2
import os
from pathlib import Path


def custom_preproc(img, input_size, swap=(2, 0, 1)):
    """
    Custom implementation of YOLOX preproc function.
    Resizes image while maintaining aspect ratio and pads to target size.
    
    Args:
        img: Input image (BGR format from cv2)
        input_size: Target size (height, width)
        swap: Axis swap for channel ordering (default: (2, 0, 1) for HWC to CHW)
    
    Returns:
        padded_img: Preprocessed image array (CHW format, float32)
        ratio: Resize ratio used
    """
    if len(img.shape) == 3:
        padded_img = np.ones((input_size[0], input_size[1], 3), dtype=np.uint8) * 114
    else:
        padded_img = np.ones(input_size, dtype=np.uint8) * 114

    # Calculate resize ratio to maintain aspect ratio
    r = min(input_size[0] / img.shape[0], input_size[1] / img.shape[1])
    
    # Resize image
    resized_img = cv2.resize(
        img,
        (int(img.shape[1] * r), int(img.shape[0] * r)),
        interpolation=cv2.INTER_LINEAR,
    ).astype(np.uint8)
    
    # Place resized image in padded canvas
    padded_img[: int(img.shape[0] * r), : int(img.shape[1] * r)] = resized_img

    # Transpose to CHW format and make contiguous
    padded_img = padded_img.transpose(swap)
    padded_img = np.ascontiguousarray(padded_img, dtype=np.float32)
    
    return padded_img, r


def preprocess_image(image_path, input_shape, output_path=None):
    """
    Preprocess a single image for YOLOX inference.
    
    Args:
        image_path: Path to input image
        input_shape: Model input shape (height, width)
        output_path: Optional path to save preprocessed array
    
    Returns:
        Dictionary containing:
            - preprocessed_img: Preprocessed image array (1, C, H, W)
            - ratio: Resize ratio
            - orig_shape: Original image shape (H, W)
            - image_path: Path to original image
    """
    # Load image
    img = cv2.imread(image_path)
    if img is None:
        raise ValueError(f"Could not load image from {image_path}")
    
    orig_shape = img.shape[:2]  # (height, width)
    
    # Preprocess using custom function
    preprocessed_img, ratio = custom_preproc(img, input_shape)
    
    # Add batch dimension
    preprocessed_img = preprocessed_img[np.newaxis, :]  # (1, C, H, W)
    
    # Save if output path provided
    if output_path:
        np.save(output_path, preprocessed_img)
        print(f"Saved preprocessed array to: {output_path}")
    
    result = {
        'preprocessed_img': preprocessed_img,
        'ratio': ratio,
        'orig_shape': orig_shape,
        'image_path': image_path
    }
    
    return result


def parse_args():
    parser = argparse.ArgumentParser(description="Preprocess image for YOLOX inference")
    parser.add_argument("--image", type=str, required=True, help="Path to input image")
    parser.add_argument("--input-shape", type=str, required=True, 
                       help="Model input shape as 'height,width' (e.g., '416,416')")
    parser.add_argument("--output", type=str, default=None,
                       help="Optional path to save preprocessed .npy file")
    return parser.parse_args()


def main():
    args = parse_args()
    
    # Parse input shape
    input_shape = tuple(map(int, args.input_shape.split(',')))
    
    print(f"Preprocessing image: {args.image}")
    print(f"Input shape: {input_shape}")
    
    # Preprocess
    result = preprocess_image(args.image, input_shape, args.output)
    
    print(f"Original shape: {result['orig_shape']}")
    print(f"Preprocessed shape: {result['preprocessed_img'].shape}")
    print(f"Resize ratio: {result['ratio']:.4f}")
    
    return result


if __name__ == "__main__":
    main()
