"""Save an LMU event (practice / qualifying / race) with a readable report.

Copies LMU's results files (UserData/Log/Results/*.xml) and the matching replays
(UserData/Replays/*.Vcr) of the latest event into an archive folder, and writes report.html
there: results, your laps with sectors, consistency, penalties and track-limit warnings.

  py tools/session_report.py                       # latest event -> ../../LMU - Sessioni/<date> <track>
  py tools/session_report.py --out D:/Sessions     # somewhere else
  py tools/session_report.py --no-replays          # results + report only (replays are big)

An event = the latest results file plus the earlier ones on the same track within 6 hours.
To watch a saved replay again, copy its .Vcr back into UserData/Replays.
"""
import argparse
import glob
import html
import os
import re
import shutil
import statistics
import sys
import xml.etree.ElementTree as ET
from collections import Counter
from datetime import datetime

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_LMU = r"D:\SteamLibrary\steamapps\common\Le Mans Ultimate"
DEFAULT_OUT = os.path.normpath(os.path.join(HERE, "..", "..", "LMU - Sessioni"))
KIND = {"P": "Practice", "Q": "Qualifying", "W": "Warm-up", "R": "Race"}


def player_name(lmu_dir):
    with open(os.path.join(lmu_dir, "UserData", "player", "Settings.JSON"), encoding="utf-8") as f:
        m = re.search(r'"Player Name"\s*:\s*"([^"]*)"', f.read())
    return m.group(1) if m else ""


def num(v):
    try:
        x = float(v)
    except (TypeError, ValueError):
        return 0.0
    return x if x > 0 else 0.0


def fmt(t, dec=3):
    if not t or t <= 0:
        return "—"
    m, s = divmod(t, 60)
    return f"{int(m)}:{s:0{3 + dec}.{dec}f}" if m else f"{s:.{dec}f}"


def session_code(path):
    m = re.search(r"-\d+([PQWR])(\d+)\.xml$", os.path.basename(path))
    return (m.group(1), m.group(2)) if m else ("?", "")


def parse(path, driver):
    root = ET.parse(path).getroot()
    rr = root.find("RaceResults")
    kind, idx = session_code(path)
    sess = next((c for c in rr if c.find("Driver") is not None), None)
    info = {
        "path": path, "kind": kind, "code": f"{kind}{idx}", "name": KIND.get(kind, kind),
        "track": rr.findtext("TrackCourse") or rr.findtext("TrackVenue") or "",
        "event": rr.findtext("TrackEvent") or "", "length": num(rr.findtext("TrackLength")),
        "when": rr.findtext("TimeString") or "", "setting": rr.findtext("Setting") or "",
        "minutes": int(num(sess.findtext("Minutes"))) if sess is not None else 0,
        "drivers": [], "me": None, "penalties": [], "limits": [], "incidents": 0, "invalid": Counter(),
    }
    if sess is None:
        return info
    for d in sess.findall("Driver"):
        laps = []
        for lap in d.findall("Lap"):
            t = num((lap.text or "").strip())
            laps.append({
                "n": int(num(lap.get("num"))), "t": t, "p": int(num(lap.get("p"))),
                "s": [num(lap.get("s1")), num(lap.get("s2")), num(lap.get("s3"))],
                "pit": lap.get("pit") == "1", "top": num(lap.get("topspeed")),
                "tyre": (lap.get("fcompound") or "").split(",")[-1],
            })
        rec = {
            "name": d.findtext("Name") or "", "car": d.findtext("CarType") or "",
            "cls": d.findtext("CarClass") or "", "team": d.findtext("TeamName") or "",
            "num": d.findtext("CarNumber") or "", "pos": int(num(d.findtext("Position"))),
            "cpos": int(num(d.findtext("ClassPosition"))), "grid": int(num(d.findtext("ClassGridPos"))),
            "best": num(d.findtext("BestLapTime")), "laps": int(num(d.findtext("Laps"))),
            "stops": int(num(d.findtext("Pitstops"))), "status": d.findtext("FinishStatus") or "",
            "finish": num(d.findtext("FinishTime")), "lap_list": laps,
        }
        info["drivers"].append(rec)
        if rec["name"] == driver:
            info["me"] = rec
    stream = sess.find("Stream")
    if stream is not None:
        for e in stream:
            if e.get("Driver") != driver and driver not in (e.text or ""):
                continue
            if e.tag == "Penalty" and e.get("Driver") == driver:
                et = int(num(e.get("et")))
                info["penalties"].append(f"{e.get('Penalty')} — {e.get('Reason')} ({et // 60}:{et % 60:02d} into the session)")
            elif e.tag == "TrackLimits" and e.get("Driver") == driver and (e.text or "") != "No Further Action":
                text = (e.text or "").strip()
                if text.startswith("Invalid Lap"):
                    info["invalid"][text[len("Invalid Lap"):].strip().lower() or "invalid"] += 1
                else:  # warnings, points, penalties: worth listing one by one
                    info["limits"].append(f"lap {int(num(e.get('Lap'))) + 1}: {text} ({e.get('CurrentPoints')} pts)")
            elif e.tag == "Incident" and (e.text or "").startswith(driver):
                info["incidents"] += 1
    return info


def stats(me):
    valid = [l for l in me["lap_list"] if l["t"] > 0 and all(l["s"])]
    if not valid:
        return {}
    best = min(l["t"] for l in valid)
    clean = [l["t"] for l in valid if l["t"] < best * 1.03 and not l["pit"]]
    ideal = [min(l["s"][k] for l in valid) for k in range(3)]
    return {
        "best": best, "ideal": sum(ideal), "ideal_s": ideal,
        "avg": statistics.mean(clean) if clean else 0, "sd": statistics.pstdev(clean) if len(clean) > 2 else 0,
        "clean": len(clean), "valid": len(valid),
    }


CSS = """
:root{--bg:#f6f7f9;--card:#fff;--text:#16181d;--dim:#667085;--line:#e4e7ec;--me:#e8f0ff;--good:#0f9d58;--best:#7c3aed;--warn:#c2410c}
@media (prefers-color-scheme:dark){:root{--bg:#111318;--card:#1a1d24;--text:#eef1f6;--dim:#98a2b3;--line:#2a2f3a;--me:#22314d;--good:#3ddc84;--best:#b87cff;--warn:#ff8a65}}
body{background:var(--bg);color:var(--text);font:14px/1.45 system-ui,Segoe UI,sans-serif;margin:0;padding:24px 16px}
main{max-width:1000px;margin:auto}h1{font-size:24px;margin:0 0 4px}h2{font-size:18px;margin:28px 0 8px}
.sub{color:var(--dim);margin-bottom:16px}.card{background:var(--card);border:1px solid var(--line);border-radius:10px;padding:14px 16px;margin:10px 0;overflow-x:auto}
.kpis{display:flex;flex-wrap:wrap;gap:10px}.kpi{background:var(--card);border:1px solid var(--line);border-radius:10px;padding:10px 14px;min-width:120px}
.kpi b{display:block;font-size:20px}.kpi span{color:var(--dim);font-size:12px}
table{border-collapse:collapse;width:100%;font-variant-numeric:tabular-nums}th,td{padding:4px 8px;text-align:right;border-bottom:1px solid var(--line);white-space:nowrap}
th{color:var(--dim);font-weight:500;font-size:12px}td.l,th.l{text-align:left}tr.me td{background:var(--me);font-weight:600}
.best{color:var(--best);font-weight:700}.pb{color:var(--good)}.dim{color:var(--dim)}.warn{color:var(--warn)}
"""


def section(info, driver):
    me = info["me"]
    out = [f"<h2>{html.escape(info['name'])} <span class='dim'>({info['code']}, {info['minutes']} min)</span></h2>"]
    if not me:
        return out + ["<p class='dim'>You didn't drive in this session.</p>"]
    st = stats(me)
    cls = [d for d in info["drivers"] if d["cls"] == me["cls"]]
    field_best = min((d["best"] for d in cls if d["best"] > 0), default=0)
    k = [("Class position", f"P{me['cpos']}" + (f" <small class='dim'>from P{me['grid']}</small>" if info["kind"] == "R" else "")
          + f" <small class='dim'>/ {len(cls)}</small>"),
         ("Best lap", fmt(me["best"])), ("Gap to class best", f"+{me['best'] - field_best:.3f}" if me["best"] and field_best else "—"),
         ("Ideal lap", fmt(st.get("ideal"))), ("Laps", str(me["laps"]))]
    if st.get("clean", 0) > 2:
        k.append(("Clean-lap average", f"{fmt(st['avg'])} <small class='dim'>±{st['sd']:.2f}</small>"))
    if info["kind"] == "R":
        k.append(("Pit stops", str(me["stops"])))
    out.append("<div class='kpis'>" + "".join(f"<div class='kpi'><span>{a}</span><b>{b}</b></div>" for a, b in k) + "</div>")
    notes = [f"<li class='warn'>Penalty: {html.escape(p)}</li>" for p in info["penalties"]]
    if info["invalid"]:
        parts = ", ".join(f"{n} {html.escape(k)}" for k, n in info["invalid"].most_common())
        notes.append(f"<li>Laps invalidated by track limits: {parts}</li>")
    notes += [f"<li>Track limits {html.escape(t)}</li>" for t in info["limits"]]
    if info["incidents"]:
        notes.append(f"<li class='dim'>{info['incidents']} contacts reported by LMU</li>")
    if notes:
        out.append("<div class='card'><ul style='margin:0;padding-left:18px'>" + "".join(notes) + "</ul></div>")

    # Your laps.
    best_s = [min((l["s"][i] for l in me["lap_list"] if l["t"] > 0 and all(l["s"])), default=0) for i in range(3)]
    rows = []
    for l in me["lap_list"]:
        valid = l["t"] > 0
        cells = []
        for i in range(3):
            v = l["s"][i]
            c = "best" if valid and v and abs(v - best_s[i]) < 1e-4 else ""
            cells.append(f"<td class='{c}'>{fmt(v) if v else '—'}</td>")
        lt = "best" if valid and abs(l["t"] - me["best"]) < 1e-4 else ""
        tag = " <span class='dim'>pit</span>" if l["pit"] else ("" if valid else " <span class='warn'>invalid</span>")
        pos = f"<td>P{l['p']}</td>" if info["kind"] == "R" else ""
        rows.append(f"<tr><td>{l['n']}</td>{pos}<td class='{lt}'>{fmt(l['t']) if valid else '—'}{tag}</td>{''.join(cells)}"
                    f"<td class='dim'>{l['top']:.0f}</td><td class='l dim'>{html.escape(l['tyre'])}</td></tr>")
    pos_h = "<th>Pos</th>" if info["kind"] == "R" else ""
    out.append("<div class='card'><table><tr><th>Lap</th>" + pos_h +
               "<th>Time</th><th>S1</th><th>S2</th><th>S3</th><th>km/h</th><th class='l'>Tyre</th></tr>" + "".join(rows) + "</table></div>")

    # Class results.
    cls.sort(key=lambda d: (d["cpos"] or 999))
    leader = cls[0] if cls else None
    res = []
    for d in cls:
        if info["kind"] == "R":
            if d is leader:
                gap = fmt(d["finish"]) if d["finish"] else "—"
            elif leader and d["laps"] < leader["laps"]:
                gap = f"+{leader['laps'] - d['laps']} L"
            elif d["finish"] and leader and leader["finish"]:
                gap = f"+{d['finish'] - leader['finish']:.3f}"
            else:
                gap = d["status"]
        else:
            gap = f"+{d['best'] - field_best:.3f}" if d["best"] and d is not leader else ""
        bc = "best" if d["best"] and abs(d["best"] - field_best) < 1e-4 else ""
        res.append(f"<tr class='{'me' if d is me else ''}'><td>{d['cpos']}</td><td class='l'>{html.escape(d['name'])}</td>"
                   f"<td class='l dim'>{html.escape(d['car'])}</td><td class='{bc}'>{fmt(d['best'])}</td><td>{d['laps']}</td>"
                   + (f"<td>{d['stops']}</td>" if info["kind"] == "R" else "") + f"<td>{html.escape(gap)}</td></tr>")
    stops_h = "<th>Stops</th>" if info["kind"] == "R" else ""
    out.append(f"<div class='card'><table><tr><th>Pos</th><th class='l'>Driver</th><th class='l'>Car</th><th>Best</th><th>Laps</th>{stops_h}"
               f"<th>{'Gap' if info['kind'] == 'R' else 'To best'}</th></tr>" + "".join(res) + "</table></div>")
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--lmu", default=DEFAULT_LMU)
    ap.add_argument("--out", default=DEFAULT_OUT)
    ap.add_argument("--driver")
    ap.add_argument("--no-replays", action="store_true")
    a = ap.parse_args()

    driver = a.driver or player_name(a.lmu)
    results = sorted(glob.glob(os.path.join(a.lmu, "UserData", "Log", "Results", "*.xml")), key=os.path.getmtime)
    if not results:
        sys.exit("no results files")
    last = parse(results[-1], driver)
    event = [last]
    for p in reversed(results[:-1]):
        if os.path.getmtime(results[-1]) - os.path.getmtime(p) > 6 * 3600:
            break
        try:
            s = parse(p, driver)
        except ET.ParseError:
            continue
        if s["track"] != last["track"]:
            break
        event.insert(0, s)
    me_car = next((s["me"]["car"] for s in reversed(event) if s["me"]), "")
    day = datetime.fromtimestamp(os.path.getmtime(results[-1])).strftime("%Y-%m-%d")
    folder = os.path.join(a.out, re.sub(r'[<>:"/\\|?*]', "_", f"{day} {last['track']} - {me_car}".strip(" -")))
    os.makedirs(folder, exist_ok=True)

    copied = []
    for s in event:
        shutil.copy2(s["path"], folder)
        copied.append(os.path.basename(s["path"]))
        if a.no_replays:
            continue
        t = os.path.getmtime(s["path"])
        for v in glob.glob(os.path.join(a.lmu, "UserData", "Replays", "*.Vcr")):
            name = os.path.basename(v)
            if f" {s['code']} " in f" {name} " or re.search(rf"\b{s['code']}\b", name):
                if -120 < os.path.getmtime(v) - t < 300 and not os.path.exists(os.path.join(folder, name)):
                    shutil.copy2(v, folder)
                    copied.append(name)

    copied = sorted(n for n in os.listdir(folder) if n != "report.html")
    title = f"{last['track']} · {me_car}"
    body = [f"<h1>{html.escape(title)}</h1>",
            f"<div class='sub'>{html.escape(last['event'])} · {html.escape(last['when'])} · {html.escape(last['setting'])} · "
            f"{html.escape(driver)}</div>"]
    for s in event:
        body += section(s, driver)
    body.append(f"<p class='dim' style='margin-top:28px'>Files saved with this report: {html.escape(', '.join(copied))}. "
                "To watch a replay again, copy its .Vcr back into LMU's UserData\\Replays.</p>")
    page = (f"<!doctype html><html lang='en'><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
            f"<title>{html.escape(title)}</title><style>{CSS}</style></head><body><main>{''.join(body)}</main></body></html>")
    with open(os.path.join(folder, "report.html"), "w", encoding="utf-8") as f:
        f.write(page)
    print(folder)
    for c in copied:
        print("  ", c)


if __name__ == "__main__":
    main()
