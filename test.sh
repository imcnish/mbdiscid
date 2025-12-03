#!/bin/bash
# mbdiscid test suite
# Usage: ./test.sh [device]
# Example: ./test.sh /dev/rdisk19

DEVICE="${1:-}"
PROG="./mbdiscid"

# Sample TOC data (17-track disc)
RAW_TOC="1 17 150 19745 32575 42805 54545 72047 85787 95555 117545 145010 150657 160517 178172 193610 215417 231297 244930 263855"
MB_TOC="1 17 263855 150 19745 32575 42805 54545 72047 85787 95555 117545 145010 150657 160517 178172 193610 215417 231297 244930"
AR_TOC="17 263855 150 19745 32575 42805 54545 72047 85787 95555 117545 145010 150657 160517 178172 193610 215417 231297 244930"
FB_TOC="17 150 19745 32575 42805 54545 72047 85787 95555 117545 145010 150657 160517 178172 193610 215417 231297 244930 3518"
FB_TOC_WITH_ID="e00dbc11 17 150 19745 32575 42805 54545 72047 85787 95555 117545 145010 150657 160517 178172 193610 215417 231297 244930 3518"

pass=0
fail=0

run_test() {
    local desc="$1"
    local expect="$2"  # "pass" or "fail"
    local cmd="$3"

    echo -n "TEST: $desc ... "
    output=$(eval "$cmd" 2>&1)
    rc=$?

    if [[ "$expect" == "pass" && $rc -eq 0 ]] || [[ "$expect" == "fail" && $rc -ne 0 ]]; then
        echo "OK"
        ((pass++))
    else
        echo "FAILED (rc=$rc, expected $expect)"
        echo "  Command: $cmd"
        echo "  Output: $output"
        ((fail++))
    fi
}

echo "========================================"
echo "mbdiscid Test Suite"
echo "========================================"
echo ""

# ========================================
# HELP AND VERSION
# ========================================
echo "--- Help and Version ---"
run_test "-h shows help" pass "$PROG -h"
run_test "--help shows help" pass "$PROG --help"
run_test "-V shows version" pass "$PROG -V"
run_test "--version shows version" pass "$PROG --version"

# ========================================
# LIST DRIVES
# ========================================
echo ""
echo "--- List Drives ---"
run_test "-l lists drives" pass "$PROG -l"
run_test "--list-drives lists drives" pass "$PROG --list-drives"
run_test "-l with other options (exits early)" pass "$PROG -lM"

# ========================================
# CALCULATE MODE - RAW FORMAT (default input)
# ========================================
echo ""
echo "--- Calculate Mode: Raw Format (default input) ---"
run_test "-c with raw TOC (args)" pass "$PROG -c $RAW_TOC"
run_test "-c with raw TOC (quoted)" pass "$PROG -c '$RAW_TOC'"
run_test "-c with raw TOC (stdin)" pass "echo '$RAW_TOC' | $PROG -c"
run_test "-Rc explicit raw mode" pass "$PROG -Rc $RAW_TOC"
run_test "-Rct raw TOC only" pass "$PROG -Rct $RAW_TOC"

# ========================================
# CALCULATE MODE - MUSICBRAINZ FORMAT
# ========================================
echo ""
echo "--- Calculate Mode: MusicBrainz Format ---"
run_test "-Mc with MB TOC" pass "$PROG -Mc $MB_TOC"
run_test "-Mci MB ID only" pass "$PROG -Mci $MB_TOC"
run_test "-Mct MB TOC only" pass "$PROG -Mct $MB_TOC"
run_test "-Mcu MB URL only" pass "$PROG -Mcu $MB_TOC"
run_test "-Mcitu MB all actions" pass "$PROG -Mcitu $MB_TOC"

# ========================================
# CALCULATE MODE - ACCURATERIP FORMAT
# ========================================
echo ""
echo "--- Calculate Mode: AccurateRip Format ---"
run_test "-Ac with AR TOC (native)" pass "$PROG -Ac $AR_TOC"
run_test "-Ac with MB TOC (auto-detect)" pass "$PROG -Ac $MB_TOC"
run_test "-Aci AR ID only" pass "$PROG -Aci $AR_TOC"
run_test "-Act AR TOC only" pass "$PROG -Act $AR_TOC"

# ========================================
# CALCULATE MODE - FREEDB FORMAT
# ========================================
echo ""
echo "--- Calculate Mode: FreeDB Format ---"
run_test "-Fc with FB TOC" pass "$PROG -Fc $FB_TOC"
run_test "-Fc with FB TOC (with discid)" pass "$PROG -Fc $FB_TOC_WITH_ID"
run_test "-Fci FB ID only" pass "$PROG -Fci $FB_TOC"
run_test "-Fct FB TOC only" pass "$PROG -Fct $FB_TOC"

# ========================================
# CALCULATE MODE - ALL MODES
# ========================================
echo ""
echo "--- Calculate Mode: All Modes ---"
run_test "-ac all modes from raw TOC" pass "$PROG -ac $RAW_TOC"
run_test "-c defaults to all (skips MCN/ISRC)" pass "$PROG -c $RAW_TOC"

# ========================================
# QUIET MODE
# ========================================
echo ""
echo "--- Quiet Mode ---"
run_test "-q suppresses errors" pass "$PROG -cq 2>&1 | grep -q . && exit 1 || exit 0"
run_test "-Mcq with valid TOC" pass "$PROG -Mcq $MB_TOC"

# ========================================
# EXPECTED FAILURES - USAGE ERRORS
# ========================================
echo ""
echo "--- Expected Failures: Usage Errors ---"
run_test "No args (no device)" fail "$PROG"
run_test "-c without TOC data" fail "$PROG -c"
run_test "-c with empty stdin" fail "echo '' | $PROG -c"
run_test "Mutually exclusive: -R -M" fail "$PROG -RMc $RAW_TOC"
run_test "Mutually exclusive: -A -F" fail "$PROG -AFc $AR_TOC"
run_test "Mutually exclusive: -M -a" fail "$PROG -Mac $MB_TOC"
run_test "Mutually exclusive: -C -I" fail "$PROG -CIc $RAW_TOC"
run_test "-C requires disc (explicit)" fail "$PROG -Cc $RAW_TOC"
run_test "-I requires disc (explicit)" fail "$PROG -Ic $RAW_TOC"
run_test "-u not supported for Raw" fail "$PROG -Rcu $RAW_TOC"
run_test "-u not supported for AR" fail "$PROG -Acu $AR_TOC"
run_test "-u not supported for FB" fail "$PROG -Fcu $FB_TOC"
run_test "-o not supported for Raw" fail "$PROG -Rco $RAW_TOC"
run_test "-o not supported for AR" fail "$PROG -Aco $AR_TOC"
run_test "-o not supported for FB" fail "$PROG -Fco $FB_TOC"

# ========================================
# EXPECTED FAILURES - TOC ERRORS
# ========================================
echo ""
echo "--- Expected Failures: TOC Errors ---"
run_test "TOC: insufficient data" fail "$PROG -c 1 2"
run_test "TOC: bad number" fail "$PROG -c 1 17 abc 19745"
run_test "TOC: non-monotonic offsets" fail "$PROG -Rc 1 17 150 19745 15000 42805 263855"
run_test "TOC: leadout before last track" fail "$PROG -Rc 1 3 150 1000 2000 1500"
run_test "TOC: invalid track range (first=0)" fail "$PROG -Rc 0 5 150 1000 2000 3000 4000 5000 6000"
run_test "TOC: invalid track range (last>99)" fail "$PROG -Rc 1 100 150 1000 263855"
run_test "TOC: invalid track range (first>last)" fail "$PROG -Rc 5 3 150 1000 2000 3000"
run_test "TOC: MB format wrong count" fail "$PROG -Mc 1 17 263855 150 19745"
run_test "TOC: AR format wrong count" fail "$PROG -Ac 17 263855 150"
run_test "TOC: FB format wrong count" fail "$PROG -Fc 17 150 19745 3518"

# ========================================
# EDGE CASES
# ========================================
echo ""
echo "--- Edge Cases ---"
run_test "Single track disc" pass "$PROG -Mc 1 1 15000 150"
run_test "Two track disc" pass "$PROG -Mc 1 2 30000 150 15000"
run_test "20 track disc" pass "$PROG -Mc 1 20 200000 150 10000 20000 30000 40000 50000 60000 70000 80000 90000 100000 110000 120000 130000 140000 150000 160000 170000 180000 190000"
run_test "First track not 1" pass "$PROG -Mc 3 5 50000 150 10000 20000"
run_test "Large offsets (near 80 min limit)" pass "$PROG -Mc 1 2 350000 150 300000"
run_test "Minimum valid TOC" pass "$PROG -Mc 1 1 200 150"
run_test "Tab-separated TOC (stdin)" pass "printf '1\t2\t30000\t150\t15000' | $PROG -Mc"
run_test "Extra whitespace in TOC" pass "$PROG -Mc '1   2    30000   150   15000'"
run_test "Duplicate mode flag -MM" pass "$PROG -MMc $MB_TOC"
run_test "Duplicate action flag -ii" pass "$PROG -Mciit $MB_TOC"

# ========================================
# EXPECTED FAILURES - USER MISTAKES
# ========================================
echo ""
echo "--- Expected Failures: User Mistakes ---"
run_test "Invalid short option" fail "$PROG -x"
run_test "Unknown long option" fail "$PROG --foo"
run_test "Invalid device path" fail "$PROG /dev/nonexistent"
run_test "Regular file as device" fail "$PROG /etc/passwd"
run_test "Empty device argument" fail "$PROG ''"
run_test "Multiple devices" fail "$PROG /dev/sr0 /dev/sr1"
run_test "-c with device path" fail "$PROG -Mc /dev/rdisk1"
run_test "Action only, no device" fail "$PROG -i"
run_test "Negative number in TOC" fail "$PROG -Mc 1 2 -100 150 500"
run_test "Floating point in TOC" fail "$PROG -Mc 1 2 30000.5 150 15000"
run_test "Overflow number in TOC" fail "$PROG -Mc 1 2 99999999999 150 1000"
run_test "Hex number in TOC (not FB)" fail "$PROG -Mc 1 2 0x1000 150 500"
run_test "Whitespace-only stdin" fail "echo '   ' | $PROG -Mc"
run_test "Letters in TOC" fail "$PROG -Mc 1 2 abc 150 500"
run_test "Special chars in TOC" fail "$PROG -Mc 1 2 1000! 150 500"

# ========================================
# OUTPUT VERIFICATION
# ========================================
echo ""
echo "--- Output Verification ---"
# Verify known disc ID calculations
run_test "MB ID matches expected" pass "$PROG -Mci $MB_TOC | grep -q 'm.wjLfLe7XrMz1c_iAL6qo06Q4w-'"
run_test "FB ID matches expected" pass "$PROG -Fci $FB_TOC | grep -q 'e00dbc11'"
run_test "AR ID format correct" pass "$PROG -Aci $AR_TOC | grep -qE '^[0-9]{3}-[0-9a-f]{8}-[0-9a-f]{8}-[0-9a-f]{8}$'"

# Verify URL format
run_test "MB URL format correct" pass "$PROG -Mcu $MB_TOC | grep -q 'http://musicbrainz.org/cdtoc/attach'"

# Verify TOC output formats
run_test "Raw TOC starts with '1 17 150'" pass "$PROG -Rct $RAW_TOC | grep -q '^1 17 150'"
run_test "MB TOC starts with '1 17 263855'" pass "$PROG -Mct $MB_TOC | grep -q '^1 17 263855'"
run_test "AR TOC starts with '17 263855'" pass "$PROG -Act $AR_TOC | grep -q '^17 263855'"
run_test "FB TOC ends with total seconds" pass "$PROG -Fct $FB_TOC | grep -q '3518$'"

# ========================================
# PHYSICAL DISC TESTS (if device provided)
# ========================================
if [[ -n "$DEVICE" ]]; then
    echo ""
    echo "--- Physical Disc Tests ($DEVICE) ---"
    run_test "Read disc (default -a)" pass "$PROG $DEVICE"
    run_test "Read disc -M" pass "$PROG -M $DEVICE"
    run_test "Read disc -Mi" pass "$PROG -Mi $DEVICE"
    run_test "Read disc -Mt" pass "$PROG -Mt $DEVICE"
    run_test "Read disc -Mu" pass "$PROG -Mu $DEVICE"
    run_test "Read disc -Mitu" pass "$PROG -Mitu $DEVICE"
    run_test "Read disc -A" pass "$PROG -A $DEVICE"
    run_test "Read disc -F" pass "$PROG -F $DEVICE"
    run_test "Read disc -R" pass "$PROG -R $DEVICE"
    run_test "Read disc -C (MCN)" pass "$PROG -C $DEVICE"
    run_test "Read disc -I (ISRC)" pass "$PROG -I $DEVICE"
    run_test "Read disc -a (all)" pass "$PROG -a $DEVICE"
    # Note: -o tests would open browser, skip in automated testing
else
    echo ""
    echo "--- Physical Disc Tests ---"
    echo "SKIPPED: No device specified. Run with: $0 /dev/rdiskN"
fi

# ========================================
# SUMMARY
# ========================================
echo ""
echo "========================================"
echo "Results: $pass passed, $fail failed"
echo "========================================"

exit $fail
