"""
Dynamic Banners-N-Flags: game update checker / signature repair tool.

After an ATS update, run:   tools\\update_check.bat            (check only)
                            tools\\update_check.bat --write    (also repair the installed ini)

For every signature in the ini it checks that the pattern matches exactly once in amtrucks.exe and that the
values read from it are plausible. If a signature no longer matches, it looks for the most similar code in the
new executable (usually the game was recompiled and a few bytes moved), proposes a repaired signature and, with
--write, stores it in the ini (a backup is kept). The plugin reads its signatures from the ini, so repairs need
no recompiling.

Only the Python standard library is used.
"""
import argparse
import configparser
import datetime
import json
import os
import re
import shutil
import struct
import sys

ATS_EXE_IN_LIBRARY = os.path.join('steamapps', 'common', 'American Truck Simulator', 'bin', 'win_x64', 'amtrucks.exe')


def find_ats_exe():
    """Locate amtrucks.exe through Steam's registry entry and library list. Returns None if not found."""
    roots = []
    try:
        import winreg
        for hive, key in ((winreg.HKEY_CURRENT_USER, r'Software\Valve\Steam'),
                          (winreg.HKEY_LOCAL_MACHINE, r'SOFTWARE\WOW6432Node\Valve\Steam')):
            try:
                with winreg.OpenKey(hive, key) as k:
                    for value in ('SteamPath', 'InstallPath'):
                        try:
                            roots.append(winreg.QueryValueEx(k, value)[0])
                        except OSError:
                            pass
            except OSError:
                pass
    except ImportError:
        pass
    roots.append(r'C:\Program Files (x86)\Steam')
    libraries = []
    for root in roots:
        libraries.append(root)
        vdf = os.path.join(root, 'steamapps', 'libraryfolders.vdf')
        if os.path.exists(vdf):
            libraries += [p.replace('\\\\', '\\') for p in
                          re.findall(r'"path"\s+"([^"]+)"', open(vdf, encoding='utf-8', errors='replace').read())]
    for lib in libraries:
        exe = os.path.join(lib, ATS_EXE_IN_LIBRARY)
        if os.path.exists(exe):
            return os.path.normpath(exe)
    return None

# Operand positions inside each signature; they must match src/game_layout.cpp.
# kind: 'rip' = rip-relative disp32 to a global (instruction length 7), 'off' = struct offset, 'fn' = function start
SIGNATURES = {
    'PlayerChain': [('game_global', 3, 4, 'rip'), ('actor', 13, 4, 'off'), ('truck', 43, 1, 'off'),
                    ('trailer', 55, 4, 'off')],
    'Records': [('records_data', 3, 4, 'off'), ('records_count', 10, 4, 'off')],
    'Patches': [('patches_data', 3, 4, 'off'), ('patches_count', 10, 4, 'off')],
    'NextTrailer': [('next_trailer', 3, 4, 'off')],
    'Merged': [('merged', 3, 4, 'off')],
    'TrailerConnected': [('trailer_connected', 3, 4, 'off')],
    'PatchDraw': [('patch_draw_function', 0, 0, 'fn')],
    'ModelHookups': [('model_hookups', 3, 4, 'off')],
    'HookupClass': [('get_class_vt_slot', 13, 1, 'off')],
}
REQUIRED = {'PlayerChain', 'Records', 'Patches', 'NextTrailer', 'Merged'}
OPTIONAL_NOTE = {
    'TrailerConnected': 'without it, trailers are not toggled',
    'PatchDraw': 'without it, flag cloth uses the fallback (it shows while the game is paused)',
    'ModelHookups': 'without it, beacon units are not toggled',
    'HookupClass': 'without it, beacon units are not toggled',
    'Reflection': 'without it, beacon units are not toggled',
}
# Values found for ATS 1.61, shown for comparison only.
KNOWN_161 = {'actor': 0x31B0, 'truck': 0x18, 'trailer': 0xC8, 'records_data': 0x770, 'records_count': 0x778,
             'patches_data': 0x7C8, 'patches_count': 0x7D0, 'next_trailer': 0x1060, 'merged': 0x1020,
             'trailer_connected': 0xFB8, 'model_hookups': 0x318, 'get_class_vt_slot': 0x28,
             'light_type': 0x230, 'beacon': 0x200}


def check_reflection(exe):
    """The beacon detection's name-based lookups, done the same way as src/reflection.cpp but on the file.
    Returns (values, problems)."""
    d = exe.data
    pe = struct.unpack_from('<I', d, 0x3C)[0]
    base = struct.unpack_from('<Q', d, pe + 24 + 24)[0]
    secs = {name: (va, vsz, ro, rsz) for name, va, vsz, ro, rsz in exe.sections}

    def rva2off(rva):
        for va, vsz, ro, rsz in secs.values():
            if va <= rva < va + rsz:
                return ro + rva - va
        return None

    def q(rva):
        o = rva2off(rva)
        return struct.unpack_from('<Q', d, o)[0] if o is not None else 0

    def string_at(va_abs):
        o = rva2off(va_abs - base) if base <= va_abs < base + exe.size_of_image else None
        return d[o:o + 64].split(b'\0')[0].decode(errors='replace') if o is not None else None

    def strings(s, sec='.rdata'):
        va, vsz, ro, rsz = secs[sec]
        blob = d[ro:ro + rsz]
        return [va + m.start() + 1 for m in re.finditer(b'\0' + re.escape(s.encode()) + b'\0', blob)]

    def pointers_to(value_rva, sec):
        va, vsz, ro, rsz = secs[sec]
        needle = struct.pack('<Q', base + value_rva)
        return [va + m.start() for m in re.finditer(re.escape(needle), d[ro:ro + rsz]) if m.start() % 8 == 0]

    values, problems = {}, []
    desc = None
    for s in strings('flare_vehicle'):
        for meta in pointers_to(s, '.data'):
            for cand in pointers_to(meta, '.rdata'):
                if base <= q(cand + 0x20) < base + exe.size_of_image:
                    desc = cand
                    break
            if desc: break
        if desc: break
    if not desc:
        return values, ["class 'flare_vehicle' not found"]
    parent = q(desc + 0x18)
    parent_name = string_at(q(q(parent - base) - base)) if parent else None
    if parent_name != 'light_source':
        problems.append(f"class layout changed (flare_vehicle's parent is {parent_name!r})")
    rec = q(desc + 0x20) - base
    for _ in range(256):
        if q(rec) != base + desc:
            break
        if string_at(q(rec + 32)) == 'light_type':
            values['light_type'] = q(rec + 8)
        rec += 40
    if 'light_type' not in values:
        problems.append('attribute flare_vehicle.light_type not found')
    for s in strings('beacon'):
        for p in pointers_to(s, '.data'):
            if string_at(q(p - 16)) == 'aux' and string_at(q(p + 16)) == 'brake':
                values['beacon'] = q(p - 8)
    if 'beacon' not in values:
        problems.append("light type 'beacon' not found in the enum table")
    return values, problems


# ------------------------------------------------------------------------------------------------ PE
class Exe:
    def __init__(self, path):
        self.path = path
        self.data = open(path, 'rb').read()
        d = self.data
        pe = struct.unpack_from('<I', d, 0x3C)[0]
        nsec = struct.unpack_from('<H', d, pe + 6)[0]
        optsz = struct.unpack_from('<H', d, pe + 20)[0]
        self.size_of_image = struct.unpack_from('<I', d, pe + 24 + 56)[0]
        # Same format as GameBuildId() in include/game_build.h
        self.build_id = f'{struct.unpack_from("<I", d, pe + 8)[0]:08X}-{self.size_of_image:X}'
        self.sections = []
        for i in range(nsec):
            o = pe + 24 + optsz + i * 40
            name = d[o:o + 8].rstrip(b'\0').decode(errors='replace')
            vsz, va, rsz, ro = struct.unpack_from('<IIII', d, o + 8)
            self.sections.append((name, va, vsz, ro, rsz))
        text = [s for s in self.sections if s[0] == '.text'][0]
        self.text_va, self.text = text[1], d[text[3]:text[3] + text[4]]

    def section_of(self, rva):
        for name, va, vsz, ro, rsz in self.sections:
            if va <= rva < va + max(vsz, rsz):
                return name
        return None


# ------------------------------------------------------------------------------------------------ patterns
def parse(sig):
    return [None if t in ('?', '??') else int(t, 16) for t in sig.split()]


def fmt(pat):
    return ' '.join('?' if b is None else f'{b:02X}' for b in pat)


def to_regex(pat):
    return re.compile(b''.join(b'.' if b is None else re.escape(bytes([b])) for b in pat), re.S)


def find_all(text, pat, limit=3):
    return [m.start() for _, m in zip(range(limit), to_regex(pat).finditer(text))]


def read_operand(exe, off, pos, size):
    b = exe.text[off + pos:off + pos + size]
    if len(b) < size:
        return None
    return b[0] if size == 1 else struct.unpack('<i' if size == 4 else '<B', b)[0]


def extract(exe, name, off):
    """Returns ({value_name: value}, [problems]) for a match at .text offset `off`."""
    values, problems = {}, []
    for vname, pos, size, kind in SIGNATURES[name]:
        if kind == 'fn':
            values[vname] = exe.text_va + off
            continue
        v = read_operand(exe, off, pos, size)
        if v is None:
            problems.append(f'{vname}: out of range')
            continue
        if kind == 'rip':
            target = exe.text_va + off + 7 + v
            values[vname] = target
            if exe.section_of(target) not in ('.data', '.rdata'):
                problems.append(f'{vname}: rip target 0x{target:X} is not in a data section')
        else:
            v &= 0xFFFFFFFF
            values[vname] = v
            if v == 0 or v > 0x10000 or v % 8:
                problems.append(f'{vname} = 0x{v:X} is implausible')
    if name in ('Records', 'Patches'):
        a, b = list(values.values())[:2]
        if b != a + 8:
            problems.append('data/count offsets are not adjacent')
    return values, problems


def operand_positions(name):
    s = set()
    for _, pos, size, kind in SIGNATURES[name]:
        s.update(range(pos, pos + size))
    return s


def fuzzy_find(exe, name, pat, known):
    """Find the most similar code for a signature that no longer matches.

    Candidates come from distinctive byte runs of the old signature and from places where the last known
    operand values (struct offsets rarely change between updates) appear at the right position.
    Returns a list of (score, offset, values, problems), best first."""
    fixed = [i for i, b in enumerate(pat) if b is not None]
    if not fixed:
        return []
    candidates = set()
    # 1. runs of >= 4 fixed bytes
    run = []
    for i, b in enumerate(pat + [None]):
        if b is not None:
            run.append(i)
            continue
        if len(run) >= 4:
            chunk = bytes(pat[run[0]:run[-1] + 1])
            for k in range(0, len(chunk) - 3, 2):
                sub, pos, hits = chunk[k:k + 4], -1, 0
                while hits < 4000:
                    pos = exe.text.find(sub, pos + 1)
                    if pos < 0:
                        break
                    hits += 1
                    c = pos - (run[0] + k)
                    if 0 <= c <= len(exe.text) - len(pat):
                        candidates.add(c)
        run = []
    # 2. last known operand values
    for vname, opos, size, kind in SIGNATURES[name]:
        if kind != 'off' or vname not in known or size != 4:
            continue
        needle, pos, hits = struct.pack('<I', known[vname]), -1, 0
        while hits < 20000:
            pos = exe.text.find(needle, pos + 1)
            if pos < 0:
                break
            hits += 1
            c = pos - opos
            if 0 <= c <= len(exe.text) - len(pat):
                candidates.add(c)
    results = []
    for c in candidates:
        similarity = sum(1 for i in fixed if exe.text[c + i] == pat[i]) / len(fixed)
        if similarity < 0.5:
            continue
        values, problems = extract(exe, name, c)
        same_as_known = all(values.get(k) == v for k, v in known.items() if k in values and k != 'game_global'
                            and k != 'patch_draw_function')
        score = similarity + (0.10 if same_as_known and not problems else 0.0)
        results.append((score, c, values, problems))
    results.sort(key=lambda r: -r[0])
    return results


def repair(exe, name, pat, off):
    """Build a unique signature at `off`: keep matching bytes, take the new bytes where the old ones differ."""
    ops = operand_positions(name)
    new = []
    for i, b in enumerate(pat):
        actual = exe.text[off + i]
        new.append(None if (b is None or i in ops) else actual)
    for extra in range(0, 33):
        if extra:
            new.append(exe.text[off + len(pat) + extra - 1])
        if len(find_all(exe.text, new)) == 1:
            return new
    return None


# ------------------------------------------------------------------------------------------------ built-ins
def builtin_signatures(dll_paths, source_path):
    """Signatures a plugin build uses when the ini has none: read from the DLL's DBSIG: markers,
    else from src/config_manager.cpp. Returns (dict, where_from)."""
    for dll in dll_paths:
        if dll and os.path.exists(dll):
            data = open(dll, 'rb').read()
            sigs = {m.group(1).decode(): m.group(2).decode()
                    for m in re.finditer(rb'DBSIG:(\w+)=([0-9A-Fa-f? ]+)\x00', data)}
            if sigs:
                return sigs, dll
    if os.path.exists(source_path):
        src = open(source_path, encoding='utf-8').read()
        joined = re.sub(r'"\s*\n\s*"', '', src)   # join C string literal continuations
        sigs = dict(re.findall(r'"DBSIG:(\w+)=([0-9A-Fa-f? ]+)"', joined))
        if sigs:
            return sigs, source_path
    return {}, None


def write_overrides(ini_path, build_id, sigs):
    """Replace the ini's [Signatures] section with a GameBuild-stamped one containing only `sigs`.
    User settings and comments elsewhere are kept. Returns the backup path (or None for a new file)."""
    backup = None
    lines = []
    if os.path.exists(ini_path):
        backup = ini_path + f'.bak_{datetime.datetime.now():%Y%m%d_%H%M%S}'
        shutil.copy2(ini_path, backup)
        lines = open(ini_path, encoding='utf-8').read().splitlines()
    out, skipping = [], False
    for line in lines:
        header = re.match(r'^\s*\[(\w+)\]', line)
        if header:
            skipping = header.group(1) == 'Signatures'
        if not skipping:
            out.append(line)
    while out and not out[-1].strip():
        out.pop()
    out += ['', '[Signatures]',
            '; Written by tools\\update_check.py. Only used while this exact game build is running.',
            f'GameBuild = {build_id}']
    out += [f'{name} = {sig}' for name, sig in sigs.items()]
    open(ini_path, 'w', encoding='utf-8').write('\n'.join(out) + '\n')
    return backup


# ------------------------------------------------------------------------------------------------ main
def main():
    here = os.path.dirname(os.path.abspath(__file__))
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--exe', default=None, help='path to amtrucks.exe (default: found through Steam)')
    ap.add_argument('--print-exe', action='store_true', help='only print the detected amtrucks.exe path')
    ap.add_argument('--ini', default=None, help='dynamic_banners.ini (default: the installed one)')
    ap.add_argument('--dll', default=None, help='dynamic_banners.dll to read built-in signatures from '
                                                '(default: the installed one)')
    ap.add_argument('--write', action='store_true', help='store repaired signatures in the ini (keeps a backup)')
    args = ap.parse_args()
    args.exe = args.exe or find_ats_exe()
    if args.print_exe:
        print(args.exe or '')
        return 0 if args.exe else 2
    if not args.exe or not os.path.exists(args.exe):
        print('Could not find amtrucks.exe through Steam. Pass it with --exe "<path>\\amtrucks.exe".')
        return 2

    plugins = os.path.join(os.path.dirname(args.exe), 'plugins')
    ini_path = args.ini or os.path.join(plugins, 'dynamic_banners.ini')
    exe = Exe(args.exe)
    builtins, builtins_from = builtin_signatures(
        [args.dll, os.path.join(plugins, 'dynamic_banners.dll'), os.path.join(here, '..', 'bin', 'dynamic_banners.dll')],
        os.path.join(here, '..', 'src', 'config_manager.cpp'))
    if not builtins:
        print('Could not find dynamic_banners.dll (or the source) to read the built-in signatures from. Use --dll.')
        return 2

    cfg = configparser.ConfigParser(inline_comment_prefixes=(';',))
    cfg.optionxform = str
    if os.path.exists(ini_path):
        cfg.read(ini_path, encoding='utf-8')
    ini_build = cfg.get('Signatures', 'GameBuild', fallback='')
    overrides = {k: v for k, v in (cfg.items('Signatures') if cfg.has_section('Signatures') else [])
                 if k in SIGNATURES and v.strip()}
    use_overrides = bool(ini_build) and ini_build == exe.build_id

    print(f'Executable : {args.exe}')
    print(f'             build {exe.build_id}, modified '
          f'{datetime.datetime.fromtimestamp(os.path.getmtime(args.exe)):%Y-%m-%d %H:%M}')
    print(f'Built-ins  : {os.path.abspath(builtins_from)}')
    print(f'Config     : {os.path.abspath(ini_path)}' + ('' if os.path.exists(ini_path) else ' (does not exist)'))
    if overrides and not use_overrides:
        print(f'             has {len(overrides)} override(s) for game build {ini_build or "?"} - ignored for this build')
    elif overrides:
        print(f'             has {len(overrides)} override(s) for this build')
    print()

    known_path = os.path.join(here, 'known_good.json')
    known = {}
    if os.path.exists(known_path):
        known = json.load(open(known_path))
    else:   # first run: values found for ATS 1.61
        for name, ops in SIGNATURES.items():
            known[name] = {v: KNOWN_161[v] for v, _, _, k in ops if v in KNOWN_161}
    new_known = {}
    effective_overrides = dict(overrides) if use_overrides else {}
    repairs, broken = {}, []
    for name in SIGNATURES:
        source = 'ini' if name in effective_overrides else 'built-in'
        sig = effective_overrides.get(name) or builtins.get(name)
        if not sig:
            print(f'[MISSING   ] {name}: no built-in signature in this plugin build')
            broken.append(name)
            continue
        pat = parse(sig)
        hits = find_all(exe.text, pat)
        if len(hits) == 1:
            values, problems = extract(exe, name, hits[0])
            status = 'OK' if not problems else 'SUSPICIOUS'
            print(f'[{status:10}] {name:17} ({source}) ' + ', '.join(
                f'{k}=0x{v:X}' + ('' if KNOWN_161.get(k, v) == v else f' (1.61: 0x{KNOWN_161[k]:X})')
                for k, v in values.items() if k not in ('game_global', 'patch_draw_function')))
            for p in problems:
                print(f'             ! {p}')
            if problems:
                broken.append(name)
            else:
                new_known[name] = values
            continue

        why = 'no longer matches' if not hits else 'matches more than once'
        print(f'[BROKEN    ] {name:17} ({source}) {why} - searching for the new location...')
        results = fuzzy_find(exe, name, pat, known.get(name, {}))
        good = [r for r in results if not r[3]]
        chosen = None
        if good and good[0][0] >= 0.75:
            best = good[0]
            ties = [r for r in good if best[0] - r[0] < 0.05]
            same = all({k: v for k, v in r[2].items() if k != 'patch_draw_function'} ==
                       {k: v for k, v in best[2].items() if k != 'patch_draw_function'} for r in ties)
            print(f'             best candidate exe+0x{exe.text_va + best[1]:X}: match score {best[0]:.2f} '
                  f'(1.00 = identical bytes, +0.10 when the offsets are unchanged), '
                  f'{len(ties)} candidate(s) within 5%' + (' (all give the same values)' if len(ties) > 1 and same else ''))
            if len(ties) == 1 or (same and name != 'PatchDraw'):
                chosen = best
        elif results:
            print(f'             best candidate only scores {results[0][0]:.2f} - too different to trust')
        new = repair(exe, name, pat, chosen[1]) if chosen else None
        if new:
            print('             values: ' + ', '.join(f'{k}=0x{v:X}' for k, v in chosen[2].items()))
            print(f'             repaired signature: {fmt(new)}')
            repairs[name] = fmt(new)
            new_known[name] = {k: v for k, v in chosen[2].items()}
            continue
        print('             could not repair automatically')
        broken.append(name)

    rvalues, rproblems = check_reflection(exe)
    status = 'OK' if not rproblems else 'BROKEN'
    print(f'[{status:10}] {"Reflection":17} (by name) ' + ', '.join(
        f'{k}=0x{v:X}' + ('' if KNOWN_161.get(k, v) == v else f' (1.61: 0x{KNOWN_161[k]:X})') for k, v in rvalues.items()))
    for p in rproblems:
        print(f'             ! {p}')
    if rproblems:
        broken.append('Reflection')

    print()
    fatal = [b for b in broken if b in REQUIRED]
    if repairs:
        if args.write:
            # keep still-valid overrides for this build and add the repairs; a stale section is replaced
            to_write = dict(effective_overrides)
            to_write.update(repairs)
            backup = write_overrides(ini_path, exe.build_id, to_write)
            print(f'Wrote {len(repairs)} repaired signature(s) for build {exe.build_id} to the ini'
                  + (f' (backup: {os.path.basename(backup)}).' if backup else '.'))
        else:
            print(f'{len(repairs)} signature(s) can be repaired. Run again with --write to store them.')
    if new_known and (args.write or not repairs):
        merged = dict(known)
        merged.update({k: {n: v for n, v in vals.items() if n not in ('game_global', 'patch_draw_function')}
                       for k, vals in new_known.items()})
        json.dump(merged, open(known_path, 'w'), indent=1)
    for b in broken:
        print(f'NEEDS MANUAL WORK: {b}' + (f' ({OPTIONAL_NOTE[b]})' if b in OPTIONAL_NOTE else ' (plugin stays inactive)'))
    if not broken and not repairs:
        print('Everything matches - the plugin supports this game version as is.')
    if fatal:
        print('\nThe game code changed shape - see "After a game update" in README.md.')
    return 2 if fatal else (1 if broken or (repairs and not args.write) else 0)


if __name__ == '__main__':
    sys.exit(main())
