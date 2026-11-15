#!/usr/bin/env bash
# Runs every test that needs no board and no PC services: the C code compiled on the PC and the small Python tests.
# Used by the CI (.github/workflows/tests.yml) and by hand:   bash scripts/run_host_tests.sh
set -u
cd "$(dirname "$0")/.."
FW=firmware/main
failed=0
passed=0
run() {  # name, command...
  local name="$1"; shift
  if out=$("$@" 2>&1); then
    echo "ok    $name: $(echo "$out" | tail -1)"; passed=$((passed + 1))
  else
    echo "FAIL  $name"; echo "$out" | tail -15 | sed 's/^/      /'; failed=$((failed + 1))
  fi
}
build_and_run() {  # name, build flags..., then run
  local name="$1"; shift
  local exe="/tmp/bug_test_$name.exe"
  if gcc "$@" -o "$exe" -lm 2>/tmp/bug_test_build.txt; then
    run "$name" "$exe"
  else
    echo "FAIL  $name (does not compile)"; tail -8 /tmp/bug_test_build.txt | sed 's/^/      /'; failed=$((failed + 1))
  fi
}
T=server/skills_test
# the tests run from their own folder (some read local recordings with relative paths, and skip them when absent)
cd $T
FWR=../../$FW
build_and_run follow_ups   context_test.c $FWR/local_context.c -I$FWR
build_and_run card_skills  -DSDSKILLS_HOST sdskills_test.c $FWR/local_sdskills.c $FWR/local_context.c -I$FWR
build_and_run smart_home   -DHOME_HOST home_test.c $FWR/local_home.c $FWR/local_context.c -I$FWR
build_and_run mqtt_packets -DMQTT_HOST mqtt_test.c $FWR/mqtt_lite.c -I$FWR
awk '/^static bool cut_prefix/{p=1} p{print} p&&/^static void strip_fillers/{s=1} s&&/^}/{exit}' $FWR/local_skills.c > fillers_body.inc
build_and_run fillers      fillers_test.c
rm -f fillers_body.inc
build_and_run voice_id     -DVOICE_ID_HOST voiceid_test.c $FWR/voice_id.c -I$FWR
cd ../..
PY=python3; python3 --version >/dev/null 2>&1 || PY=python  # (on Windows "python3" can be a Store shortcut that does nothing)
run free_chat      $PY server/llm_chat_test.py
run pretend_home   $PY server/sim_home_test.py
echo "$passed passed, $failed failed"
[ "$failed" -eq 0 ]
