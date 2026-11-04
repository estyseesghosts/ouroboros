#!/bin/sh
# tools-local/session.sh -- Stream S5 (metrics) gym-walk session driver.
#
# Usage: session.sh NN CMD [args...]
#   NN                       run number; the run lives in logs/NN/
#   prep [--edge-on]         gate logs/NN/env.txt + args.txt + scene.txt.
#                            Default asserts EIDOS_EDGE=0 (outlines-off
#                            phase first); --edge-on asserts it is absent
#                            (outlines-on repeat).
#   start                    open the session: stamp the wall clock, print the
#                            mandatory `nxlink -s | tee` capture command.
#   shot NAME                gate logs/NN/NAME.jpg (size + black/flat gates).
#   temp start|end CELSIUS   record the SoC temp at session start/end.
#   collect                  verify boot/stderr/stdout landed, report gates.
#   want3 BASE WANT3         compare two stdout captures: the want=3-forced
#                            diagnostic must lower present with g2dread 0,
#                            else STOP (exit 3).
#   validate                 run the validator on logs/NN/ (no hardware).
#   close                    refuse unless everything passes (exit 1 + reasons).
#
# A gym walk may move (parked is NOT required) but must stay in the scene
# named in logs/NN/scene.txt for every cited window. A run that mixes
# scenes (loader/menu) is VOID, never PASS: close refuses, nothing is cited.
# Legend and box-ticking live in logs/next/checklist.txt, never in code.
# No builds, no pushes, no hardware probing beyond nxlink + file drops.

set -u
OPS="${SWITCH_OPS:-$(cd "$(dirname "$0")/.." && pwd)}"
LOGS="$OPS/logs"
MIN_SHOT_BYTES=30000
MIN_PROF_LINES=4
MIN_WALL_S=120
# Scenes that are never citable: the loader, menus, the fallback picture.
BAD_SCENES="unknown fallback boot loader title title_menu menu black"

fail() { echo "session.sh: $*" >&2; exit 1; }
need() { command -v "$1" >/dev/null 2>&1 || fail "missing tool: $1"; }

usage() {
    sed -n '2,/^# No builds/p' "$0" | sed 's/^# \{0,1\}//'
    exit "${1:-0}"
}

[ $# -ge 2 ] || usage 1
NN="$1"; CMD="$2"; shift 2
D="$LOGS/$NN"
[ -d "$D" ] || fail "no run dir $D (create it with env.txt + args.txt + scene.txt first)"

# ---- small file helpers -------------------------------------------------
prof_lines() { grep -h "^prof frame" "$D/stdout.txt" 2>/dev/null; }
prof_count() { prof_lines | wc -l | tr -d ' '; }
wall_avgs() {
    # avg wall ms/frame per window, one per line, first window dropped
    prof_lines | sed -E 's/.*\(([0-9]+) frames, ([0-9.]+) ms\/frame wall.*/\1 \2/' \
        | tail -n +2 | awk '$1>0{print $2}'
}
bucket_avg() {
    # $1 = bucket name -> avg per window, first window dropped
    b="$1"
    prof_lines | tail -n +2 \
        | sed -E "s/.* $b ([0-9.]+)\\/([0-9.]+).*/\\1 \\2/" \
        | awk '$1 ~ /^[0-9.]+$/ {print $1}'
}
field_vals() {
    # $1 = window field name (fps|memfree) -> one value per line, window 1 dropped
    f="$1"
    prof_lines | tail -n +2 \
        | sed -n -E "s/.* $f (-?[0-9.]+).*/\\1/p"
}
scene_vals() {
    # scene tag per window, first window dropped (untagged lines print nothing)
    prof_lines | tail -n +2 \
        | sed -n -E 's/.* scene ([A-Za-z0-9_]+).*/\1/p'
}
every_frames() {
    # profile window from args.txt (--profile N on its own line), default 300
    awk 'BEGIN{e=300} $1=="--profile"{getline; if($1 ~ /^[0-9]+$/) e=$1} END{print e}' \
        "$D/args.txt" 2>/dev/null || echo 300
}
drift_pct() {
    # stdin: one number per line -> "PCT N" (drift %, sample count)
    awk 'NR==1{min=max=$1} {if($1<min)min=$1; if($1>max)max=$1; s+=$1; n++}
         END{if(n==0||s==0){print "0 0"} else {print (max-min)/(s/n)*100, n}}'
}
trend_pct() {
    # stdin: one number per line (time order) -> first-half vs second-half drift %
    awk '{v[NR]=$1} END{if(NR<2){print "0 "NR; exit} h=int(NR/2); s1=s2=0;
         for(i=1;i<=h;i++)s1+=v[i]; for(i=h+1;i<=NR;i++)s2+=v[i];
         m1=s1/h; m2=s2/(NR-h); if(m1==0){print "0 "NR; exit}
         print (m2-m1)/m1*100, NR}'
}

# ---- prep ----------------------------------------------------------------
cmd_prep() {
    edge_on=0
    [ "${1:-}" = "--edge-on" ] && edge_on=1
    [ -f "$D/env.txt" ] || fail "$D/env.txt missing"
    [ -f "$D/args.txt" ] || fail "$D/args.txt missing"
    [ -f "$D/scene.txt" ] || fail "$D/scene.txt missing (one line: the gym field scene, e.g. field)"
    grep -q '^EIDOS_NXLINK=1$' "$D/env.txt" \
        || fail "$D/env.txt lacks ^EIDOS_NXLINK=1\$ (nxlink -s stdout is mandatory)"
    if [ "$edge_on" -eq 0 ]; then
        grep -q '^EIDOS_EDGE=0$' "$D/env.txt" \
            || fail "EDGE=0 first: $D/env.txt lacks ^EIDOS_EDGE=0\$ (outlines-off phase runs before --edge-on)"
    else
        grep -q '^EIDOS_EDGE=0$' "$D/env.txt" \
            && fail "outlines-on repeat must NOT set EIDOS_EDGE=0"
    fi
    grep -q '^--profile$' "$D/args.txt" || fail "$D/args.txt lacks --profile"
    grep -q '^300$' "$D/args.txt" || fail "$D/args.txt lacks the 300 window line"
    scene=$(head -1 "$D/scene.txt" | tr -d ' \t\r\n')
    [ -n "$scene" ] || fail "$D/scene.txt is empty"
    case " $BAD_SCENES " in *" $scene "*) fail "scene.txt names '$scene', which is never citable";; esac
    echo "env:"; cat "$D/env.txt"; echo "args:"; cat "$D/args.txt"; echo "scene: $scene"
    echo "Push env.txt + args.txt to the Switch (sdmc:/switch/eidos/), then:"
    echo "  session.sh $NN start"
    date +%s > "$D/session-prep.txt"
    echo "prep $NN ok ($(cat "$D/session-prep.txt"))"
}

# ---- start ---------------------------------------------------------------
cmd_start() {
    [ -f "$D/session-prep.txt" ] || fail "run prep first: session.sh $NN prep"
    need nxlink || fail "nxlink not on PATH (devkitPro). stdout capture is mandatory: nxlink -s | tee $D/stdout.txt"
    date +%s > "$D/session-started.txt"
    echo "session $NN started at $(cat "$D/session-started.txt")"
    echo "MANDATORY capture (keep it running for the whole session):"
    echo "  nxlink -s 2>\"$D/nxlink-stderr.txt\" | tee \"$D/stdout.txt\""
    echo "Then: launch the game, walk the gym (moving allowed, parked not"
    echo "required), stay in '$(head -1 "$D/scene.txt" 2>/dev/null)', hold wall>=$MIN_WALL_S s."
}

# ---- shot gate -----------------------------------------------------------
shot_stats() {
    # $1 = jpg path -> "BYTES MEAN STDDEV" (via ffmpeg, else sips+bmp, else bytes only)
    p="$1"; bytes=$(wc -c < "$p" | tr -d ' ')
    if command -v ffmpeg >/dev/null 2>&1; then
        stats=$(ffmpeg -v error -i "$p" -vf "scale=64:64,format=gray" \
            -f rawvideo -pix_fmt gray - 2>/dev/null \
            | python3 -c 'import sys,statistics
d=sys.stdin.buffer.read()
if len(d)<100: print("0 0"); raise SystemExit
m=sum(d)/len(d); print(m,(sum((x-m)**2 for x in d)/len(d))**0.5)') \
            && { echo "$bytes $stats"; return; }
    fi
    if command -v sips >/dev/null 2>&1; then
        tmp="$(mktemp /tmp/shotstat.XXXXXX.bmp)"
        if sips -s format bmp -z 64 64 "$p" --out "$tmp" >/dev/null 2>&1; then
            stats=$(python3 - "$tmp" <<'EOF'
import struct,sys
d=open(sys.argv[1],'rb').read()
off=struct.unpack('<I',d[10:14])[0]
px=d[off:]
m=sum(px)/len(px)
print(m,(sum((x-m)**2 for x in px)/len(px))**0.5)
EOF
) && { rm -f "$tmp"; echo "$bytes $stats"; return; }
        fi
        rm -f "$tmp"
    fi
    echo "$bytes -1 -1"
}

cmd_shot() {
    [ $# -ge 1 ] || fail "usage: session.sh $NN shot NAME"
    p="$D/$1.jpg"
    [ -f "$p" ] || fail "shot missing: $p (drop the capture at $p first)"
    set -- $(shot_stats "$p")
    bytes="$1"; mean="$2"; sd="$3"
    echo "shot $p bytes=$bytes mean=$mean sd=$sd"
    [ "$bytes" -ge "$MIN_SHOT_BYTES" ] \
        || fail "shot rejected (too small for 1280x720: black/empty?): $p"
    if [ "$mean" != "-1" ]; then
        awk "BEGIN{exit !( $mean < 8 )}" \
            && fail "shot rejected (black frame, mean luma $mean): $p"
        awk "BEGIN{exit !( $sd < 4.0 )}" \
            && fail "shot rejected (flat frame, probable menu/loader): $p"
    else
        echo "WARN: no decoder (ffmpeg/sips); size gate only" >&2
    fi
    echo "shot $1 ok"
}

# ---- temp -----------------------------------------------------------------
cmd_temp() {
    [ $# -eq 2 ] || fail "usage: session.sh $NN temp start|end CELSIUS"
    case "$1" in start|end) ;; *) fail "temp wants start|end" ;; esac
    case "$2" in *[!0-9.]*) fail "temp wants a number, got $2" ;; esac
    echo "$2" > "$D/temp-$1.txt"
    echo "temp $1 $2C recorded"
}

# ---- collect --------------------------------------------------------------
cmd_collect() {
    for f in boot.txt stderr.txt stdout.txt; do
        [ -f "$D/$f" ] || fail "missing $D/$f (pull sdmc:/switch/eidos/$f into $D/ first)"
    done
    echo "--- quick gates (close enforces):"
    echo "prof lines: $(prof_count) (need >= $MIN_PROF_LINES)"
    if [ -f "$D/session-started.txt" ]; then
        echo "wall: $(( $(date +%s) - $(cat "$D/session-started.txt") ))s (need >= $MIN_WALL_S)"
    else
        echo "wall: no session-started.txt (start not run)"
    fi
    tail -2 "$D/stdout.txt" 2>/dev/null
}

# ---- want3 diagnostic ------------------------------------------------------
cmd_want3() {
    # session.sh NN want3 BASE_STDOUT WANT3_STDOUT
    [ $# -eq 2 ] || fail "usage: session.sh $NN want3 BASE_STDOUT WANT3_STDOUT"
    base="$1"; w3="$2"
    [ -f "$base" ] || fail "missing $base"; [ -f "$w3" ] || fail "missing $w3"
    for f in "$base" "$w3"; do
        grep -q '^prof frame' "$f" || fail "no prof lines in $f"
    done
    if ! grep -h '^prof frame' "$w3" | grep -q 'want=3'; then
        echo "STOP: want=3 build not detected (no want=3 census in $w3)." >&2
        echo "Apply the noted one-line want|3 force, rebuild, rerun. Do not proceed." >&2
        exit 3
    fi
    gp=$(grep -h '^prof frame' "$base" | tail -n +2 \
        | sed -E 's/.* present ([0-9.]+)\/.*/\1/' | awk '{s+=$1;n++}END{print s/n}')
    wp=$(grep -h '^prof frame' "$w3" | tail -n +2 \
        | sed -E 's/.* present ([0-9.]+)\/.*/\1/' | awk '{s+=$1;n++}END{print s/n}')
    echo "present avg: gated=$gp want3=$wp (must fall)"
    awk "BEGIN{exit !( $wp < $gp )}" \
        || { echo "STOP: present did not fall under want=3 ($gp -> $wp). Reopen the queue theory; do not proceed." >&2; exit 3; }
    for f in "$base" "$w3"; do
        ga=$(grep -h '^prof frame' "$f" | tail -n +2 \
            | sed -E 's/.* g2dread ([0-9.]+)\/([0-9.]+).*/\1/' | sort -n | tail -1)
        awk "BEGIN{exit !( $ga <= 0.01 )}" \
            || { echo "STOP: g2dread avg $ga in $f (must stay ~0)." >&2; exit 3; }
    done
    echo "want3 diagnostic PASS: present falls ($gp -> $wp), g2dread 0"
}

# ---- validator --------------------------------------------------------------
cmd_validate() {
    [ -f "$D/stdout.txt" ] || fail "no $D/stdout.txt (collect first)"
    every=$(every_frames)
    n=$(prof_count)
    echo "== golden format ($n prof lines, window $every)"
    bad=$(prof_lines | grep -v -c ' max):.* preup .* predraw .* preswap .* prefin .* unacc ' || true)
    [ "$n" -ge "$MIN_PROF_LINES" ] || { echo "FORMAT: FAIL ($n < $MIN_PROF_LINES prof lines)"; bad=9999; }
    [ "${bad:-0}" -eq 0 ] && echo "FORMAT: PASS (run-43 bucket set + unacc on all lines)" \
        || echo "FORMAT: FAIL ($bad lines miss the golden tail)"
    echo "== window fields (fps, scene, memfree: S5 build)"
    for f in fps scene memfree; do
        if prof_lines | tail -n +2 | grep -q " $f=" 2>/dev/null || prof_lines | grep -q " $f "; then
            echo "field $f: present"
        else
            echo "field $f: absent"
        fi
    done
    if prof_lines | grep -q ' scene '; then
        echo "(scene tags present; attribution checked below)"
    else
        echo "(no scene tags: pre-S5 build, windows unattributed -> SCENE VOID)"
    fi
    echo "== census (contractual: want, aqueue; rest informational)"
    for lbl in want aqueue; do
        grep -q -h "^prof frame.* $lbl=" "$D/stdout.txt" \
            && echo "census $lbl: present" || echo "census $lbl: MISSING -> FAIL"
    done
    for lbl in paint_n upbytes_canvas upbytes_screen upcalls upskip swapgap pbos skip_n gxcmds verts polys pboq ngtex texup texrespec read_async ui_redraw ui_skip pool_hi; do
        grep -q -h "^prof frame.* $lbl=" "$D/stdout.txt" \
            && echo "census $lbl: present (info)" || echo "census $lbl: absent (info; owning stream not merged?)"
    done
    echo "== steady detector (gym walk may move; transition window 1 dropped)"
    set -- $(wall_avgs | drift_pct); wd="${1:-0}"; wn="${2:-0}"
    set -- $(bucket_avg present | drift_pct); pd="${1:-0}"
    zfps=$(grep -c 'eidos: 0 fps' "$D/stdout.txt" || true)
    echo "wall drift=$wd% (need <10), present drift=$pd% (need <15), 0-fps seconds=$zfps (need 0), n=$wn"
    if awk "BEGIN{exit !( $wd < 10 && $pd < 15 )}"; then
        [ "$zfps" -eq 0 ] && echo "STEADY: PASS" || echo "STEADY: VOID (0-fps seconds present)"
    else
        echo "STEADY: VOID (drift over bound: loader/menu mix, thermal, or wild walk?)"
    fi
    if field_vals memfree | grep -q '^[0-9]'; then
        set -- $(field_vals memfree | trend_pct); md="${1:-0}"
        echo "memfree trend=$md% (decline voids: NXMEM-6)"
        awk "BEGIN{exit !( $md < -5 )}" && echo "MEM: VOID (free memory trends down)" || echo "MEM: ok"
    else
        echo "MEM: unknown (no memfree values; pre-S5 or non-Switch build)"
    fi
    echo "== scene (every cited window must carry the run scene)"
    target=""; [ -f "$D/scene.txt" ] && target=$(head -1 "$D/scene.txt" | tr -d ' \t\r\n')
    if grep -q ' scene ' "$D/stdout.txt"; then
        echo "window scenes:"; scene_vals | sort | uniq -c | sort -rn
        echo "change lines:"; grep -h 'eidos: scene' "$D/stdout.txt" | sort | uniq -c | sort -rn || echo "(none)"
        if [ -z "$target" ]; then
            echo "SCENE: VOID (no scene.txt target to compare)"
        else
            off=$(scene_vals | grep -v -c "^${target}$" || true)
            badscene=0
            for s in $BAD_SCENES; do
                scene_vals | grep -q "^${s}$" && badscene=1
            done
            if [ "${off:-0}" -eq 0 ] && [ "$badscene" -eq 0 ]; then
                echo "SCENE: PASS (all cited windows '$target')"
            else
                echo "SCENE: VOID (windows leave '$target': non-comparable, do not cite)"
            fi
        fi
    else
        echo "change lines:"; grep -h 'eidos: scene' "$D/stdout.txt" | sort | uniq -c | sort -rn || echo "(none)"
        echo "SCENE: VOID (windows unattributed: pre-S5 build or scene mismatch)"
    fi
    echo "== hitches (informational; a hitch never fails the run, drift may void it)"
    grep -h '^hitch' "$D/stdout.txt" || echo "(no hitch lines)"
    echo "== artifacts (build-info.txt identity)"
    if [ -f "$D/build-info.txt" ]; then
        # shellcheck disable=SC1090
        . "$D/build-info.txt"
        sent=$(grep -o -E 'Sending eidos.nro, [0-9]+ bytes' "$D/stdout.txt" | tail -1 | grep -o -E '[0-9]+' | head -1)
        [ -n "${nro_bytes:-}" ] && [ "$sent" = "$nro_bytes" ] \
            && echo "NRO bytes: $sent matches build-info" \
            || echo "NRO bytes: stdout($sent) vs build-info(${nro_bytes:-?}) -> FAIL"
        if [ -n "${nro_sha:-}" ] && [ -n "${nro_path:-}" ] && [ -f "$nro_path" ]; then
            got=$(shasum -a 256 "$nro_path" | cut -d' ' -f1)
            [ "$got" = "$nro_sha" ] && echo "NRO sha: match" || echo "NRO sha: MISMATCH -> FAIL"
        else
            echo "NRO sha: unchecked (need nro_sha + nro_path in build-info.txt)"
        fi
        [ "${o0_count:-?}" = "0" ] && echo "-O2 proof: o0_count=0" || echo "-O2 proof: o0_count=${o0_count:-?} -> FAIL"
        awk "BEGIN{exit !( ${wasm_bytes:-0} <= ${wasm_cap:-18874368} )}" \
            && echo "wasm: ${wasm_bytes:-?} <= cap (info)" || echo "wasm: over cap (info)"
        case "${mesa:-}" in *16732f6*) echo "Mesa strings: 16732f6 present";; *) echo "Mesa strings: MISSING -> FAIL";; esac
        [ "${drm_nouveau:-?}" = "0" ] && echo "drm_nouveau: 0" || echo "drm_nouveau: ${drm_nouveau:-?} -> FAIL"
    else
        echo "ARTIFACTS: VOID (no build-info.txt; record NRO bytes+sha, o0_count, wasm, mesa, drm at build)"
    fi
    echo "== transport/exit"
    [ -f "$D/nxlink-stderr.txt" ] \
        && [ "$(wc -c < "$D/nxlink-stderr.txt" | tr -d ' ')" -le 64 ] \
        && echo "nxlink stderr: capture-noise only" \
        || echo "nxlink stderr: MISSING or noisy -> FAIL"
    tail -2 "$D/stdout.txt" | grep -q 'NXEXIT=0' \
        && echo "exit: NXEXIT=0 clean" || echo "exit: no clean NXEXIT=0 -> FAIL"
    echo "== verdict"
    cmd_verdict_quiet
}

cmd_verdict_quiet() {
    # recompute the FAIL conditions; print VALIDATOR line; exit 0/1 (never 3)
    # FAIL (wrong data) dominates VOID (missing/non-comparable data).
    every=$(every_frames); n=$(prof_count)
    bad=$(prof_lines | grep -v -c ' max):.* preup .* predraw .* preswap .* prefin .* unacc ' || true)
    verdict=PASS
    [ "$n" -ge "$MIN_PROF_LINES" ] && [ "${bad:-0}" -eq 0 ] || verdict=FAIL
    for lbl in want aqueue; do
        grep -q -h "^prof frame.* $lbl=" "$D/stdout.txt" || verdict=FAIL
    done
    set -- $(wall_avgs | drift_pct); wd="${1:-0}"
    set -- $(bucket_avg present | drift_pct); pd="${1:-0}"
    zfps=$(grep -c 'eidos: 0 fps' "$D/stdout.txt" || true)
    if ! awk "BEGIN{exit !( $wd < 10 && $pd < 15 )}"; then
        [ "$verdict" = "FAIL" ] || verdict=VOID
    elif [ "$zfps" -ne 0 ]; then
        [ "$verdict" = "FAIL" ] || verdict=VOID
    fi
    if field_vals memfree | grep -q '^[0-9]'; then
        set -- $(field_vals memfree | trend_pct); md="${1:-0}"
        if awk "BEGIN{exit !( $md < -5 )}"; then
            [ "$verdict" = "FAIL" ] || verdict=VOID
        fi
    fi
    target=""; [ -f "$D/scene.txt" ] && target=$(head -1 "$D/scene.txt" | tr -d ' \t\r\n')
    if grep -q ' scene ' "$D/stdout.txt" && [ -n "$target" ]; then
        off=$(scene_vals | grep -v -c "^${target}$" || true)
        badscene=0
        for s in $BAD_SCENES; do
            scene_vals | grep -q "^${s}$" && badscene=1
        done
        if [ "${off:-0}" -ne 0 ] || [ "$badscene" -ne 0 ]; then
            [ "$verdict" = "FAIL" ] || verdict=VOID
        fi
    else
        [ "$verdict" = "FAIL" ] || verdict=VOID
    fi
    if [ -f "$D/build-info.txt" ]; then
        # shellcheck disable=SC1090
        . "$D/build-info.txt"
        sent=$(grep -o -E 'Sending eidos.nro, [0-9]+ bytes' "$D/stdout.txt" | tail -1 | grep -o -E '[0-9]+' | head -1)
        [ -n "${nro_bytes:-}" ] && [ "$sent" = "$nro_bytes" ] || verdict=FAIL
        if [ -n "${nro_sha:-}" ] && [ -n "${nro_path:-}" ] && [ -f "$nro_path" ]; then
            got=$(shasum -a 256 "$nro_path" | cut -d' ' -f1)
            [ "$got" = "$nro_sha" ] || verdict=FAIL
        fi
        [ "${o0_count:-?}" = "0" ] || verdict=FAIL
        case "${mesa:-}" in *16732f6*) ;; *) verdict=FAIL;; esac
        [ "${drm_nouveau:-?}" = "0" ] || verdict=FAIL
    else
        [ "$verdict" = "FAIL" ] || verdict=VOID
    fi
    [ -f "$D/nxlink-stderr.txt" ] \
        && [ "$(wc -c < "$D/nxlink-stderr.txt" | tr -d ' ')" -le 64 ] || verdict=FAIL
    tail -2 "$D/stdout.txt" | grep -q 'NXEXIT=0' || verdict=FAIL
    echo "VALIDATOR $NN: $verdict"
    [ "$verdict" = "PASS" ]
}

# ---- close ---------------------------------------------------------------
cmd_close() {
    reasons=""
    [ -f "$D/session-started.txt" ] || reasons="$reasons no-start;"
    if [ -f "$D/session-started.txt" ]; then
        wall=$(( $(date +%s) - $(cat "$D/session-started.txt") ))
        [ "$wall" -ge "$MIN_WALL_S" ] || reasons="$reasons wall=${wall}s<${MIN_WALL_S}s;"
    fi
    [ "$(prof_count)" -ge "$MIN_PROF_LINES" ] || reasons="$reasons prof-lines<4;"
    for s in gym_start gym_end; do
        [ -f "$D/$s.jpg" ] || reasons="$reasons no-$s.jpg;"
    done
    [ -f "$D/temp-start.txt" ] || reasons="$reasons no-temp-start;"
    [ -f "$D/temp-end.txt" ] || reasons="$reasons no-temp-end;"
    if [ -n "$reasons" ]; then
        echo "CLOSE REFUSED ($NN): $reasons" >&2
        echo "Tick boxes only with run $NN evidence; fix the session, do not cite it." >&2
        exit 1
    fi
    cmd_verdict_quiet || {
        echo "CLOSE REFUSED ($NN): validator not PASS (see above). Do not cite this run." >&2
        exit 1
    }
    echo "RUN $NN CITABLE: validator PASS, $(prof_count) prof lines, wall ${wall}s, NXEXIT=0."
    echo "Paste into the checklist with run id $NN: bytes + sha + prof-count + shots + temps."
}

case "$CMD" in
    prep)     cmd_prep "$@" ;;
    start)    cmd_start "$@" ;;
    shot)     cmd_shot "$@" ;;
    temp)     cmd_temp "$@" ;;
    collect)  cmd_collect "$@" ;;
    want3)    cmd_want3 "$@" ;;
    validate) cmd_validate "$@" ;;
    close)    cmd_close "$@" ;;
    *) usage 1 ;;
esac
