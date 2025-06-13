#!/bin/bash
# Copyright (c) 2025 Ian McNish
#
# mbdiscid Option Logic Test Suite
# Tests all option combinations and error conditions

# Test data for calculate mode
CDTOC="1 12 150 17477 32100 47997 67160 84650 93732 110667 127377 147860 160437 183097 198592"

# Count of total failures
FAIL_CTR=0

echo '=== MBDISCID OPTION LOGIC TEST SUITE ==='
echo

# Function to test a command and report result
test_cmd() {
  local desc="$1"
  local cmd="$2"
  local expect_fail="$3"

  echo -n "Testing: ${desc}"
  echo -n "  Command: ${cmd}"

  if eval "${cmd}" >/dev/null 2>&1; then
    if [[ "${expect_fail}" = 'fail' ]]; then
      # Should have failed
      echo ' [FAIL]'
      ((FAIL_CTR++))
      return 1
    else
      echo ' [PASS]'
      return 0
    fi
  else
    if [[ "${expect_fail}" = 'fail' ]]; then
      # Failed as expected
      echo ' [PASS]'
      return 0
    else
      # Unexpected failure
      echo ' [FAIL]'
      ((FAIL_CTR++))
      return 1
    fi
  fi
}

echo '### Basic single action tests (should pass) ###'
test_cmd "Default (no action = -i)" "./mbdiscid -c ${CDTOC}"
test_cmd "Explicit -i" "./mbdiscid -i -c ${CDTOC}"
test_cmd "TOC only (-t)" "./mbdiscid -t -c ${CDTOC}"
test_cmd "ID and TOC (-T)" "./mbdiscid -T -c ${CDTOC}"
test_cmd "Raw TOC (-r)" "./mbdiscid -r -c ${CDTOC}"
test_cmd "URL (-u)" "./mbdiscid -u -c ${CDTOC}"
test_cmd "All info (-a)" "./mbdiscid -a -c ${CDTOC}"
echo

echo '### Mode tests (should pass) ###'
test_cmd "Default mode (MusicBrainz)" "./mbdiscid -c ${CDTOC}"
test_cmd "Explicit MusicBrainz (-M)" "./mbdiscid -M -c ${CDTOC}"
test_cmd "FreeDB mode (-F)" "./mbdiscid -F -c ${CDTOC}"
test_cmd "FreeDB with ID (-F -i)" "./mbdiscid -F -i -c ${CDTOC}"
test_cmd "FreeDB with TOC (-F -t)" "./mbdiscid -F -t -c ${CDTOC}"
test_cmd "FreeDB with ID+TOC (-F -T)" "./mbdiscid -F -T -c ${CDTOC}"
echo

echo '### Open flag combinations (should pass) ###'
test_cmd "Open alone (-o)" "./mbdiscid -o -c ${CDTOC}"
test_cmd "ID with open (-i -o)" "./mbdiscid -i -o -c ${CDTOC}"
test_cmd "TOC with open (-t -o)" "./mbdiscid -t -o -c ${CDTOC}"
test_cmd "ID+TOC with open (-T -o)" "./mbdiscid -T -o -c ${CDTOC}"
test_cmd "URL with open (-u -o)" "./mbdiscid -u -o -c ${CDTOC}"
echo

echo '### Multiple action errors (should fail) ###'
test_cmd "ID and TOC (-i -t)" "./mbdiscid -i -t -c ${CDTOC}" "fail"
test_cmd "ID and URL (-i -u)" "./mbdiscid -i -u -c ${CDTOC}" "fail"
test_cmd "TOC and URL (-t -u)" "./mbdiscid -t -u -c ${CDTOC}" "fail"
test_cmd "Raw and ID (-r -i)" "./mbdiscid -r -i -c ${CDTOC}" "fail"
test_cmd "Raw and TOC (-r -t)" "./mbdiscid -r -t -c ${CDTOC}" "fail"
test_cmd "Raw and URL (-r -u)" "./mbdiscid -r -u -c ${CDTOC}" "fail"
test_cmd "All and ID (-a -i)" "./mbdiscid -a -i -c ${CDTOC}" "fail"
test_cmd "All and TOC (-a -t)" "./mbdiscid -a -t -c ${CDTOC}" "fail"
test_cmd "All and URL (-a -u)" "./mbdiscid -a -u -c ${CDTOC}" "fail"
test_cmd "ID, TOC, URL (-i -t -u)" "./mbdiscid -i -t -u -c ${CDTOC}" "fail"
echo

echo '### Mode conflict errors (should fail) ###'
test_cmd "Both modes (-M -F)" "./mbdiscid -M -F -c ${CDTOC}" "fail"
test_cmd "Both modes reversed (-F -M)" "./mbdiscid -F -M -c ${CDTOC}" "fail"
echo

echo '### Mode with raw/all errors (should fail) ###'
test_cmd "MusicBrainz with raw (-M -r)" "./mbdiscid -M -r -c ${CDTOC}" "fail"
test_cmd "FreeDB with raw (-F -r)" "./mbdiscid -F -r -c ${CDTOC}" "fail"
test_cmd "MusicBrainz with all (-M -a)" "./mbdiscid -M -a -c ${CDTOC}" "fail"
test_cmd "FreeDB with all (-F -a)" "./mbdiscid -F -a -c ${CDTOC}" "fail"
echo

echo '### FreeDB URL/open errors (should fail) ###'
test_cmd "FreeDB with URL (-F -u)" "./mbdiscid -F -u -c ${CDTOC}" "fail"
test_cmd "FreeDB with open (-F -o)" "./mbdiscid -F -o -c ${CDTOC}" "fail"
test_cmd "FreeDB with URL+open (-F -u -o)" "./mbdiscid -F -u -o -c ${CDTOC}" "fail"
echo

echo '### Invalid CDTOC data (should fail) ###'
test_cmd "Missing arguments" "./mbdiscid -c" "fail"
test_cmd "Only first track" "./mbdiscid -c 1" "fail"
test_cmd "Only first and last" "./mbdiscid -c 1 12" "fail"
test_cmd "Missing leadout" "./mbdiscid -c 1 2 150 17477" "fail"
test_cmd "Too many offsets" "./mbdiscid -c 1 2 150 17477 32100 47997 67160" "fail"
test_cmd "Invalid track range (0)" "./mbdiscid -c 0 12 ${CDTOC}" "fail"
test_cmd "Invalid track range (100)" "./mbdiscid -c 1 100 ${CDTOC}" "fail"
test_cmd "First > Last" "./mbdiscid -c 12 1 ${CDTOC}" "fail"
echo

echo '### Long option tests (should pass) ###'
test_cmd "Long --id" "./mbdiscid --id -c ${CDTOC}"
test_cmd "Long --toc" "./mbdiscid --toc -c ${CDTOC}"
test_cmd "Long --id-toc" "./mbdiscid --id-toc -c ${CDTOC}"
test_cmd "Long --raw" "./mbdiscid --raw -c ${CDTOC}"
test_cmd "Long --url" "./mbdiscid --url -c ${CDTOC}"
test_cmd "Long --all" "./mbdiscid --all -c ${CDTOC}"
test_cmd "Long --musicbrainz" "./mbdiscid --musicbrainz -c ${CDTOC}"
test_cmd "Long --freedb" "./mbdiscid --freedb -c ${CDTOC}"
test_cmd "Long --open" "./mbdiscid --open -c ${CDTOC}"
test_cmd "Long --calculate" "./mbdiscid --calculate ${CDTOC}"
echo

echo '### Mixed short/long options (should pass) ###'
test_cmd "Short action, long mode" "./mbdiscid -i --freedb -c ${CDTOC}"
test_cmd "Long action, short mode" "./mbdiscid --id -F -c ${CDTOC}"
test_cmd "Mixed everything" "./mbdiscid --id -o --calculate ${CDTOC}"
echo

echo "=== TEST SUMMARY ==="
echo "Failed tests: ${FAIL_CTR}"
echo "Check output above for any FAIL results"
echo
echo "Note: This assumes ./mbdiscid exists in current directory"
echo "Note: -o tests may fail if OPEN_CMD not supported on platform"
echo
