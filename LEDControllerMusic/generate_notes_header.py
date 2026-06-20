"""
生成 notes_data.h 头文件，供 LEDControllerMusic.ino 使用。
从 Notes.csv 读取 TIME / VALUE / LABEL 三列数据，
输出三个 PROGMEM 浮点数组:

  musicFlowTime[]    — FLOW 模式: 每个音符的起播时间 (s)
  musicFlowFreq[]    — FLOW 模式: 每个音符的频率 (Hz)
  musicBreathTime[]  — BREATH 模式: 仅时间点

用法:
  python3 generate_notes_header.py [Notes.csv] [notes_data.h]
"""

import csv
import sys

def float_str(s):
    """确保可以安全作为 C++ float literal。无小数点时补 '.0'，避免 '220f'。"""
    s = s.strip()
    if '.' not in s:
        s = s + '.0'
    return s + 'f'

def generate_header(csv_path="Notes.csv", header_path="notes_data.h"):
    rows = []
    with open(csv_path, "r") as f:
        reader = csv.DictReader(f)
        for row in reader:
            rows.append(row)

    n = len(rows)
    time_vals = [r["TIME"] for r in rows]
    freq_vals = [r["VALUE"] for r in rows]

    with open(header_path, "w") as f:
        f.write("// 自动生成自 {}\n".format(csv_path))
        f.write("// 总行数: {}\n".format(n))
        f.write("#include <avr/pgmspace.h>\n\n")
        f.write("#define MUSIC_NOTE_COUNT  {}\n\n".format(n))

        # FLOW 模式: TIME + FREQ
        f.write("// --- FLOW 模式: 时间点 + 频率 ---\n")
        for name, vals in [("musicFlowTime", time_vals),
                           ("musicFlowFreq", freq_vals)]:
            f.write("const PROGMEM float {}[MUSIC_NOTE_COUNT] = {{\n".format(name))
            for i in range(0, len(vals), 8):
                chunk = [float_str(vals[j]) for j in range(i, min(i + 8, len(vals)))]
                line = "    " + ", ".join(chunk)
                if i + 8 < len(vals):
                    line += ","
                f.write(line + "\n")
            f.write("};\n\n")

        # BREATH 模式: 仅 TIME
        f.write("// --- BREATH 模式: 仅时间点 ---\n")
        f.write("const PROGMEM float musicBreathTime[MUSIC_NOTE_COUNT] = {\n")
        for i in range(0, len(time_vals), 8):
            chunk = [float_str(time_vals[j]) for j in range(i, min(i + 8, len(time_vals)))]
            line = "    " + ", ".join(chunk)
            if i + 8 < len(time_vals):
                line += ","
            f.write(line + "\n")
        f.write("};\n")

    print("{} rows → {} written".format(n, header_path))

if __name__ == "__main__":
    input_csv = sys.argv[1] if len(sys.argv) > 1 else "Notes.csv"
    output_h  = sys.argv[2] if len(sys.argv) > 2 else "notes_data.h"
    generate_header(input_csv, output_h)