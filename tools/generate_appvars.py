#!/usr/bin/env python3
from __future__ import annotations

import argparse
import re
import struct
import subprocess
import sys
import threading
from pathlib import Path
from typing import Callable, Dict

try:
    from PIL import Image, ImageDraw, ImageFont
except ImportError as exc:  # pragma: no cover
    raise SystemExit(
        "Pillow is required. Install with: python -m pip install pillow"
    ) from exc

ARTICLE_MAGIC = b"CART"
FONT_MAGIC = b"CFNT"
ARTICLE_FORMAT_VERSION = 3
FONT_FORMAT_VERSION = 1
CELL_SIZE = 16
BITMAP_BYTES = 32
MAX_APPVAR_SIZE = 65512
UNTITLED_CHAPTER = "(untitled)"


def normalize_appvar_name(name: str) -> str:
    normalized = name.strip().upper()
    if not normalized:
        raise ValueError("AppVar name cannot be empty")
    if len(normalized) > 8:
        raise ValueError(f"AppVar name '{name}' exceeds 8 chars")
    if not re.fullmatch(r"[A-Z0-9]+", normalized):
        raise ValueError(
            f"AppVar name '{name}' contains invalid chars; only A-Z and 0-9 are allowed"
        )
    return normalized


def text_to_glyph_set(text: str) -> list[int]:
    cps = {ord(ch) for ch in text if ord(ch) >= 0x80}
    return sorted(cps)


def pixel_to_intensity(pixel: object) -> int:
    if isinstance(pixel, int):
        return pixel
    if isinstance(pixel, float):
        return int(pixel)
    if isinstance(pixel, tuple):
        if not pixel:
            return 0
        first = pixel[0]
        if isinstance(first, int):
            return first
        if isinstance(first, float):
            return int(first)
    return 0


def render_glyph_bitmap(codepoint: int, font: ImageFont.FreeTypeFont, threshold: int) -> bytes:
    image = Image.new("L", (CELL_SIZE, CELL_SIZE), 0)
    draw = ImageDraw.Draw(image)
    char = chr(codepoint)

    bbox = draw.textbbox((0, 0), char, font=font)
    if bbox is None:
        return bytes(BITMAP_BYTES)

    width = bbox[2] - bbox[0]
    height = bbox[3] - bbox[1]
    x = (CELL_SIZE - width) // 2 - bbox[0]
    y = (CELL_SIZE - height) // 2 - bbox[1]

    draw.text((x, y), char, font=font, fill=255)

    out = bytearray()
    for row in range(CELL_SIZE):
        bits = 0
        for col in range(CELL_SIZE):
            pixel = pixel_to_intensity(image.getpixel((col, row)))
            if pixel >= threshold:
                bits |= 1 << (15 - col)
        out.append((bits >> 8) & 0xFF)
        out.append(bits & 0xFF)

    return bytes(out)


def pack_name_field(name: str) -> bytes:
    encoded = name.encode("ascii")
    if len(encoded) > 8:
        raise ValueError(f"AppVar name '{name}' exceeds 8 chars")
    return encoded.ljust(8, b"\0")


def parse_text_and_toc(text: str) -> tuple[str, list[tuple[int, str]]]:
    pattern = re.compile(r"!#(.*?)#!|!##!", re.DOTALL)
    chunks: list[str] = []
    toc: list[tuple[int, str]] = []
    byte_offset = 0
    last = 0

    for match in pattern.finditer(text):
        segment = text[last : match.start()]
        if segment:
            chunks.append(segment)
            byte_offset += len(segment.encode("utf-8"))

        title_match = match.group(1)
        if title_match is None:
            title = UNTITLED_CHAPTER
        else:
            stripped = title_match.strip()
            title = stripped if stripped else UNTITLED_CHAPTER
        toc.append((byte_offset, title))
        last = match.end()

    tail = text[last:]
    if tail:
        chunks.append(tail)

    return "".join(chunks), toc


def build_article_blob(article_bytes: bytes, font_name: str, toc_entries: list[tuple[int, str]]) -> bytes:
    toc_body = bytearray()
    for chapter_offset, chapter_title in toc_entries:
        title_bytes = chapter_title.encode("utf-8")
        if len(title_bytes) > 48:
            title_bytes = title_bytes[:48]
            while title_bytes and (title_bytes[-1] & 0xC0) == 0x80:
                title_bytes = title_bytes[:-1]
        toc_body.extend(struct.pack("<I", chapter_offset))
        toc_body.append(len(title_bytes))
        toc_body.extend(title_bytes)

    toc_offset = 28 + len(article_bytes)
    header = (
        ARTICLE_MAGIC
        + bytes([ARTICLE_FORMAT_VERSION, 0, 0, 0])
        + struct.pack("<I", len(article_bytes))
        + pack_name_field(font_name)
        + struct.pack("<I", toc_offset)
        + struct.pack("<I", len(toc_entries))
    )
    blob = header + article_bytes + bytes(toc_body)
    if len(blob) > MAX_APPVAR_SIZE:
        raise ValueError(f"Article AppVar too large: {len(blob)} bytes (max {MAX_APPVAR_SIZE})")
    return blob


def build_font_blob(glyphs: Dict[int, bytes]) -> bytes:
    header = FONT_MAGIC + bytes([FONT_FORMAT_VERSION, 0, 0, 0]) + struct.pack("<I", len(glyphs))
    body = bytearray()
    for cp in sorted(glyphs.keys()):
        body.extend(struct.pack("<I", cp))
        body.extend(glyphs[cp])
    blob = header + bytes(body)
    if len(blob) > MAX_APPVAR_SIZE:
        raise ValueError(f"Font AppVar too large: {len(blob)} bytes (max {MAX_APPVAR_SIZE})")
    return blob


def run_convbin(convbin: Path, input_bin: Path, output_8xv: Path, name: str, archive: bool) -> None:
    cmd = [
        str(convbin),
        "-i",
        str(input_bin),
        "-o",
        str(output_8xv),
        "-k",
        "8xv",
        "-n",
        name,
    ]
    if archive:
        cmd.append("-r")
    subprocess.run(cmd, check=True)


def generate_assets(
    article_path: Path,
    font_path: Path,
    out_dir: Path,
    convbin: Path,
    article_name_raw: str,
    font_name_raw: str,
    font_size: int,
    threshold: int,
    archive: bool,
    logger: Callable[[str], None] | None = None,
) -> Dict[str, object]:
    article_name = normalize_appvar_name(article_name_raw)
    font_name = normalize_appvar_name(font_name_raw)

    if not article_path.exists():
        raise FileNotFoundError(f"Article file not found: {article_path}")
    if not font_path.exists():
        raise FileNotFoundError(f"Font file not found: {font_path}")
    if not convbin.exists():
        raise FileNotFoundError(f"convbin not found: {convbin}")
    if font_size <= 0:
        raise ValueError("font-size must be > 0")
    if threshold < 0 or threshold > 255:
        raise ValueError("threshold must be in range 0..255")

    if logger is not None:
        logger("Reading article text...")
    raw_text = article_path.read_text(encoding="utf-8")
    text, toc_entries = parse_text_and_toc(raw_text)
    article_bytes = text.encode("utf-8")

    if logger is not None:
        logger("Collecting required glyphs...")
    glyph_set = text_to_glyph_set(text)
    font = ImageFont.truetype(str(font_path), size=font_size)

    if logger is not None:
        logger("Rasterizing glyphs...")
    glyphs: Dict[int, bytes] = {}
    for cp in glyph_set:
        glyphs[cp] = render_glyph_bitmap(cp, font, threshold)

    article_blob = build_article_blob(article_bytes, font_name, toc_entries)
    font_blob = build_font_blob(glyphs)

    out_dir.mkdir(parents=True, exist_ok=True)

    article_bin = out_dir / f"{article_name}.bin"
    font_bin = out_dir / f"{font_name}.bin"
    article_8xv = out_dir / f"{article_name}.8xv"
    font_8xv = out_dir / f"{font_name}.8xv"

    article_bin.write_bytes(article_blob)
    font_bin.write_bytes(font_blob)

    if logger is not None:
        logger("Running convbin for article AppVar...")
    run_convbin(convbin, article_bin, article_8xv, article_name, archive)

    if logger is not None:
        logger("Running convbin for font AppVar...")
    run_convbin(convbin, font_bin, font_8xv, font_name, archive)

    return {
        "article_8xv": article_8xv,
        "font_8xv": font_8xv,
        "article_bytes": len(article_bytes),
        "toc_count": len(toc_entries),
        "glyph_count": len(glyph_set),
        "font_payload_bytes": len(font_blob),
    }


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Generate article+font AppVars for CERE from a UTF-8 article"
    )
    parser.add_argument("--article", help="Path to UTF-8 article text file")
    parser.add_argument("--font", help="Path to TTF/OTF font file")
    parser.add_argument("--out-dir", default="out", help="Output directory")
    parser.add_argument("--font-size", type=int, default=16, help="Font size used for rasterization")
    parser.add_argument(
        "--threshold",
        type=int,
        default=96,
        help="Pixel threshold (0-255) when converting grayscale glyph to monochrome",
    )
    parser.add_argument("--convbin", default=r"C:\CEdev\bin\convbin.exe", help="Path to convbin executable")
    parser.add_argument("--article-name", default="CEREART", help="AppVar name for article data")
    parser.add_argument("--font-name", default="CEREFNT", help="AppVar name for font data")
    parser.add_argument("--archive", action="store_true", help="Mark generated AppVars as archived")
    parser.add_argument("--gui", action="store_true", help="Open Tkinter GUI")
    return parser.parse_args()


def run_gui() -> int:
    try:
        import tkinter as tk
        from tkinter import filedialog, messagebox
    except ImportError as exc:  # pragma: no cover
        raise SystemExit("Tkinter is not available in this Python environment") from exc

    root = tk.Tk()
    root.title("CERE AppVar Generator")
    root.geometry("780x520")

    article_var = tk.StringVar(value="")
    font_var = tk.StringVar(value="")
    out_dir_var = tk.StringVar(value="out")
    article_name_var = tk.StringVar(value="CEREART")
    font_name_var = tk.StringVar(value="CEREFNT")
    font_size_var = tk.StringVar(value="16")
    threshold_var = tk.StringVar(value="96")
    convbin_var = tk.StringVar(value=r"C:\CEdev\bin\convbin.exe")
    archive_var = tk.BooleanVar(value=False)

    root.columnconfigure(1, weight=1)

    def add_row(row: int, label: str, var: tk.StringVar, browse: str | None = None) -> None:
        tk.Label(root, text=label, anchor="w").grid(row=row, column=0, sticky="w", padx=8, pady=4)
        entry = tk.Entry(root, textvariable=var)
        entry.grid(row=row, column=1, sticky="ew", padx=8, pady=4)

        if browse == "file":
            tk.Button(
                root,
                text="Browse",
                command=lambda: var.set(filedialog.askopenfilename() or var.get()),
            ).grid(row=row, column=2, sticky="ew", padx=8, pady=4)
        elif browse == "dir":
            tk.Button(
                root,
                text="Browse",
                command=lambda: var.set(filedialog.askdirectory() or var.get()),
            ).grid(row=row, column=2, sticky="ew", padx=8, pady=4)

    add_row(0, "Article txt", article_var, "file")
    add_row(1, "Font ttf/otf", font_var, "file")
    add_row(2, "Output dir", out_dir_var, "dir")
    add_row(3, "Article AppVar", article_name_var)
    add_row(4, "Font AppVar", font_name_var)
    add_row(5, "Font size", font_size_var)
    add_row(6, "Threshold (0-255)", threshold_var)
    add_row(7, "convbin.exe", convbin_var, "file")

    tk.Checkbutton(root, text="Archive generated AppVars", variable=archive_var).grid(
        row=8, column=1, sticky="w", padx=8, pady=4
    )

    log = tk.Text(root, height=14, wrap="word")
    log.grid(row=10, column=0, columnspan=3, sticky="nsew", padx=8, pady=8)
    root.rowconfigure(10, weight=1)

    generate_button = tk.Button(root, text="Generate")
    generate_button.grid(row=9, column=1, sticky="e", padx=8, pady=6)

    def append_log(message: str) -> None:
        log.insert("end", f"{message}\n")
        log.see("end")

    def append_log_threadsafe(message: str) -> None:
        root.after(0, lambda: append_log(message))

    def run_generate() -> None:
        try:
            font_size = int(font_size_var.get().strip())
            threshold = int(threshold_var.get().strip())
        except ValueError:
            messagebox.showerror("Invalid Input", "Font size and threshold must be integers")
            return

        def worker() -> None:
            try:
                append_log_threadsafe("Starting generation...")
                result = generate_assets(
                    article_path=Path(article_var.get().strip()),
                    font_path=Path(font_var.get().strip()),
                    out_dir=Path(out_dir_var.get().strip()),
                    convbin=Path(convbin_var.get().strip()),
                    article_name_raw=article_name_var.get().strip(),
                    font_name_raw=font_name_var.get().strip(),
                    font_size=font_size,
                    threshold=threshold,
                    archive=archive_var.get(),
                    logger=append_log_threadsafe,
                )
                append_log_threadsafe(f"Generated article AppVar: {result['article_8xv']}")
                append_log_threadsafe(f"Generated font AppVar: {result['font_8xv']}")
                append_log_threadsafe(f"Article UTF-8 bytes: {result['article_bytes']}")
                append_log_threadsafe(f"Embedded chapter entries: {result['toc_count']}")
                append_log_threadsafe(f"Unique CJK/non-ASCII glyphs: {result['glyph_count']}")
                append_log_threadsafe(f"Font payload bytes: {result['font_payload_bytes']}")
                root.after(0, lambda: messagebox.showinfo("Done", "Generation completed"))
            except Exception as exc:  # pragma: no cover
                append_log_threadsafe(f"Error: {exc}")
                root.after(0, lambda: messagebox.showerror("Generation Failed", str(exc)))
            finally:
                root.after(0, lambda: generate_button.config(state="normal"))

        generate_button.config(state="disabled")
        threading.Thread(target=worker, daemon=True).start()

    generate_button.config(command=run_generate)

    root.mainloop()
    return 0


def main() -> int:
    args = parse_args()

    if args.gui:
        return run_gui()

    if not args.article or not args.font:
        raise SystemExit("CLI mode requires --article and --font (or use --gui)")

    result = generate_assets(
        article_path=Path(args.article),
        font_path=Path(args.font),
        out_dir=Path(args.out_dir),
        convbin=Path(args.convbin),
        article_name_raw=args.article_name,
        font_name_raw=args.font_name,
        font_size=args.font_size,
        threshold=args.threshold,
        archive=args.archive,
    )

    print(f"Generated article AppVar: {result['article_8xv']}")
    print(f"Generated font AppVar: {result['font_8xv']}")
    print(f"Article UTF-8 bytes: {result['article_bytes']}")
    print(f"Embedded chapter entries: {result['toc_count']}")
    print(f"Unique CJK/non-ASCII glyphs: {result['glyph_count']}")
    print(f"Font payload bytes: {result['font_payload_bytes']}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
