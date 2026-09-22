#!/usr/bin/env
"""
 Copyright (C) 2025 AMD, Inc
 Author(s): Brian Xu (brianx@amd.com), Advait Naik (advanaik@amd.com)

 Licensed under the Apache License, Version 2.0 (the "License"). You may
 not use this file except in compliance with the License. A copy of the
 License is located at

    http://www.apache.org/licenses/LICENSE-2.0

 Unless required by applicable law or agreed to in writing, software
 distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
 License for the specific language governing permissions and limitations
 under the License.
"""

import sys
import struct
import json
import os
import argparse
import textwrap
from argparse import RawTextHelpFormatter

LOG_ENTRY_SIZE = 0x20 
LOG_HDR_SIZE = LOG_ENTRY_SIZE

class Colors:
    HEADER = '\033[95m'
    OKBLUE = '\033[94m'
    OKCYAN = '\033[96m'
    OKGREEN = '\033[92m'
    WARNING = '\033[93m'
    FAIL = '\033[91m'
    ENDC = '\033[0m'
    BOLD = '\033[1m'
    UNDERLINE = '\033[4m'

def read_binary_file(file_path):
    with open(file_path, "rb") as f:
        data = f.read()
        return data, len(data)

def parse_log_entries(log_buffer, log_buf_size, log_map_file, verbose=1):
    """Parses and prints log entries from the binary log buffer."""

    with open(log_map_file, "r") as f:
        log_map = json.load(f)["log"]
    
    with open(log_map_file, "r") as f:
        file_map = json.load(f)["file"]

    buf_total_low, buf_total_high = struct.unpack_from("<II", log_buffer, 0)
    buf_total = (buf_total_high << 32) | buf_total_low

    log_buf_entry_size = log_buf_size - LOG_HDR_SIZE
    buf_entries = min(buf_total, log_buf_entry_size)
    start_index = (buf_total % log_buf_entry_size) if buf_total > log_buf_entry_size else 0

    print(f"{Colors.OKBLUE}Log size (bytes): {Colors.HEADER}{buf_total}{Colors.ENDC}")
    print(f"{Colors.OKBLUE}Log buf entry size (bytes): {Colors.HEADER}{log_buf_entry_size}{Colors.ENDC}")
    if verbose >= 2:
        print(f"{Colors.OKBLUE}Log buffer start index: {Colors.HEADER}{start_index}{Colors.ENDC}")

    index = start_index + LOG_HDR_SIZE
    processed_entries = 0

    while processed_entries <= buf_entries:
        entry = log_buffer[index:index + LOG_ENTRY_SIZE]
        if len(entry) < LOG_ENTRY_SIZE:
            break

        length, ts_high, ts_low, file_id, line_num, log_id = struct.unpack_from("<IIIIII", entry, 0)
        args = struct.unpack_from("<II", entry, 24) if length > 6 else ()

        if verbose >= 3:
            print(f"{Colors.OKBLUE}Length: {Colors.OKCYAN}{length}{Colors.ENDC}")
            print(f"{Colors.OKBLUE}File ID: {Colors.OKCYAN}{file_id}{Colors.ENDC}")
            
        if verbose >= 2:
            file_name = next((item["name"] for item in file_map if item["id"] == file_id), None)
            print(f"{Colors.OKBLUE}File Name: {Colors.OKCYAN}{file_name}{Colors.ENDC}")
            print(f"{Colors.OKBLUE}Line Number: {Colors.OKCYAN}{line_num}{Colors.ENDC}")
            print(f"{Colors.OKBLUE}Timestamp: {Colors.OKCYAN}0x{ts_high:08X} : 0x{ts_low:08X}{Colors.ENDC}")

        if verbose >= 3:
            print(f"{Colors.OKBLUE}Log ID: {Colors.OKCYAN}{log_id}{Colors.ENDC}")

        log_format = next((item["name"] for item in log_map if item["id"] == log_id), None)
        if log_format:
            log_format = f"{Colors.OKGREEN}[CERT] {Colors.ENDC}" + log_format
            if length == 6:
                print(log_format, end='')
            elif length == 7:
                print(log_format % (args[0]), end='')
            elif length == 8:
                print(log_format % (args[0], args[1]), end='')

        if verbose >= 2:
            print(f"{Colors.WARNING}----------------------------------{Colors.ENDC}")

        processed_entries += LOG_ENTRY_SIZE
        index += LOG_ENTRY_SIZE

        if (index == buf_entries + LOG_HDR_SIZE):
            index = 0

def main():
    desc = textwrap.dedent('''\
        This cmdline tool parse and print CERT logs from a binary file.
    ''')
    parser=argparse.ArgumentParser(description=desc,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('-i', '--input', dest='input', required=True, 
        help="Path to the binary log file")
    parser.add_argument('-v', '--verbose', dest='verbose', type=int, choices=[1, 2, 3], default=1, 
        help="Enable verbose/debug mode")
    args = parser.parse_args()

    if not os.path.exists(args.input):
        print(f"{Colors.FAIL}Error: File '{args.input}' does not exist.{Colors.ENDC}")
        sys.exit(1)

    log_buffer, length = read_binary_file(args.input)

    log_map_file = os.path.join(os.path.dirname(__file__), ".", "log_map.json")
    if not os.path.exists(log_map_file):
        print(f"{Colors.FAIL}Error: Log map file '{log_map_file}' does not exist.{Colors.ENDC}")
        sys.exit(1)

    parse_log_entries(log_buffer, length, log_map_file, verbose=args.verbose)

if __name__ == "__main__":
    main()
