#!/usr/bin/env python3
from __future__ import annotations

import argparse
import re
import struct
import subprocess
import sys
import threading
from dataclasses import dataclass
from pathlib import Path
from typing import Callable, Dict, Iterable

try:
    from PIL import Image, ImageDraw, ImageFont, ImageTk
except ImportError as exc:  # pragma: no cover
    raise SystemExit("Pillow is required. Install with: python -m pip install pillow") from exc

try:
    from fontTools.ttLib import TTFont
except ImportError:  # pragma: no cover
    TTFont = None

ARTICLE_MAGIC = b"CART"
FONT_MAGIC = b"CFNT"
ARTICLE_FORMAT_VERSION = 3
FONT_FORMAT_VERSION = 1
CELL_SIZE = 16
BITMAP_BYTES = 32
MAX_APPVAR_SIZE = 65512
UNTITLED_CHAPTER = "(untitled)"
DEFAULT_CONVBIN = r"C:\CEdev\bin\convbin.exe"
DEFAULT_WINDOWS_FONTS_DIR = Path(r"C:\Windows\Fonts")
DEFAULT_PREVIEW_TEXT = "CERE 中文界面预览"
FONT_SUFFIXES = {".ttf", ".ttc", ".otf", ".fon"}
DEFAULT_ARTICLE_FONT_APPVAR = "CARTFNT"
DEFAULT_BASE_FONT_APPVAR = "CEREFNT"
DEFAULT_FONT = "simsun.ttc"


@dataclass(frozen=True)
class FontEntry:
    display_name: str
    path: Path


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
        segment = text[last: match.start()]
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


def load_article_text_utf8(
    article_path: Path,
    logger: Callable[[str], None] | None = None,
) -> str:
    if article_path.suffix.lower() != ".txt":
        raise ValueError(f"文章文件必须是 .txt: {article_path}")

    raw = article_path.read_bytes()

    try:
        text = raw.decode("utf-8")
        return text
    except UnicodeDecodeError:
        pass

    # UTF-8 with BOM is still UTF-8, normalize it to plain UTF-8 for consistency.
    try:
        text_bom = raw.decode("utf-8-sig")
        article_path.write_text(text_bom, encoding="utf-8")
        if logger is not None:
            logger("检测到 UTF-8 BOM，已规范化为 UTF-8 无 BOM")
        return text_bom
    except UnicodeDecodeError:
        pass

    candidates: list[str] = []
    if raw.startswith(b"\xff\xfe") or raw.startswith(b"\xfe\xff"):
        candidates.append("utf-16")
    candidates.extend(["gb18030", "cp936", "big5"])

    for encoding in candidates:
        try:
            text = raw.decode(encoding)
        except UnicodeDecodeError:
            continue

        article_path.write_text(text, encoding="utf-8")
        if logger is not None:
            logger(f"文章编码不是 UTF-8，已从 {encoding} 自动转换为 UTF-8")
        return text

    raise ValueError(
        "文章文件不是 UTF-8，且自动转码失败。请保存为 UTF-8 .txt 后重试"
    )


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


def decode_name_record(raw: bytes, platform_id: int) -> str:
    if not raw:
        return ""
    if platform_id in (0, 3):
        for encoding in ("utf-16-be", "utf-16", "utf-8", "latin-1"):
            try:
                return raw.decode(encoding, errors="ignore").strip()
            except Exception:
                continue
        return ""
    for encoding in ("utf-8", "latin-1", "utf-16"):
        try:
            return raw.decode(encoding, errors="ignore").strip()
        except Exception:
            continue
    return ""


def get_font_display_name(font_path: Path) -> str:
    if TTFont is None:
        return font_path.stem

    try:
        with TTFont(str(font_path), fontNumber=0, lazy=True) as tt:
            names_table = tt.get("name")
            if names_table is None:
                return font_path.stem

            best_zh = ""
            best_any = ""
            for record in names_table.names:
                if record.nameID not in (1, 4, 16):
                    continue
                value = decode_name_record(record.string, record.platformID)
                if not value:
                    continue

                lang_id = getattr(record, "langID", 0)
                is_zh = (record.platformID == 3 and lang_id in (0x0804, 0x0404, 0x0C04, 0x1404))
                if is_zh and not best_zh:
                    best_zh = value
                if not best_any:
                    best_any = value

            if best_zh:
                return best_zh
            if best_any:
                return best_any
            return font_path.stem
    except Exception:
        return font_path.stem


def discover_windows_font_entries(fonts_dir: Path = DEFAULT_WINDOWS_FONTS_DIR) -> list[FontEntry]:
    if not fonts_dir.exists():
        return []

    entries: list[FontEntry] = []
    for path in sorted(fonts_dir.iterdir(), key=lambda p: p.name.lower()):
        if not path.is_file():
            continue
        if path.suffix.lower() not in FONT_SUFFIXES:
            continue

        display_name = get_font_display_name(path)
        entries.append(FontEntry(display_name=f"{display_name} ({path.name})", path=path))

    entries.sort(key=lambda item: item.display_name.lower())
    return entries


def extract_non_ascii_from_c_sources(src_dir: Path) -> list[int]:
    if not src_dir.exists():
        return []

    points: set[int] = set()
    for file_path in sorted(src_dir.rglob("*")):
        if file_path.suffix.lower() not in {".c", ".h"}:
            continue

        try:
            text = file_path.read_text(encoding="utf-8", errors="ignore")
        except Exception:
            continue

        for ch in text:
            cp = ord(ch)
            if cp >= 0x80:
                points.add(cp)

    return sorted(points)


def rasterize_glyphs(glyph_set: Iterable[int], font: ImageFont.FreeTypeFont, threshold: int) -> Dict[int, bytes]:
    glyphs: Dict[int, bytes] = {}
    for cp in glyph_set:
        glyphs[cp] = render_glyph_bitmap(cp, font, threshold)
    return glyphs


def ensure_common_args(
    font_path: Path,
    convbin: Path,
    font_size: int,
    threshold: int,
) -> None:
    if not font_path.exists():
        raise FileNotFoundError(f"Font file not found: {font_path}")
    if font_path.suffix.lower() not in FONT_SUFFIXES:
        allowed = ", ".join(sorted(FONT_SUFFIXES))
        raise ValueError(f"Font file must use one of: {allowed}; got: {font_path.suffix or '(none)'}")
    if not convbin.exists():
        raise FileNotFoundError(f"convbin not found: {convbin}")
    if font_size <= 0:
        raise ValueError("font-size must be > 0")
    if not (0 <= threshold <= 255):
        raise ValueError("threshold must be in range 0..255")


def write_font_outputs(
    out_dir: Path,
    convbin: Path,
    appvar_name: str,
    blob: bytes,
    archive: bool,
) -> tuple[Path, Path]:
    out_dir.mkdir(parents=True, exist_ok=True)
    font_bin = out_dir / f"{appvar_name}.bin"
    font_8xv = out_dir / f"{appvar_name}.8xv"
    font_bin.write_bytes(blob)
    run_convbin(convbin, font_bin, font_8xv, appvar_name, archive)
    return font_bin, font_8xv


def generate_base_font(
    *,
    font_path: Path,
    out_dir: Path,
    convbin: Path,
    base_font_name_raw: str,
    font_size: int,
    threshold: int,
    archive: bool,
    src_dir: Path,
    logger: Callable[[str], None] | None = None,
) -> Dict[str, object]:
    base_font_name = normalize_appvar_name(base_font_name_raw)
    ensure_common_args(font_path=font_path, convbin=convbin, font_size=font_size, threshold=threshold)

    if logger is not None:
        logger("收集基础字形，请等一下。")
    base_glyph_set = extract_non_ascii_from_c_sources(src_dir)

    if logger is not None:
        logger("栅格化基础字形，请等一下。")
    font = ImageFont.truetype(str(font_path), size=font_size)
    base_glyphs = rasterize_glyphs(base_glyph_set, font, threshold)
    base_blob = build_font_blob(base_glyphs)

    if logger is not None:
        logger("生成基础字形，请等一下。")
    _, base_8xv = write_font_outputs(
        out_dir=out_dir,
        convbin=convbin,
        appvar_name=base_font_name,
        blob=base_blob,
        archive=archive,
    )

    return {
        "base_font_8xv": base_8xv,
        "base_glyph_count": len(base_glyph_set),
        "base_payload_bytes": len(base_blob),
    }


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

    ensure_common_args(font_path=font_path, convbin=convbin, font_size=font_size, threshold=threshold)

    if logger is not None:
        logger("读取文章，请等一下。")
    raw_text = load_article_text_utf8(article_path, logger=logger)
    text, toc_entries = parse_text_and_toc(raw_text)
    article_bytes = text.encode("utf-8")

    if logger is not None:
        logger("收集文章字形，请等一下。")
    article_glyph_set = text_to_glyph_set(text)

    if logger is not None:
        logger("栅格化文章字形，请等一下。")
    font = ImageFont.truetype(str(font_path), size=font_size)
    article_glyphs = rasterize_glyphs(article_glyph_set, font, threshold)

    article_blob = build_article_blob(article_bytes, font_name, toc_entries)
    font_blob = build_font_blob(article_glyphs)

    out_dir.mkdir(parents=True, exist_ok=True)
    article_bin = out_dir / f"{article_name}.bin"
    article_8xv = out_dir / f"{article_name}.8xv"
    article_bin.write_bytes(article_blob)

    if logger is not None:
        logger("生成文章，请等一下。")
    run_convbin(convbin, article_bin, article_8xv, article_name, archive)

    if logger is not None:
        logger("生成文章字形，请等一下。")
    _, font_8xv = write_font_outputs(
        out_dir=out_dir,
        convbin=convbin,
        appvar_name=font_name,
        blob=font_blob,
        archive=archive,
    )

    result: Dict[str, object] = {
        "article_8xv": article_8xv,
        "font_8xv": font_8xv,
        "article_bytes": len(article_bytes),
        "toc_count": len(toc_entries),
        "glyph_count": len(article_glyph_set),
        "font_payload_bytes": len(font_blob),
    }

    return result


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Generate article/font AppVars for CERE")

    parser.add_argument("--article", help="文章 txt 文件")
    parser.add_argument("--font", help="字体文件")
    parser.add_argument("--out-dir", default="out", help="输出目录")
    parser.add_argument("--font-size", type=int, default=16, help="字体大小")
    parser.add_argument(
        "--threshold",
        type=int,
        default=96,
        help="二值阈值 (0-255)",
    )
    parser.add_argument("--convbin", default=DEFAULT_CONVBIN, help="convbin 路径")
    parser.add_argument("--article-name", default="CEREART", help="文章 AppVar 名")
    parser.add_argument("--font-name", default=DEFAULT_ARTICLE_FONT_APPVAR, help="文章字形 AppVar 名")
    parser.add_argument("--archive", action="store_true", help="输出为归档 AppVar")

    parser.add_argument("--gui", action="store_true", help="启动图形界面")
    parser.add_argument("--base-font-name", default=DEFAULT_BASE_FONT_APPVAR, help="基础字形 AppVar 名")
    parser.add_argument("--base-only", action="store_true", help="仅生成基础字形")
    parser.add_argument("--src-dir", default=str(Path(__file__).resolve().parents[1] / "src"), help="C 源码目录")

    return parser.parse_args()


def render_font_preview_image(font_path: Path, sample_text: str, *, width: int = 900, height: int = 300) -> Image.Image:
    canvas = Image.new("RGB", (width, height), "#FAFAF7")
    draw = ImageDraw.Draw(canvas)

    draw.rectangle((16, 16, width - 16, height - 16), outline="#90A4AE", width=2)

    try:
        title_font = ImageFont.truetype(str(font_path), size=24)
    except Exception:
        title_font = ImageFont.load_default()

    try:
        content_font = ImageFont.truetype(str(font_path), size=40)
    except Exception:
        content_font = ImageFont.load_default()

    draw.text((30, 28), "字体预览", font=title_font, fill="#263238")
    draw.text((30, 92), sample_text, font=content_font, fill="#111111")
    draw.text((30, 176), "此字形只会应用在中文", font=content_font, fill="#263238")

    return canvas


def run_gui() -> int:
    try:
        import tkinter as tk
        from tkinter import filedialog, messagebox, ttk
    except ImportError as exc:  # pragma: no cover
        raise SystemExit("Tkinter is not available in this Python environment") from exc

    root = tk.Tk()
    root.title("CERE 字形文章生成 - Copyright (c) 2026 ziyangbai all rights reserved - Licensed under LGPL v3.0 License")
    root.geometry("900x700")

    article_var = tk.StringVar(value="")
    font_var = tk.StringVar(value="")
    out_dir_var = tk.StringVar(value="out")
    article_name_var = tk.StringVar(value="CEREART")
    font_name_var = tk.StringVar(value=DEFAULT_ARTICLE_FONT_APPVAR)
    base_font_name_var = tk.StringVar(value=DEFAULT_BASE_FONT_APPVAR)
    font_size_var = tk.StringVar(value="16")
    threshold_var = tk.StringVar(value="96")
    convbin_var = tk.StringVar(value=DEFAULT_CONVBIN)
    archive_var = tk.BooleanVar(value=False)

    root.columnconfigure(1, weight=1)

    font_entries = discover_windows_font_entries()
    font_display_to_path = {entry.display_name: entry.path for entry in font_entries}

    def choose_article() -> None:
        selected = filedialog.askopenfilename(
            title="选择文章 txt 文件",
            filetypes=[("Text files", "*.txt"), ("All files", "*.*")],
        )
        if selected:
            article_var.set(selected)

    def choose_font_file() -> None:
        selected = filedialog.askopenfilename(
            title="选择字体文件",
            filetypes=[("Font files", "*.ttf *.ttc *.otf *.fon"), ("All files", "*.*")],
        )
        if selected:
            font_var.set(selected)
            update_preview(Path(selected))

    def choose_output_dir() -> None:
        selected = filedialog.askdirectory(title="选择输出目录")
        if selected:
            out_dir_var.set(selected)

    def choose_convbin() -> None:
        selected = filedialog.askopenfilename(title="选择 convbin.exe")
        if selected:
            convbin_var.set(selected)

    tk.Label(root, text="文章文件", anchor="w").grid(row=0, column=0, sticky="w", padx=8, pady=4)
    tk.Entry(root, textvariable=article_var).grid(row=0, column=1, sticky="ew", padx=8, pady=4)
    tk.Button(root, text="浏览", command=choose_article).grid(row=0, column=2, sticky="ew", padx=8, pady=4)

    tk.Label(root, text="字体", anchor="w").grid(row=1, column=0, sticky="w", padx=8, pady=4)
    selected_font_display = tk.StringVar(value="")
    font_combo = ttk.Combobox(root, textvariable=selected_font_display, state="readonly")
    font_combo["values"] = [entry.display_name for entry in font_entries]
    font_combo.grid(row=1, column=1, sticky="ew", padx=8, pady=4)

    def on_font_selected(_event: object) -> None:
        display = selected_font_display.get()
        font_path = font_display_to_path.get(display)
        if font_path is None:
            return
        font_var.set(str(font_path))
        update_preview(font_path)

    font_combo.bind("<<ComboboxSelected>>", on_font_selected)
    tk.Button(root, text="指定字体文件", command=choose_font_file).grid(row=1, column=2, sticky="ew", padx=8, pady=4)

    tk.Label(root, text="字体路径", anchor="w").grid(row=2, column=0, sticky="w", padx=8, pady=4)
    tk.Entry(root, textvariable=font_var, state="readonly").grid(row=2, column=1, sticky="ew", padx=8, pady=4)
    

    tk.Label(root, text="输出目录", anchor="w").grid(row=3, column=0, sticky="w", padx=8, pady=4)
    tk.Entry(root, textvariable=out_dir_var).grid(row=3, column=1, sticky="ew", padx=8, pady=4)
    tk.Button(root, text="浏览", command=choose_output_dir).grid(row=3, column=2, sticky="ew", padx=8, pady=4)

    tk.Label(root, text="文章 AppVar", anchor="w").grid(row=4, column=0, sticky="w", padx=8, pady=4)
    tk.Entry(root, textvariable=article_name_var).grid(row=4, column=1, sticky="ew", padx=8, pady=4)

    tk.Label(root, text="文章字形 AppVar", anchor="w").grid(row=5, column=0, sticky="w", padx=8, pady=4)
    tk.Entry(root, textvariable=font_name_var).grid(row=5, column=1, sticky="ew", padx=8, pady=4)

    tk.Label(root, text="基础字形 AppVar", anchor="w").grid(row=6, column=0, sticky="w", padx=8, pady=4)
    tk.Entry(root, textvariable=base_font_name_var).grid(row=6, column=1, sticky="ew", padx=8, pady=4)

    tk.Label(root, text="字体大小", anchor="w").grid(row=7, column=0, sticky="w", padx=8, pady=4)
    tk.Entry(root, textvariable=font_size_var).grid(row=7, column=1, sticky="ew", padx=8, pady=4)

    tk.Label(root, text="二值阈值（0-255）", anchor="w").grid(row=8, column=0, sticky="w", padx=8, pady=4)
    tk.Entry(root, textvariable=threshold_var).grid(row=8, column=1, sticky="ew", padx=8, pady=4)

    tk.Label(root, text="convbin.exe", anchor="w").grid(row=9, column=0, sticky="w", padx=8, pady=4)
    tk.Entry(root, textvariable=convbin_var).grid(row=9, column=1, sticky="ew", padx=8, pady=4)
    tk.Button(root, text="浏览", command=choose_convbin).grid(row=9, column=2, sticky="ew", padx=8, pady=4)

    tk.Checkbutton(root, text="Archive 的 AppVar", variable=archive_var).grid(
        row=10, column=1, sticky="w", padx=8, pady=4
    )

    preview_label = tk.Label(root, text="请在上方选择字体以查看预览", anchor="center")
    preview_label.grid(row=11, column=0, columnspan=3, sticky="nsew", padx=8, pady=8)
    root.rowconfigure(11, weight=1)

    log = tk.Text(root, height=10, wrap="word")
    log.grid(row=13, column=0, columnspan=3, sticky="nsew", padx=8, pady=8)
    root.rowconfigure(13, weight=1)

    button_frame = tk.Frame(root)
    button_frame.grid(row=12, column=0, columnspan=3, sticky="ew", padx=8, pady=6)
    button_frame.columnconfigure(0, weight=1)
    button_frame.columnconfigure(1, weight=1)

    generate_button = tk.Button(button_frame, text="生成文章 + 字形")
    base_only_button = tk.Button(button_frame, text="生成基础字形")
    generate_button.grid(row=0, column=0, sticky="ew", padx=4)
    base_only_button.grid(row=0, column=1, sticky="ew", padx=4)

    preview_cache: dict[str, ImageTk.PhotoImage] = {}

    def append_log(message: str) -> None:
        log.insert("end", f"{message}\n")
        log.see("end")

    def append_log_threadsafe(message: str) -> None:
        root.after(0, lambda: append_log(message))

    def update_preview(font_path: Path) -> None:
        try:
            image = render_font_preview_image(font_path, DEFAULT_PREVIEW_TEXT)
            photo = ImageTk.PhotoImage(image)
            preview_cache["photo"] = photo
            preview_label.config(image=photo, text="")
        except Exception as exc:
            preview_label.config(image="", text=f"预览失败：{exc}")

    if font_entries:
        default_font = font_entries[0]
        for entry in font_entries:
            if DEFAULT_FONT.lower() in entry.path.name.lower():
                default_font = entry
                break
        selected_font_display.set(default_font.display_name)
        font_var.set(str(default_font.path))
        update_preview(default_font.path)

    def parse_int(var: tk.StringVar, name: str) -> int:
        try:
            return int(var.get().strip())
        except ValueError as exc:
            raise ValueError(f"{name} 必须是整数") from exc

    def set_buttons_enabled(enabled: bool) -> None:
        state = "normal" if enabled else "disabled"
        generate_button.config(state=state)
        base_only_button.config(state=state)

    def run_generate() -> None:
        try:
            font_size = parse_int(font_size_var, "字体大小")
            threshold = parse_int(threshold_var, "二值阈值")
        except Exception as exc:
            messagebox.showerror("输入错误", str(exc))
            return

        def worker() -> None:
            try:
                append_log_threadsafe("生成中，请等一下。")
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

                append_log_threadsafe(f"文章 AppVar: {result['article_8xv']}")
                append_log_threadsafe(f"文章字形 AppVar: {result['font_8xv']}")
                append_log_threadsafe(f"文章 UTF-8 字节数: {result['article_bytes']}")
                append_log_threadsafe(f"章节条目数: {result['toc_count']}")
                append_log_threadsafe(f"文章字形数量: {result['glyph_count']}")
                append_log_threadsafe(f"文章字形负载字节: {result['font_payload_bytes']}")

                root.after(0, lambda: messagebox.showinfo("完成", "生成成功"))
            except Exception as exc:  # pragma: no cover
                append_log_threadsafe(f"错误: {exc}")
                root.after(0, lambda: messagebox.showerror("生成失败", str(exc)))
            finally:
                root.after(0, lambda: set_buttons_enabled(True))

        set_buttons_enabled(False)
        threading.Thread(target=worker, daemon=True).start()

    def run_generate_base_only() -> None:
        try:
            font_size = parse_int(font_size_var, "字体大小")
            threshold = parse_int(threshold_var, "二值阈值")
        except Exception as exc:
            messagebox.showerror("输入错误", str(exc))
            return

        def worker() -> None:
            try:
                append_log_threadsafe("生成中，请等一下。")
                result = generate_base_font(
                    font_path=Path(font_var.get().strip()),
                    out_dir=Path(out_dir_var.get().strip()),
                    convbin=Path(convbin_var.get().strip()),
                    base_font_name_raw=base_font_name_var.get().strip(),
                    font_size=font_size,
                    threshold=threshold,
                    archive=archive_var.get(),
                    src_dir=Path(__file__).resolve().parents[1] / "src",
                    logger=append_log_threadsafe,
                )

                append_log_threadsafe(f"基础字形 AppVar: {result['base_font_8xv']}")
                append_log_threadsafe(f"基础字形数量: {result['base_glyph_count']}")
                append_log_threadsafe(f"基础字形负载字节: {result['base_payload_bytes']}")
                root.after(0, lambda: messagebox.showinfo("完成", "基础字形生成成功"))
            except Exception as exc:  # pragma: no cover
                append_log_threadsafe(f"错误: {exc}")
                root.after(0, lambda: messagebox.showerror("生成失败", str(exc)))
            finally:
                root.after(0, lambda: set_buttons_enabled(True))

        set_buttons_enabled(False)
        threading.Thread(target=worker, daemon=True).start()

    generate_button.config(command=run_generate)
    base_only_button.config(command=run_generate_base_only)

    root.mainloop()
    return 0


def main() -> int:
    args = parse_args()

    cli_mode_requested = any(
        value
        for value in [
            args.article,
            args.font,
            args.base_only,
            args.archive,
            args.article_name != "CEREART",
            args.font_name != DEFAULT_ARTICLE_FONT_APPVAR,
            args.base_font_name != DEFAULT_BASE_FONT_APPVAR,
        ]
    )

    if args.gui or not cli_mode_requested:
        return run_gui()

    font_size = args.font_size
    threshold = args.threshold
    convbin = Path(args.convbin)
    font_path = Path(args.font) if args.font else Path("")
    out_dir = Path(args.out_dir)
    src_dir = Path(args.src_dir)

    if args.base_only:
        result = generate_base_font(
            font_path=font_path,
            out_dir=out_dir,
            convbin=convbin,
            base_font_name_raw=args.base_font_name,
            font_size=font_size,
            threshold=threshold,
            archive=args.archive,
            src_dir=src_dir,
        )

        print(f"Generated base font AppVar: {result['base_font_8xv']}")
        print(f"Base glyph count: {result['base_glyph_count']}")
        print(f"Base payload bytes: {result['base_payload_bytes']}")
        return 0

    if not args.article or not args.font:
        raise SystemExit("CLI mode requires --article and --font; or use --base-only with --font")

    result = generate_assets(
        article_path=Path(args.article),
        font_path=font_path,
        out_dir=out_dir,
        convbin=convbin,
        article_name_raw=args.article_name,
        font_name_raw=args.font_name,
        font_size=font_size,
        threshold=threshold,
        archive=args.archive,
    )

    print(f"Generated article AppVar: {result['article_8xv']}")
    print(f"Generated article font AppVar: {result['font_8xv']}")
    print(f"Article UTF-8 bytes: {result['article_bytes']}")
    print(f"Embedded chapter entries: {result['toc_count']}")
    print(f"Unique article non-ASCII glyphs: {result['glyph_count']}")
    print(f"Article font payload bytes: {result['font_payload_bytes']}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
