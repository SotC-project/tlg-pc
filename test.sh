#!/bin/bash
# Headless test run: ./test.sh <seconds> [VAR=value ...]
# Lowest priority of the three ports: waits (WAITMAX s, default 1800) until no SotC or inFAMOUS
# instance runs and RAM is free, and yields at once when one starts.
cd /d/tlg-pc/testrun || exit 1
secs=$1; shift
others='shadps4|sotc_ab|ShadowOfTheColossus|^i3.exe|inFAMOUS Second|SnippingTool'
memfree_mb() { while read -r k v _; do [ "$k" = "MemFree:" ] && { echo $((v / 1024)); return; }; done < /proc/meminfo; }
for ((w = 0; w < ${WAITMAX:-1800}; w++)); do
    # Free for 30 s in a row (other sessions' batches leave only a few seconds between runs), or GO.
    if [ "$(tasklist | grep -ciE "$others")" = "0" ] && [ "$(memfree_mb)" -ge 10000 ]; then
        free=$((free + 1)); { [ -f /d/tlg-pc/GO ] || [ $free -ge 6 ]; } && break
    else free=0; fi
    sleep 5
done
[ "$(tasklist | grep -ciE "$others")" != "0" ] && { echo "REFUSED: another game is running"; exit 1; }
[ "$(tasklist | grep -ci '^tlg.exe')" != "0" ] && { echo "REFUSED: tlg.exe already running"; exit 1; }
# Slot protocol with the inFAMOUS session: it touches GO when it leaves a gap; RUNNING tells it
# to wait (runs are short); SLOT_DONE says the slot is over.
rm -f /d/tlg-pc/SLOT_DONE; touch /d/tlg-pc/RUNNING
trap 'rm -f /d/tlg-pc/RUNNING /d/tlg-pc/GO; touch /d/tlg-pc/SLOT_DONE' EXIT
bin=${BIN:-/d/tlg-pc/bin}
env SOTC_HEADLESS=1 ${MUTE-SOTC_MUTE=1} "$@" "$bin/tlg.exe" -f false --game "D:/Games/CUSA03627/eboot.bin" > /dev/null 2>&1 &
pid=$!
for ((i = 0; i < secs; i++)); do
    kill -0 $pid 2>/dev/null || break
    if [ "$(tasklist | grep -ciE "$others")" != "0" ]; then echo "YIELDED: another game started"; break; fi
    if [ "$(memfree_mb)" -lt 3000 ]; then echo "YIELDED: memory low"; break; fi
    sleep 1
done
if kill -0 $pid 2>/dev/null; then kill -9 $pid 2>/dev/null; sleep 2; fi
[ "$(tasklist | grep -ci '^tlg.exe')" != "0" ] && { taskkill //IM tlg.exe //F > /dev/null 2>&1; sleep 2; }
echo "waited $((w*5))s, ran ${i}s, flips $(grep -c 'Flip: fps' user/log/shad_log.txt)"
grep -n "Critical" -A1 user/log/shad_log.txt | head -6 | cut -c1-300
