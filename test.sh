#!/bin/bash
#
# mbdiscid Test Suite
# ===================
# Comprehensive tests using verified dBpoweramp reference data.
#
# Usage:
#   ./test.sh              Run all no-disc tests
#   ./test.sh /dev/rdiskN  Run all tests including disc-based tests
#
# Test data verified against AccurateRip database via dBpoweramp.
#

# Configuration
MBDISCID="${MBDISCID:-./mbdiscid}"
DEVICE="${1:-}"

# Colors (disabled if not a terminal)
if [[ -t 1 ]]; then
    RED='\033[0;31m'
    GREEN='\033[0;32m'
    YELLOW='\033[1;33m'
    NC='\033[0m'
else
    RED='' GREEN='' YELLOW='' NC=''
fi

# Counters
TESTS_RUN=0
TESTS_PASSED=0
TESTS_FAILED=0

# =============================================================================
# VERIFIED TEST DATA - From dBpoweramp CD Ripper logs and mbdiscid output
# =============================================================================
# Format notes:
#   MB_TOC:  first last leadout offset1...offsetN (offsets include +150 pregap)
#   AR_TOC:  count audio first offset1...offsetN leadout (raw LBA, no pregap)
#   FB_TOC:  count offset1...offsetN total_seconds (offsets include +150)
#   AR_ID:   NNN-XXXXXXXX-XXXXXXXX-XXXXXXXX (track_count-discid1-discid2-cddbid)
#   FB_ID:   XXXXXXXX (8 hex digits, same as last component of AR_ID)
#   MB_ID:   28-char base64-like string (MusicBrainz disc ID)
# =============================================================================

# -----------------------------------------------------------------------------
# STANDARD AUDIO CDs
# -----------------------------------------------------------------------------

# Sublime - Sublime (17 tracks, LBA 0 start)
declare -A SUBLIME=(
    [name]="Sublime - Sublime"
    [type]="audio"
    [tracks]=17
    [first_lba]=0
    [mb_toc]="1 17 263855 150 19745 32575 42805 54545 72047 85787 95555 117545 145010 150657 160517 178172 193610 215417 231297 244930"
    [ar_toc]="17 17 1 0 19595 32425 42655 54395 71897 85637 95405 117395 144860 150507 160367 178022 193460 215267 231147 244780 263705"
    [fb_toc]="17 150 19745 32575 42805 54545 72047 85787 95555 117545 145010 150657 160517 178172 193610 215417 231297 244930 3518"
    [ar_id]="017-00231e4f-01bf54d7-e00dbc11"
    [fb_id]="e00dbc11"
    [mb_id]="m.wjLfLe7XrMz1c_iAL6qo06Q4w-"
    [mcn]=""
    [isrc_expected]="1: USGA19649263
2: USGA19649251
3: USGA19649249
4: USGA19649260
5: USGA19648331
6: USGA19649253
7: USGA19649254
8: USGA19649255
9: USGA19649252
10: USGA19649250
11: USGA19649257
12: USGA19649259
13: USGA19649258
14: USGA19649261
15: USGA19649256
16: USGA19649262
17: USGA19649248"
)

# Goo Goo Dolls - Dizzy up the Girl (13 tracks, LBA 32 start)
declare -A GGD=(
    [name]="Goo Goo Dolls - Dizzy up the Girl"
    [type]="audio"
    [tracks]=13
    [first_lba]=32
    [mb_toc]="1 13 203420 182 12262 28217 46107 58452 77167 97980 112652 130482 143362 152105 173820 183620"
    [ar_toc]="13 13 1 32 12112 28067 45957 58302 77017 97830 112502 130332 143212 151955 173670 183470 203270"
    [fb_toc]="13 182 12262 28217 46107 58452 77167 97980 112652 130482 143362 152105 173820 183670 2712"
    [ar_id]="013-0015a200-00d903ba-a60a960d"
    [fb_id]="a60a960d"
    [mb_id]="eafSQC0kDG0EPmE15c7vmMp6PNs-"
    [mcn]="0075994705828"
    [isrc_expected]="1: USWB19800782
2: USWB19800781
3: USWB19800783
4: USWB19800784
5: USWB19800785
6: USWB19800786
7: USWB19800787
8: USWB19800788
9: USWB19800789
10: USWB19800790
11: USWB19800160
12: USWB19800791
13: USWB19800792"
)

# The Cranberries - Bury the Hatchet (13 tracks, LBA 0 start)
declare -A CRANBERRIES=(
    [name]="The Cranberries - Bury the Hatchet"
    [type]="audio"
    [tracks]=13
    [first_lba]=0
    [mb_toc]="1 13 214052 150 15980 28357 52925 69090 85687 102352 119185 133290 146322 160770 176690 198225"
    [ar_toc]="13 13 1 0 15830 28207 52775 68940 85537 102202 119035 133140 146172 160620 176540 198075 213902"
    [fb_toc]="13 150 15980 28357 52925 69090 85687 102352 119185 133290 146322 160770 176690 198225 2854"
    [ar_id]="013-0016e72f-00e46449-ac0b240d"
    [fb_id]="ac0b240d"
    [mb_id]="ED06vnWi7Emm8fFP1Cm7R0hBHvc-"
    [mcn]=""
    [isrc_expected]="1: USIR29900001
2: USIR29900002
3: USIR29900003
4: USIR29900004
5: USIR29900005
6: USIR29900006
7: USIR29900007
8: USIR29900008
9: USIR29900009
10: USIR29900010
11: USIR29900011
12: USIR29900012
13: USIR29900013"
)

# Matchbox Twenty - Yourself or Someone Like You (12 tracks, LBA 32 start)
declare -A MB20=(
    [name]="Matchbox Twenty - Yourself or Someone Like You"
    [type]="audio"
    [tracks]=12
    [first_lba]=32
    [mb_toc]="1 12 210860 182 17567 34492 51517 69417 86292 111805 126812 140137 158400 177687 193810"
    [ar_toc]="12 12 1 32 17417 34342 51367 69267 86142 111655 126662 139987 158250 177537 193660 210710"
    [fb_toc]="12 182 17567 34492 51517 69417 86292 111805 126812 140137 158400 177687 193810 2811"
    [ar_id]="012-00150304-00c439dc-aa0af90c"
    [fb_id]="aa0af90c"
    [mb_id]="eu.FPhvxgQ78cpBNSeKdyXTp85s-"
    [mcn]="0075679272126"
    [isrc_expected]=""
)

# Rush - Exit... Stage Left (13 tracks, LBA 0 start)
declare -A RUSH=(
    [name]="Rush - Exit... Stage Left"
    [type]="audio"
    [tracks]=13
    [first_lba]=0
    [mb_toc]="1 13 346060 150 23602 54277 89160 106172 120292 131985 171500 178762 200477 255250 280202 302760"
    [ar_toc]="13 13 1 0 23452 54127 89010 106022 120142 131835 171350 178612 200327 255100 280052 302610 345910"
    [fb_toc]="13 150 23602 54277 89160 106172 120292 131985 171500 178762 200477 255250 280202 302760 4614"
    [ar_id]="013-00227675-0159d3a9-b112040d"
    [fb_id]="b112040d"
    [mb_id]="4PtT2zz5BmntI7XfmT2dTpEsZ0E-"
    [mcn]="0731453463226"
    [isrc_expected]="1: USMR18180110
2: USMR18180111
3: USMR18180112
4: USMR18180113
5: USMR18180114
6: USMR18180115
7: USMR18180116
8: USMR18180117
9: USWWW0139107
10: USWWW0139096
11: USMR18180120
12: USMR18180121
13: USMR18180122"
)

# Dada - Puzzle (12 tracks, LBA 0 start)
declare -A DADA=(
    [name]="Dada - Puzzle"
    [type]="audio"
    [tracks]=12
    [first_lba]=0
    [mb_toc]="1 12 247562 150 27602 48552 67590 86080 102480 123680 142122 160132 179750 195157 223667"
    [ar_toc]="12 12 1 0 27452 48402 67440 85930 102330 123530 141972 159982 179600 195007 223517 247412"
    [fb_toc]="12 150 27602 48552 67590 86080 102480 123680 142122 160132 179750 195157 223667 3300"
    [ar_id]="012-0018740e-00e1baf6-b30ce20c"
    [fb_id]="b30ce20c"
    [mb_id]="EbaBnJokyGEpgZ_1CN_RAhcLqRw-"
    [mcn]=""
    [isrc_expected]=""
)

# -----------------------------------------------------------------------------
# ENHANCED CDs (data track at end)
# -----------------------------------------------------------------------------

# Metallica - St. Anger (11 audio + 1 data)
# Data track 12: LBA 349352-357655
declare -A METALLICA=(
    [name]="Metallica - St. Anger"
    [type]="enhanced"
    [tracks]=11
    [first_lba]=0
    [mb_toc]="1 11 338102 150 26427 59512 97427 121795 160052 185967 218225 242760 274965 298510"
    [ar_toc]="12 11 1 0 26277 59362 97277 121645 159902 185817 218075 242610 274815 298360 349352 357656"
    [fb_toc]="12 150 26427 59512 97427 121795 160052 185967 218225 242760 274965 298510 349502 4770"
    [ar_id]="011-001f27c4-010ea9c1-bb12a00c"
    [fb_id]="bb12a00c"
    [mb_id]="eoknU.IyXXaywKSXdaNZgbqkGZw-"
    [mcn]="0075596285322"
    [isrc_expected]="1: USEE10340428
2: USEE10340429
3: USEE10340430
4: USEE10340431
5: USEE10340432
6: USEE10340433
7: USEE10340434
8: USEE10340435
9: USEE10340436
10: USEE10340437
11: USEE10340438"
)

# Blue October - Foiled (14 audio + 1 data)
# Data track 15: LBA 321555-332527
declare -A BLUEOCT=(
    [name]="Blue October - Foiled"
    [type]="enhanced"
    [tracks]=14
    [first_lba]=0
    [mb_toc]="1 14 310305 150 7534 33634 51696 71318 95909 116841 136693 158748 181104 200303 222900 247371 280976"
    [ar_toc]="15 14 1 0 7384 33484 51546 71168 95759 116691 136543 158598 180954 200153 222750 247221 280826 321555 332528"
    [fb_toc]="15 150 7534 33634 51696 71318 95909 116841 136693 158748 181104 200303 222900 247371 280976 321705 4435"
    [ar_id]="014-00209635-01652576-e211510f"
    [fb_id]="e211510f"
    [mb_id]="hO3GT18x_9qBZL3vZhhpDexHnv8-"
    [mcn]="0602517484016"
    [isrc_expected]="1: USUM70747896
2: USUM70747897
3: USUM70747898
4: USUM70747899
5: USUM70747900
6: USUM70747901
7: USUM70747903
8: USUM70747904
9: USUM70747905
10: USUM70747906
11: USUM70747907
12: USUM70747908
13: USUM70747909
14: USUM70747910"
)

# -----------------------------------------------------------------------------
# MIXED MODE CDs (data track at beginning)
# -----------------------------------------------------------------------------

# Sarah McLachlan - The Freedom Sessions (1 data + 8 audio)
# Data track 1: LBA 0-148583, Audio tracks 2-9
declare -A FREEDOM=(
    [name]="Sarah McLachlan - The Freedom Sessions"
    [type]="mixed"
    [tracks]=8
    [first_lba]=148584
    [mb_toc]="1 9 320528 150 148734 169482 184797 202605 217733 248258 259988 278078"
    [ar_toc]="9 8 2 0 148584 169332 184647 202455 217583 248108 259838 277928 320378"
    [fb_toc]="9 150 148734 169482 184797 202605 217733 248258 259988 278078 4273"
    [ar_id]="008-001ef535-00ad3cb0-7b10af09"
    [fb_id]="7b10af09"
    [mb_id]="xYH60C0oTAOYn7y3CWYvrD7RMH4-"
    [mcn]=""
    [isrc_expected]=""
)

# =============================================================================
# TEST HELPERS
# =============================================================================

test_pass() {
    ((TESTS_PASSED++))
    echo -e "${GREEN}PASS${NC}"
}

test_fail() {
    ((TESTS_FAILED++))
    echo -e "${RED}FAIL${NC}"
    [[ -n "${1:-}" ]] && echo -e "    Expected: $1"
    [[ -n "${2:-}" ]] && echo -e "    Got:      $2"
}

run_test() {
    local desc="$1"
    local expected="$2"
    shift 2

    ((TESTS_RUN++))
    echo -n "  $desc ... "

    local actual
    actual=$("$@" 2>&1) || true

    if [[ "$actual" == "$expected" ]]; then
        test_pass
    else
        test_fail "$expected" "$actual"
    fi
}

run_test_match() {
    local desc="$1"
    local pattern="$2"
    shift 2

    ((TESTS_RUN++))
    echo -n "  $desc ... "

    local actual
    actual=$("$@" 2>&1) || true

    if [[ "$actual" =~ $pattern ]]; then
        test_pass
    else
        test_fail "match: $pattern" "$actual"
    fi
}

run_test_contains() {
    local desc="$1"
    local substring="$2"
    shift 2

    ((TESTS_RUN++))
    echo -n "  $desc ... "

    local actual
    actual=$("$@" 2>&1) || true

    if [[ "$actual" == *"$substring"* ]]; then
        test_pass
    else
        test_fail "contains: $substring" "$actual"
    fi
}

run_test_exit() {
    local desc="$1"
    local expected_rc="$2"
    shift 2

    ((TESTS_RUN++))
    echo -n "  $desc ... "

    local actual_rc
    "$@" >/dev/null 2>&1 && actual_rc=0 || actual_rc=$?

    if [[ "$actual_rc" == "$expected_rc" ]]; then
        test_pass
    else
        test_fail "exit $expected_rc" "exit $actual_rc"
    fi
}

run_test_exit_contains() {
    local desc="$1"
    local expected_rc="$2"
    local substring="$3"
    shift 3

    ((TESTS_RUN++))
    echo -n "  $desc ... "

    local actual actual_rc
    actual=$("$@" 2>&1) && actual_rc=0 || actual_rc=$?

    if [[ "$actual_rc" == "$expected_rc" ]] && [[ "$actual" == *"$substring"* ]]; then
        test_pass
    else
        test_fail "exit $expected_rc, contains '$substring'" "exit $actual_rc: $actual"
    fi
}

# Test AccurateRip ID calculation
test_ar_id() {
    local -n disc=$1
    local actual

    ((TESTS_RUN++))
    echo -n "  ${disc[name]}: AR ID ... "

    actual=$(echo "${disc[ar_toc]}" | "$MBDISCID" -Aic 2>&1) || true

    if [[ "$actual" == "${disc[ar_id]}" ]]; then
        test_pass
    else
        test_fail "${disc[ar_id]}" "$actual"
    fi
}

# Test FreeDB ID calculation
test_fb_id() {
    local -n disc=$1
    local actual

    ((TESTS_RUN++))
    echo -n "  ${disc[name]}: FreeDB ID ... "

    actual=$(echo "${disc[fb_toc]}" | "$MBDISCID" -Fic 2>&1) || true

    if [[ "$actual" == "${disc[fb_id]}" ]]; then
        test_pass
    else
        test_fail "${disc[fb_id]}" "$actual"
    fi
}

# Test MusicBrainz ID calculation
test_mb_id() {
    local -n disc=$1
    local actual

    ((TESTS_RUN++))
    echo -n "  ${disc[name]}: MB ID ... "

    actual=$(echo "${disc[mb_toc]}" | "$MBDISCID" -Mic 2>&1) || true

    if [[ "$actual" == "${disc[mb_id]}" ]]; then
        test_pass
    else
        test_fail "${disc[mb_id]}" "$actual"
    fi
}

# =============================================================================
# TEST CATEGORIES
# =============================================================================

echo "========================================"
echo "  mbdiscid Test Suite"
echo "========================================"
echo ""

# Check binary exists
if [[ ! -x "$MBDISCID" ]]; then
    echo -e "${RED}Error: $MBDISCID not found or not executable${NC}"
    exit 1
fi

# Show which binary we're testing
echo "  Binary: $(cd "$(dirname "$MBDISCID")" && pwd)/$(basename "$MBDISCID")"
echo "  Version: $("$MBDISCID" -V 2>&1)"
echo ""

# -----------------------------------------------------------------------------
echo -e "${YELLOW}=== Standalone Options ===${NC}"
# -----------------------------------------------------------------------------

run_test_exit "-h exits 0" 0 "$MBDISCID" -h
run_test_exit "--help exits 0" 0 "$MBDISCID" --help
run_test_exit "-V exits 0" 0 "$MBDISCID" -V
run_test_exit "--version exits 0" 0 "$MBDISCID" --version
run_test_exit "-L exits 0" 0 "$MBDISCID" -L
run_test_exit "no args fails" 64 "$MBDISCID"
run_test_contains "-h shows Usage" "Usage:" "$MBDISCID" -h
run_test_contains "-V shows version" "1.1.0" "$MBDISCID" -V
run_test_contains "no args shows usage" "Usage:" "$MBDISCID"

# -----------------------------------------------------------------------------
echo ""
echo -e "${YELLOW}=== AccurateRip ID Verification ===${NC}"
# -----------------------------------------------------------------------------

test_ar_id SUBLIME
test_ar_id GGD
test_ar_id CRANBERRIES
test_ar_id MB20
test_ar_id RUSH
test_ar_id DADA
test_ar_id METALLICA
test_ar_id BLUEOCT
test_ar_id FREEDOM

# -----------------------------------------------------------------------------
echo ""
echo -e "${YELLOW}=== FreeDB ID Verification ===${NC}"
# -----------------------------------------------------------------------------

test_fb_id SUBLIME
test_fb_id GGD
test_fb_id CRANBERRIES
test_fb_id MB20
test_fb_id RUSH
test_fb_id DADA
test_fb_id METALLICA
test_fb_id BLUEOCT
test_fb_id FREEDOM

# -----------------------------------------------------------------------------
echo ""
echo -e "${YELLOW}=== MusicBrainz ID Verification ===${NC}"
# -----------------------------------------------------------------------------

test_mb_id SUBLIME
test_mb_id GGD
test_mb_id CRANBERRIES
test_mb_id MB20
test_mb_id RUSH
test_mb_id DADA
test_mb_id METALLICA
test_mb_id BLUEOCT
test_mb_id FREEDOM

# -----------------------------------------------------------------------------
echo ""
echo -e "${YELLOW}=== Mode Mutual Exclusivity ===${NC}"
# -----------------------------------------------------------------------------

run_test_exit_contains "-MF fails" 64 "mutually exclusive" "$MBDISCID" -MFc
run_test_exit_contains "-MA fails" 64 "mutually exclusive" "$MBDISCID" -MAc
run_test_exit_contains "-aM fails" 64 "mutually exclusive" "$MBDISCID" -aMc
run_test_exit_contains "-AF fails" 64 "mutually exclusive" "$MBDISCID" -AFc
run_test_exit_contains "-RM fails" 64 "mutually exclusive" "$MBDISCID" -RMc

# -----------------------------------------------------------------------------
echo ""
echo -e "${YELLOW}=== Invalid Option Combinations ===${NC}"
# -----------------------------------------------------------------------------

run_test_exit_contains "-Cc requires disc" 64 "require a physical disc" "$MBDISCID" -Cc
run_test_exit_contains "-Ic requires disc" 64 "require a physical disc" "$MBDISCID" -Ic
run_test_exit_contains "-Tc requires disc" 64 "require a physical disc" "$MBDISCID" -Tc
run_test_exit_contains "-Rc invalid" 64 "mutually exclusive" "$MBDISCID" -Rc 1 2 3000 150 1000
run_test_exit_contains "-ac invalid" 64 "mutually exclusive" "$MBDISCID" -ac 1 2 3000 150 1000
run_test_exit_contains "-Au invalid" 64 "not supported" "$MBDISCID" -Auc
run_test_exit_contains "-Fu invalid" 64 "not supported" "$MBDISCID" -Fuc
run_test_exit_contains "-Ao invalid" 64 "not supported" "$MBDISCID" -Aoc

# -----------------------------------------------------------------------------
echo ""
echo -e "${YELLOW}=== TOC Input Validation ===${NC}"
# -----------------------------------------------------------------------------

# Note: -Mc uses MusicBrainz format: first last leadout offset1 ... offsetN
run_test_exit "-c with no data fails" 65 sh -c "echo '' | '$MBDISCID' -c"
run_test_exit "-c with letters fails" 65 sh -c "echo 'a b c' | '$MBDISCID' -c"
run_test_exit "-c insufficient data fails" 65 sh -c "echo '1 2' | '$MBDISCID' -c"
run_test_exit_contains "-c with device path" 64 "expects TOC data" "$MBDISCID" -c /dev/cdrom
run_test_exit "-c negative number fails" 65 sh -c "echo '1 1 1000 -150' | '$MBDISCID' -Mc"
run_test_exit "Non-monotonic offsets" 65 sh -c "echo '1 3 3000 150 1000 500' | '$MBDISCID' -Mc"
run_test_exit "Leadout before last" 65 sh -c "echo '1 2 500 150 1000' | '$MBDISCID' -Mc"
run_test_exit "first > last" 65 sh -c "echo '5 3 3000 150 1000 2000' | '$MBDISCID' -Mc"
run_test_exit "first = 0" 65 sh -c "echo '0 2 3000 150 1000' | '$MBDISCID' -Mc"
run_test_exit "last > 99" 65 sh -c "echo '1 100 3000 150 1000' | '$MBDISCID' -Mc"

# -----------------------------------------------------------------------------
echo ""
echo -e "${YELLOW}=== Default Mode Behavior ===${NC}"
# -----------------------------------------------------------------------------

# -c alone defaults to MusicBrainz
((TESTS_RUN++))
echo -n "  -c alone defaults to MusicBrainz ... "
result_c=$(echo "${SUBLIME[mb_toc]}" | "$MBDISCID" -c 2>&1) || true
result_Mc=$(echo "${SUBLIME[mb_toc]}" | "$MBDISCID" -Mc 2>&1) || true
if [[ "$result_c" == "$result_Mc" ]]; then
    test_pass
else
    test_fail "$result_Mc" "$result_c"
fi

# -ic defaults to MusicBrainz
((TESTS_RUN++))
echo -n "  -ic defaults to MusicBrainz ... "
result_ic=$(echo "${SUBLIME[mb_toc]}" | "$MBDISCID" -ic 2>&1) || true
result_Mic=$(echo "${SUBLIME[mb_toc]}" | "$MBDISCID" -Mic 2>&1) || true
if [[ "$result_ic" == "$result_Mic" ]]; then
    test_pass
else
    test_fail "$result_Mic" "$result_ic"
fi

# -----------------------------------------------------------------------------
echo ""
echo -e "${YELLOW}=== Output Format Verification ===${NC}"
# -----------------------------------------------------------------------------

# AccurateRip ID format: NNN-XXXXXXXX-XXXXXXXX-XXXXXXXX
run_test_match "AR ID format" '^[0-9]{3}-[0-9a-f]{8}-[0-9a-f]{8}-[0-9a-f]{8}$' \
    sh -c "echo '${RUSH[ar_toc]}' | '$MBDISCID' -Aic"

# FreeDB ID format: 8 hex digits
run_test_match "FreeDB ID format" '^[0-9a-f]{8}$' \
    sh -c "echo '${RUSH[fb_toc]}' | '$MBDISCID' -Fic"

# MusicBrainz ID format
run_test_match "MB ID format" '^[A-Za-z0-9._-]{27,28}$' \
    sh -c "echo '${RUSH[mb_toc]}' | '$MBDISCID' -Mic"

# -----------------------------------------------------------------------------
echo ""
echo -e "${YELLOW}=== Edge Cases ===${NC}"
# -----------------------------------------------------------------------------

# Single track disc
run_test_exit "Single track disc" 0 sh -c "echo '1 1 15000 150' | '$MBDISCID' -Mic"

# Two track disc
run_test_exit "Two track disc" 0 sh -c "echo '1 2 30000 150 15000' | '$MBDISCID' -Mic"

# Track numbers starting at 2 (like mixed mode)
run_test_exit "First track = 2" 0 sh -c "echo '2 5 50000 150 10000 20000 30000' | '$MBDISCID' -Mic"

# Non-zero first LBA (like GGD at LBA 32)
run_test_match "Non-zero first LBA" '^[0-9]{3}-' \
    sh -c "echo '${GGD[ar_toc]}' | '$MBDISCID' -Aic"

# Quiet mode
run_test_exit "-q suppresses errors" 65 sh -c "echo 'invalid' | '$MBDISCID' -qc"
((TESTS_RUN++))
echo -n "  -q produces no stderr ... "
stderr_output=$(echo "invalid" | "$MBDISCID" -qc 2>&1 >/dev/null) || true
if [[ -z "$stderr_output" ]]; then
    test_pass
else
    test_fail "empty" "$stderr_output"
fi

# Quiet mode with valid input still outputs
run_test_match "-q still outputs valid result" '^[0-9]{3}-' \
    sh -c "echo '${SUBLIME[ar_toc]}' | '$MBDISCID' -qAic"

# -----------------------------------------------------------------------------
echo ""
echo -e "${YELLOW}=== Input Methods ===${NC}"
# -----------------------------------------------------------------------------

# Stdin pipe
run_test "Pipe input" "${SUBLIME[ar_id]}" sh -c "echo '${SUBLIME[ar_toc]}' | '$MBDISCID' -Aic"

# Here-string
run_test "Here-string input" "${CRANBERRIES[ar_id]}" bash -c "'$MBDISCID' -Aic <<< '${CRANBERRIES[ar_toc]}'"

# Command line arguments
run_test "Args input" "${DADA[ar_id]}" "$MBDISCID" -Aic ${DADA[ar_toc]}

# =============================================================================
# DISC-BASED TESTS (only if device provided)
# =============================================================================

if [[ -n "$DEVICE" ]]; then
    echo ""
    echo "========================================"
    echo "  Disc-Based Tests: $DEVICE"
    echo "========================================"

    # Check device exists
    if [[ ! -e "$DEVICE" ]]; then
        echo -e "${RED}Error: Device $DEVICE does not exist${NC}"
    else
        # Try to detect which disc is inserted
        echo ""
        echo -e "${YELLOW}=== Disc Detection ===${NC}"

        ((TESTS_RUN++))
        echo -n "  Reading disc... "
        disc_ar_id=$("$MBDISCID" -Ai "$DEVICE" 2>/dev/null) || disc_ar_id=""

        if [[ -z "$disc_ar_id" ]]; then
            test_fail "readable disc" "no response"
            echo "  Could not read disc. Is there a disc in the drive?"
        else
            test_pass
            echo "  Detected AR ID: $disc_ar_id"

            # Match against known discs
            detected=""
            for disc_name in SUBLIME GGD CRANBERRIES MB20 RUSH DADA METALLICA BLUEOCT FREEDOM; do
                declare -n disc_ref=$disc_name
                if [[ "$disc_ar_id" == "${disc_ref[ar_id]}" ]]; then
                    detected=$disc_name
                    break
                fi
            done

            if [[ -n "$detected" ]]; then
                declare -n known_disc=$detected
                echo -e "  ${GREEN}Matched: ${known_disc[name]}${NC}"
                echo "  Type: ${known_disc[type]}"

                # -------------------------------------------------------------
                echo ""
                echo -e "${YELLOW}=== Disc Read Tests ===${NC}"
                # -------------------------------------------------------------

                # Run non-ISRC modes first (these don't hold exclusive access)
                run_test_exit "Read disc -M" 0 "$MBDISCID" -M "$DEVICE"
                run_test_exit "Read disc -A" 0 "$MBDISCID" -A "$DEVICE"
                run_test_exit "Read disc -F" 0 "$MBDISCID" -F "$DEVICE"
                run_test_exit "Read disc -R" 0 "$MBDISCID" -R "$DEVICE"
                run_test_exit "Read disc -T" 0 "$MBDISCID" -T "$DEVICE"

                # Verify calculated IDs match
                run_test "AR ID matches expected" "${known_disc[ar_id]}" "$MBDISCID" -Ai "$DEVICE"
                run_test "FreeDB ID matches expected" "${known_disc[fb_id]}" "$MBDISCID" -Fi "$DEVICE"
                run_test "MB ID matches expected" "${known_disc[mb_id]}" "$MBDISCID" -Mi "$DEVICE"

                # -------------------------------------------------------------
                echo ""
                echo -e "${YELLOW}=== MCN Test ===${NC}"
                # -------------------------------------------------------------

                ((TESTS_RUN++))
                echo -n "  MCN read ... "
                mcn=$("$MBDISCID" -C "$DEVICE" 2>/dev/null) || mcn=""

                if [[ -n "${known_disc[mcn]}" ]]; then
                    # Expected specific MCN value
                    if [[ "$mcn" == "${known_disc[mcn]}" ]]; then
                        test_pass
                        echo "    MCN: $mcn"
                    else
                        test_fail "${known_disc[mcn]}" "${mcn:-empty}"
                    fi
                else
                    # No MCN expected
                    if [[ -z "$mcn" ]]; then
                        test_pass
                        echo "    (No MCN expected)"
                    else
                        echo -e "${YELLOW}UNEXPECTED${NC}"
                        echo "    Found MCN: $mcn (not in test data)"
                    fi
                fi

                # -------------------------------------------------------------
                echo ""
                echo -e "${YELLOW}=== Media Type Test ===${NC}"
                # -------------------------------------------------------------

                ((TESTS_RUN++))
                echo -n "  Media type ... "
                type_output=$("$MBDISCID" -T "$DEVICE" 2>/dev/null) || type_output=""

                expected_type=""
                case "${known_disc[type]}" in
                    audio)    expected_type="Audio CD" ;;
                    enhanced) expected_type="Enhanced CD" ;;
                    mixed)    expected_type="Mixed Mode CD" ;;
                esac

                if [[ "$type_output" == *"$expected_type"* ]]; then
                    test_pass
                    echo "    Type: $expected_type"
                else
                    test_fail "$expected_type" "$type_output"
                fi

                # -------------------------------------------------------------
                # ISRC and -a tests last (they trigger ISRC scanning which holds
                # exclusive device access for several seconds after completion)
                # -------------------------------------------------------------
                echo ""
                echo -e "${YELLOW}=== ISRC Test ===${NC}"
                # -------------------------------------------------------------

                ((TESTS_RUN++))
                echo -n "  ISRC read ... "
                isrc_output=$("$MBDISCID" -I "$DEVICE" 2>/dev/null) || isrc_output=""

                if [[ -n "${known_disc[isrc_expected]}" ]]; then
                    # Expected specific ISRC values - exact match
                    if [[ "$isrc_output" == "${known_disc[isrc_expected]}" ]]; then
                        test_pass
                        isrc_count=$(echo "$isrc_output" | grep -c ':' || true)
                        echo "    Found $isrc_count ISRCs (exact match)"
                    else
                        test_fail "exact ISRC match" "$isrc_output"
                        echo "    Expected:"
                        echo "${known_disc[isrc_expected]}" | head -3 | sed 's/^/      /'
                        echo "      ..."
                    fi
                else
                    # No ISRCs expected
                    if [[ -z "$isrc_output" ]]; then
                        test_pass
                        echo "    (No ISRCs expected)"
                    else
                        echo -e "${YELLOW}UNEXPECTED${NC}"
                        echo "    Found ISRCs (not in test data):"
                        echo "$isrc_output" | head -3 | sed 's/^/      /'
                    fi
                fi

                # -------------------------------------------------------------
                echo ""
                echo -e "${YELLOW}=== -a Output Format Test ===${NC}"
                # -------------------------------------------------------------

                ((TESTS_RUN++))
                echo -n "  -a contains all sections ... "
                all_output=$("$MBDISCID" -a "$DEVICE" 2>/dev/null) || all_output=""

                missing_sections=""
                for section in "Media" "Raw" "AccurateRip" "FreeDB" "MusicBrainz"; do
                    if [[ "$all_output" != *"$section"* ]]; then
                        missing_sections="$missing_sections $section"
                    fi
                done

                if [[ -z "$missing_sections" ]]; then
                    test_pass
                else
                    test_fail "all sections" "missing:$missing_sections"
                fi

            else
                # Unknown disc - run format validation tests only
                echo -e "${YELLOW}  Unknown disc (not in test database)${NC}"
                echo "  AR ID: $disc_ar_id"
                echo ""
                echo "  Running format validation tests..."

                # -------------------------------------------------------------
                echo ""
                echo -e "${YELLOW}=== Basic Read Tests ===${NC}"
                # -------------------------------------------------------------

                # Run non-ISRC modes first
                run_test_exit "Read disc -M" 0 "$MBDISCID" -M "$DEVICE"
                run_test_exit "Read disc -A" 0 "$MBDISCID" -A "$DEVICE"
                run_test_exit "Read disc -F" 0 "$MBDISCID" -F "$DEVICE"
                run_test_exit "Read disc -R" 0 "$MBDISCID" -R "$DEVICE"
                run_test_exit "Read disc -T" 0 "$MBDISCID" -T "$DEVICE"

                # -------------------------------------------------------------
                echo ""
                echo -e "${YELLOW}=== Output Format Validation ===${NC}"
                # -------------------------------------------------------------

                # AR ID format
                ((TESTS_RUN++))
                echo -n "  AR ID format valid ... "
                if [[ "$disc_ar_id" =~ ^[0-9]{3}-[0-9a-f]{8}-[0-9a-f]{8}-[0-9a-f]{8}$ ]]; then
                    test_pass
                else
                    test_fail "NNN-XXXXXXXX-XXXXXXXX-XXXXXXXX" "$disc_ar_id"
                fi

                # FB ID format
                ((TESTS_RUN++))
                echo -n "  FreeDB ID format valid ... "
                fb_id=$("$MBDISCID" -Fi "$DEVICE" 2>/dev/null) || fb_id=""
                if [[ "$fb_id" =~ ^[0-9a-f]{8}$ ]]; then
                    test_pass
                else
                    test_fail "8 hex digits" "$fb_id"
                fi

                # MB ID format
                ((TESTS_RUN++))
                echo -n "  MB ID format valid ... "
                mb_id=$("$MBDISCID" -Mi "$DEVICE" 2>/dev/null) || mb_id=""
                if [[ "$mb_id" =~ ^[A-Za-z0-9._-]{27,28}$ ]]; then
                    test_pass
                else
                    test_fail "28-char base64-like" "$mb_id"
                fi

                # -------------------------------------------------------------
                echo ""
                echo -e "${YELLOW}=== MCN/ISRC Read Tests ===${NC}"
                # -------------------------------------------------------------

                # MCN read doesn't crash
                ((TESTS_RUN++))
                echo -n "  MCN read (no crash) ... "
                mcn=$("$MBDISCID" -C "$DEVICE" 2>/dev/null) && mcn_rc=0 || mcn_rc=$?
                if [[ $mcn_rc -eq 0 ]]; then
                    test_pass
                    if [[ -n "$mcn" ]]; then
                        echo "    Found MCN: $mcn"
                    else
                        echo "    (No MCN)"
                    fi
                else
                    test_fail "exit 0" "exit $mcn_rc"
                fi

                # ISRC read doesn't crash
                ((TESTS_RUN++))
                echo -n "  ISRC read (no crash) ... "
                isrc=$("$MBDISCID" -I "$DEVICE" 2>/dev/null) && isrc_rc=0 || isrc_rc=$?
                if [[ $isrc_rc -eq 0 ]]; then
                    test_pass
                    if [[ -n "$isrc" ]]; then
                        isrc_count=$(echo "$isrc" | grep -c ':' || true)
                        echo "    Found $isrc_count ISRCs"
                    else
                        echo "    (No ISRCs)"
                    fi
                else
                    test_fail "exit 0" "exit $isrc_rc"
                fi

                # -a output sections (also triggers ISRC scanning)
                ((TESTS_RUN++))
                echo -n "  -a contains all sections ... "
                all_output=$("$MBDISCID" -a "$DEVICE" 2>/dev/null) || all_output=""

                missing_sections=""
                for section in "Media" "Raw" "AccurateRip" "FreeDB" "MusicBrainz"; do
                    if [[ "$all_output" != *"$section"* ]]; then
                        missing_sections="$missing_sections $section"
                    fi
                done

                if [[ -z "$missing_sections" ]]; then
                    test_pass
                else
                    test_fail "all sections" "missing:$missing_sections"
                fi
            fi
        fi
    fi
fi

# =============================================================================
# SUMMARY
# =============================================================================

echo ""
echo "========================================"
echo "  Summary"
echo "========================================"
echo ""
echo "  Tests run:    $TESTS_RUN"
echo -e "  ${GREEN}Passed:       $TESTS_PASSED${NC}"
if [[ $TESTS_FAILED -gt 0 ]]; then
    echo -e "  ${RED}Failed:       $TESTS_FAILED${NC}"
else
    echo "  Failed:       0"
fi
echo ""

if [[ $TESTS_FAILED -eq 0 ]]; then
    echo -e "${GREEN}All tests passed!${NC}"
    exit 0
else
    echo -e "${RED}Some tests failed.${NC}"
    exit 1
fi
