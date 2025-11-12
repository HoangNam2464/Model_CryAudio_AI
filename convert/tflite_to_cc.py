import argparse
from pathlib import Path


def format_as_c_array(data: bytes, var_name: str, cols: int = 12) -> str:
    lines = []
    line = []
    for idx, byte in enumerate(data):
        line.append(f"0x{byte:02X}")
        if (idx + 1) % cols == 0:
            lines.append(", ".join(line))
            line = []
    if line:
        lines.append(", ".join(line))

    body = ",\n    ".join(lines)
    return (
        f"#include <cstddef>\n\n"
        f"alignas(16) const unsigned char {var_name}[] = {{\n"
        f"    {body}\n"
        f"}};\n\n"
        f"const std::size_t {var_name}_len = sizeof({var_name});\n"
    )


def main():
    parser = argparse.ArgumentParser(description="Convert a TFLite file into a C array for embedded use.")
    parser.add_argument("tflite_path", type=Path, help="Path to the .tflite model.")
    parser.add_argument(
        "--var_name",
        type=str,
        default="crynet_int8_model",
        help="Name of the generated C array variable.",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=None,
        help="Path to write the .cc file (defaults to <tflite_name>.cc in same directory).",
    )
    parser.add_argument(
        "--cols",
        type=int,
        default=12,
        help="Number of bytes per line in the generated array.",
    )
    args = parser.parse_args()

    tflite_path = args.tflite_path
    if not tflite_path.exists():
        raise FileNotFoundError(f"TFLite file not found: {tflite_path}")

    output_path = args.output
    if output_path is None:
        output_path = tflite_path.with_suffix(".cc")

    data = tflite_path.read_bytes()
    out = format_as_c_array(data, args.var_name, cols=args.cols)
    output_path.write_text(out)
    print(f"Wrote C array to {output_path} (bytes={len(data)})")


if __name__ == "__main__":
    main()
