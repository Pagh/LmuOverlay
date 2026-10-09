"""Import personal bests from LMU's own results files into the overlay's records.

LMU writes every session to <LMU>/UserData/Log/Results/*.xml with each driver's laps and
sector times, so laps driven before the overlay existed (with TinyPedal, GoFast, ...) are
there too. All-time bests are shared by the cars of a class, so this keeps, per track +
class, the fastest complete lap and the fastest single sectors and merges them into
bin/records/<track> - class <class>.ini, together with the bests already in the per-car files
(<track> - <car>.ini, which keep fuel / energy use). Times are only ever lowered; the delta
trace is the fastest one recorded by the overlay.

Run it with the overlay closed, otherwise the overlay may write its in-memory records back.

  py tools/import_results.py            # dry run: shows what would change
  py tools/import_results.py --write    # updates bin/records
"""
import argparse
import glob
import os
import re
import sys
import xml.etree.ElementTree as ET

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_LMU = r"D:\SteamLibrary\steamapps\common\Le Mans Ultimate"
DEFAULT_RECORDS = os.path.join(HERE, "..", "bin", "records")
HEADER = "; All-time personal bests for every car of the class (valid laps only). Delete this file to reset.\n"


def player_name(lmu_dir):
    with open(os.path.join(lmu_dir, "UserData", "player", "Settings.JSON"), encoding="utf-8") as f:
        m = re.search(r'"Player Name"\s*:\s*"([^"]*)"', f.read())
    return m.group(1) if m else ""


def safe_file_name(s):
    # Same rule as SafeFileName() in Timing.cpp: characters Windows forbids become '_'.
    return "".join("_" if c in '<>:"/\\|?*' or ord(c) < 32 else c for c in s)


def num(v):
    try:
        x = float(v)
    except (TypeError, ValueError):
        return 0.0
    return x if x > 0 else 0.0


def take_best(dst, s, lap):
    """dst = [s1, s2, s3, lap]; per-sector minimum + best lap (0 = unknown)."""
    for i, v in enumerate(list(s) + [lap]):
        if v > 0 and (dst[i] <= 0 or v < dst[i]):
            dst[i] = v


def scan(results_dir, driver):
    """Returns ({(track, class): {'best': [s1, s2, s3, lap], 'laps': n, 'from': (file, car)}},
    {car: class}, files, unreadable)."""
    best, car_class = {}, {}
    files = sorted(glob.glob(os.path.join(results_dir, "*.xml")))
    bad = 0
    for path in files:
        try:
            root = ET.parse(path).getroot()
        except ET.ParseError:
            bad += 1
            continue
        rr = root.find("RaceResults")
        if rr is None:
            continue
        track = (rr.findtext("TrackCourse") or rr.findtext("TrackVenue") or "").strip()
        for session in rr:
            for d in session.findall("Driver"):
                car = (d.findtext("CarType") or "").strip()
                cls = (d.findtext("CarClass") or "").strip()
                if car and cls:
                    car_class[car] = cls
                if (d.findtext("Name") or "").strip() != driver or not track or not cls:
                    continue
                aids = d.findtext("ControlAndAids") or ""
                if "AI" in aids.replace("AutoBlip", ""):  # an AI drove part of it: not your laps
                    continue
                rec = best.setdefault((track, cls), {"best": [0.0] * 4, "laps": 0, "from": ("", "")})
                for lap in d.findall("Lap"):
                    s = [num(lap.get("s1")), num(lap.get("s2")), num(lap.get("s3"))]
                    t = num((lap.text or "").strip())
                    # LMU writes invalid / incomplete laps as "--.----": complete laps only.
                    if t <= 0 or not all(s):
                        continue
                    rec["laps"] += 1
                    if rec["best"][3] <= 0 or t < rec["best"][3]:
                        rec["from"] = (os.path.basename(path), car)
                    take_best(rec["best"], s, t)
    return {k: v for k, v in best.items() if v["laps"]}, car_class, len(files), bad


def read_ini(path):
    sections, cur = {}, None
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.rstrip("\n")
            m = re.match(r"\s*\[(.+)\]\s*$", line)
            if m:
                cur = sections.setdefault(m.group(1), {})
            elif cur is not None and "=" in line and not line.lstrip().startswith(";"):
                k, v = line.split("=", 1)
                cur[k.strip()] = v.strip()
    return sections


def best_of(sections):
    b = sections.get("best", {})
    return [num(b.get("s1")), num(b.get("s2")), num(b.get("s3")), num(b.get("lap"))]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--lmu", default=DEFAULT_LMU)
    ap.add_argument("--records", default=DEFAULT_RECORDS)
    ap.add_argument("--driver", help="driver name in the results (default: LMU's Player Name)")
    ap.add_argument("--write", action="store_true")
    a = ap.parse_args()

    driver = a.driver or player_name(a.lmu)
    found, car_class, nfiles, bad = scan(os.path.join(a.lmu, "UserData", "Log", "Results"), driver)
    print(f"driver '{driver}': {nfiles} results files ({bad} unreadable), {len(found)} track/class combinations\n")

    os.makedirs(a.records, exist_ok=True)
    # What the overlay already has: class files, and per-car files (bests from before classes).
    classes = {}  # (track, class) -> {'best': [...], 'trace': {...} or None, 'sources': [...]}
    for path in glob.glob(os.path.join(a.records, "*.ini")):
        if path.endswith(".map.ini"):
            continue
        sec = read_ini(path)
        info = sec.get("info", {})
        track = info.get("track", "")
        cls = info.get("class") or car_class.get(info.get("car", ""), "")
        if not track or not cls:
            if info.get("car"):
                print(f"  ! {os.path.basename(path)}: class of '{info['car']}' unknown, left alone")
            continue
        c = classes.setdefault((track, cls), {"best": [0.0] * 4, "trace": None, "old": None})
        if "class" in info:
            c["old"] = best_of(sec)
        b = best_of(sec)
        take_best(c["best"], b[:3], b[3])
        tr = sec.get("trace")
        if tr and tr.get("ms") and (c["trace"] is None or num(tr.get("lap")) < num(c["trace"].get("lap"))):
            c["trace"] = tr

    for key, rec in found.items():
        c = classes.setdefault(key, {"best": [0.0] * 4, "trace": None, "old": None})
        c["xml"] = rec

    changed = 0
    for (track, cls), c in sorted(classes.items()):
        merged = list(c["best"])
        rec = c.get("xml")
        if rec:
            take_best(merged, rec["best"][:3], rec["best"][3])
        old = c["old"] or [0.0] * 4
        tag = "  " if merged == old else "* "
        src = f"  ({rec['laps']} laps in LMU results, best: {rec['from'][1]}, {rec['from'][0]})" if rec else ""
        print(f"{tag}{track} - {cls}: lap {old[3]:.3f} -> {merged[3]:.3f}  sectors "
              f"{' / '.join(f'{x:.3f}' for x in merged[:3])}{src}")
        if merged == old:
            continue
        changed += 1
        if not a.write:
            continue
        path = os.path.join(a.records, safe_file_name(f"{track} - class {cls}") + ".ini")
        with open(path, "w", encoding="utf-8", newline="\n") as f:
            f.write(HEADER)
            f.write(f"\n[info]\ntrack={track}\nclass={cls}\n\n[best]\n")
            for k, v in zip(("s1", "s2", "s3", "lap"), merged):
                f.write(f"{k}={v:.3f}\n")
            if c["trace"]:
                f.write("\n[trace]\n")
                for k in ("bin", "length", "lap", "ms"):
                    f.write(f"{k}={c['trace'].get(k, '')}\n")
    print(f"\n{changed} class file(s) {'written' if a.write else 'would change (dry run, use --write)'}")


if __name__ == "__main__":
    sys.exit(main())
