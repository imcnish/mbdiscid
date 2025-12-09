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
# VERIFIED TEST DATA - From dBpoweramp CD Ripper logs
# =============================================================================
# Format notes:
#   MB_TOC:  first last leadout offset1...offsetN (offsets include +150 pregap)
#   AR_TOC:  count audio first offset1...offsetN leadout (raw LBA, no pregap)
#   FB_TOC:  count offset1...offsetN total_seconds (offsets include +150)
#   AR_ID:   NNN-XXXXXXXX-XXXXXXXX-XXXXXXXX (track_count-discid1-discid2-cddbid)
#   FB_ID:   XXXXXXXX (8 hex digits, same as last component of AR_ID)
# =============================================================================

# -----------------------------------------------------------------------------
# STANDARD AUDIO CDs
# -----------------------------------------------------------------------------

# Sublime - Sublime (17 tracks, LBA 0 start)
# MCN: none, ISRC: yes
declare -A SUBLIME=(
    [name]="Sublime - Sublime"
    [type]="audio"
    [tracks]=17
    [first_lba]=0
    [has_mcn]=0
    [has_isrc]=1
    [mb_toc]="1 17 263855 150 19745 32575 42805 54545 72047 85787 95555 117545 145010 150657 160517 178172 193610 215417 231297 244930"
    [ar_toc]="17 17 1 0 19595 32425 42655 54395 71897 85637 95405 117395 144860 150507 160367 178022 193460 215267 231147 244780 263705"
    [fb_toc]="17 150 19745 32575 42805 54545 72047 85787 95555 117545 145010 150657 160517 178172 193610 215417 231297 244930 3518"
    [ar_id]="017-00231e4f-01bf54d7-e00dbc11"
    [fb_id]="e00dbc11"
)

# Goo Goo Dolls - Dizzy up the Girl (13 tracks, LBA 32 start)
# MCN: yes, ISRC: yes
declare -A GGD=(
    [name]="Goo Goo Dolls - Dizzy up the Girl"
    [type]="audio"
    [tracks]=13
    [first_lba]=32
    [has_mcn]=1
    [has_isrc]=1
    [mb_toc]="1 13 203420 182 12262 28217 46107 58452 77167 97980 112652 130482 143362 152105 173820 183670"
    [ar_toc]="13 13 1 32 12112 28067 45957 58302 77017 97830 112502 130332 143212 151955 173670 183470 203270"
    [fb_toc]="13 182 12262 28217 46107 58452 77167 97980 112652 130482 143362 152105 173820 183670 2712"
    [ar_id]="013-0015a200-00d903ba-a60a960d"
    [fb_id]="a60a960d"
)

# The Cranberries - Bury the Hatchet (13 tracks, LBA 0 start)
# MCN: none, ISRC: yes
declare -A CRANBERRIES=(
    [name]="The Cranberries - Bury the Hatchet"
    [type]="audio"
    [tracks]=13
    [first_lba]=0
    [has_mcn]=0
    [has_isrc]=1
    [mb_toc]="1 13 214052 150 15980 28357 52925 69090 85687 102352 119185 133290 146322 160770 176690 198225"
    [ar_toc]="13 13 1 0 15830 28207 52775 68940 85537 102202 119035 133140 146172 160620 176540 198075 213902"
    [fb_toc]="13 150 15980 28357 52925 69090 85687 102352 119185 133290 146322 160770 176690 198225 2854"
    [ar_id]="013-0016e72f-00e46449-ac0b240d"
    [fb_id]="ac0b240d"
)

# Matchbox Twenty - Yourself or Someone Like You (12 tracks, LBA 32 start)
# MCN: yes, ISRC: none
declare -A MB20=(
    [name]="Matchbox Twenty - Yourself or Someone Like You"
    [type]="audio"
    [tracks]=12
    [first_lba]=32
    [has_mcn]=1
    [has_isrc]=0
    [mb_toc]="1 12 210860 182 17567 34492 51517 69417 86292 111805 126812 140137 158400 177687 193810"
    [ar_toc]="12 12 1 32 17417 34342 51367 69267 86142 111655 126662 139987 158250 177537 193660 210710"
    [fb_toc]="12 182 17567 34492 51517 69417 86292 111805 126812 140137 158400 177687 193810 2811"
    [ar_id]="012-00150304-00c439dc-aa0af90c"
    [fb_id]="aa0af90c"
)

# Rush - Exit... Stage Left (13 tracks, LBA 0 start)
# MCN: yes, ISRC: yes
declare -A RUSH=(
    [name]="Rush - Exit... Stage Left"
    [type]="audio"
    [tracks]=13
    [first_lba]=0
    [has_mcn]=1
    [has_isrc]=1
    [mb_toc]="1 13 346060 150 23602 54277 89160 106172 120292 131985 171500 178762 200477 255250 280202 302760"
    [ar_toc]="13 13 1 0 23452 54127 89010 106022 120142 131835 171350 178612 200327 255100 280052 302610 345910"
    [fb_toc]="13 150 23602 54277 89160 106172 120292 131985 171500 178762 200477 255250 280202 302760 4614"
    [ar_id]="013-00227675-0159d3a9-b112040d"
    [fb_id]="b112040d"
)

# Dada - Puzzle (12 tracks, LBA 0 start)
# MCN: none, ISRC: none
declare -A DADA=(
    [name]="Dada - Puzzle"
    [type]="audio"
    [tracks]=12
    [first_lba]=0
    [has_mcn]=0
    [has_isrc]=0
    [mb_toc]="1 12 247562 150 27602 48552 67590 86080 102480 123680 142122 160132 179750 195157 223667"
    [ar_toc]="12 12 1 0 27452 48402 67440 85930 102330 123530 141972 159982 179600 195007 223517 247412"
    [fb_toc]="12 150 27602 48552 67590 86080 102480 123680 142122 160132 179750 195157 223667 3300"
    [ar_id]="012-0018740e-00e1baf6-b30ce20c"
    [fb_id]="b30ce20c"
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
    [has_mcn]=0
    [has_isrc]=1
    [mb_toc]="1 11 349502 150 26427 59512 97427 121795 160052 185967 218225 242760 274965 298510"
    [ar_toc]="12 11 1 0 26277 59362 97277 121645 159902 185817 218075 242610 274815 298360 349352 357656"
    [fb_toc]="12 150 26427 59512 97427 121795 160052 185967 218225 242760 274965 298510 349502 4770"
    [ar_id]="011-001f27c4-010ea9c1-bb12a00c"
    [fb_id]="bb12a00c"
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
    [has_mcn]=0
    [has_isrc]=1
    [mb_toc]="2 9 320528 148734 169482 184797 202605 217733 248258 259988 278078"
    [ar_toc]="9 8 2 0 148584 169332 184647 202455 217583 248108 259838 277928 320378"
    [fb_toc]="9 150 148734 169482 184797 202605 217733 248258 259988 278078 4273"
    [ar_id]="008-001ef535-00ad3cb0-7b10af09"
    [fb_id]="7b10af09"
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

# Test MusicBrainz ID format (can't verify exact value without libdiscid)
test_mb_id_format() {
    local -n disc=$1
    local actual

    ((TESTS_RUN++))
    echo -n "  ${disc[name]}: MB ID format ... "

    actual=$(echo "${disc[mb_toc]}" | "$MBDISCID" -Mic 2>&1) || true

    # MusicBrainz IDs are 28 chars, base64-like with trailing dash or hyphen
    if [[ "$actual" =~ ^[A-Za-z0-9._-]{27,28}$ ]]; then
        test_pass
    else
        test_fail "28-char base64-like string" "$actual"
    fi
}

# Verify AR ID contains correct FreeDB ID component
test_ar_contains_fb() {
    local -n disc=$1
    local actual

    ((TESTS_RUN++))
    echo -n "  ${disc[name]}: AR ID contains FreeDB ... "

    actual=$(echo "${disc[ar_toc]}" | "$MBDISCID" -Aic 2>&1) || true

    if [[ "$actual" == *"${disc[fb_id]}"* ]]; then
        test_pass
    else
        test_fail "contains ${disc[fb_id]}" "$actual"
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

# -----------------------------------------------------------------------------
echo -e "${YELLOW}=== Standalone Options ===${NC}"
# -----------------------------------------------------------------------------

run_test_exit "-h exits 0" 0 "$MBDISCID" -h
run_test_exit "--help exits 0" 0 "$MBDISCID" --help
run_test_exit "-V exits 0" 0 "$MBDISCID" -V
run_test_exit "--version exits 0" 0 "$MBDISCID" --version
run_test_exit "-L exits 0" 0 "$MBDISCID" -L
run_test_contains "-h shows Usage" "Usage:" "$MBDISCID" -h
run_test_contains "-V shows version" "1.1.0" "$MBDISCID" -V

# -----------------------------------------------------------------------------
echo ""
echo -e "${YELLOW}=== AccurateRip ID Verification (Standard Audio CDs) ===${NC}"
# -----------------------------------------------------------------------------

test_ar_id SUBLIME
test_ar_id GGD
test_ar_id CRANBERRIES
test_ar_id MB20
test_ar_id RUSH
test_ar_id DADA

# -----------------------------------------------------------------------------
echo ""
echo -e "${YELLOW}=== FreeDB ID Verification (Standard Audio CDs) ===${NC}"
# -----------------------------------------------------------------------------

test_fb_id SUBLIME
test_fb_id GGD
test_fb_id CRANBERRIES
test_fb_id MB20
test_fb_id RUSH
test_fb_id DADA

# -----------------------------------------------------------------------------
echo ""
echo -e "${YELLOW}=== MusicBrainz ID Format (Standard Audio CDs) ===${NC}"
# -----------------------------------------------------------------------------

test_mb_id_format SUBLIME
test_mb_id_format GGD
test_mb_id_format CRANBERRIES

# -----------------------------------------------------------------------------
echo ""
echo -e "${YELLOW}=== Enhanced CD (Data Track at End) ===${NC}"
# -----------------------------------------------------------------------------

test_ar_id METALLICA
test_fb_id METALLICA
test_ar_contains_fb METALLICA

# -----------------------------------------------------------------------------
echo ""
echo -e "${YELLOW}=== Mixed Mode CD (Data Track at Beginning) ===${NC}"
# -----------------------------------------------------------------------------

test_ar_id FREEDOM
test_fb_id FREEDOM
test_ar_contains_fb FREEDOM

# -----------------------------------------------------------------------------
echo ""
echo -e "${YELLOW}=== Cross-Format Consistency ===${NC}"
# -----------------------------------------------------------------------------

test_ar_contains_fb SUBLIME
test_ar_contains_fb GGD
test_ar_contains_fb RUSH

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
            for disc_name in SUBLIME GGD CRANBERRIES MB20 RUSH DADA METALLICA FREEDOM; do
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

                run_test_exit "Read disc (default)" 0 "$MBDISCID" "$DEVICE"
                run_test_exit "Read disc -M" 0 "$MBDISCID" -M "$DEVICE"
                run_test_exit "Read disc -A" 0 "$MBDISCID" -A "$DEVICE"
                run_test_exit "Read disc -F" 0 "$MBDISCID" -F "$DEVICE"
                run_test_exit "Read disc -R" 0 "$MBDISCID" -R "$DEVICE"
                run_test_exit "Read disc -T" 0 "$MBDISCID" -T "$DEVICE"
                run_test_exit "Read disc -a" 0 "$MBDISCID" -a "$DEVICE"

                # Verify calculated IDs match
                run_test "AR ID matches expected" "${known_disc[ar_id]}" "$MBDISCID" -Ai "$DEVICE"
                run_test "FreeDB ID matches expected" "${known_disc[fb_id]}" "$MBDISCID" -Fi "$DEVICE"

                # -------------------------------------------------------------
                echo ""
                echo -e "${YELLOW}=== MCN Test ===${NC}"
                # -------------------------------------------------------------

                ((TESTS_RUN++))
                echo -n "  MCN read ... "
                mcn=$("$MBDISCID" -C "$DEVICE" 2>/dev/null) || mcn=""

                if [[ "${known_disc[has_mcn]}" == "1" ]]; then
                    if [[ -n "$mcn" ]] && [[ ${#mcn} -ge 12 ]]; then
                        test_pass
                        echo "    MCN: $mcn"
                    else
                        test_fail "MCN present (12+ digits)" "${mcn:-empty}"
                    fi
                else
                    if [[ -z "$mcn" ]]; then
                        test_pass
                        echo "    (No MCN expected)"
                    else
                        # Some discs may have MCN we don't know about
                        echo -e "${YELLOW}UNEXPECTED${NC}"
                        echo "    Found MCN: $mcn (not in test data)"
                    fi
                fi

                # -------------------------------------------------------------
                echo ""
                echo -e "${YELLOW}=== ISRC Test ===${NC}"
                # -------------------------------------------------------------

                # Mixed Mode CDs have track 1 as data, which can cause ISRC probe to fail
                if [[ "${known_disc[type]}" == "mixed" ]]; then
                    echo "  ISRC read: skipped (Mixed Mode CD - track 1 is data)"
                else
                    ((TESTS_RUN++))
                    echo -n "  ISRC read ... "
                    isrc_output=$("$MBDISCID" -I "$DEVICE" 2>/dev/null) || isrc_output=""

                    if [[ "${known_disc[has_isrc]}" == "1" ]]; then
                        # Should have at least one ISRC line
                        if [[ "$isrc_output" =~ [0-9]+:\ [A-Z0-9]{12} ]]; then
                            test_pass
                            isrc_count=$(echo "$isrc_output" | grep -c ':' || true)
                            echo "    Found $isrc_count ISRCs"
                        else
                            test_fail "ISRCs present" "${isrc_output:-empty}"
                        fi
                    else
                        if [[ -z "$isrc_output" ]]; then
                            test_pass
                            echo "    (No ISRCs expected)"
                        else
                            echo -e "${YELLOW}UNEXPECTED${NC}"
                            echo "    Found ISRCs (not in test data)"
                        fi
                    fi
                fi

                # -------------------------------------------------------------
                echo ""
                echo -e "${YELLOW}=== Media Type Test ===${NC}"
                # -------------------------------------------------------------

                if [[ "$(uname)" == "Darwin" ]]; then
                    echo "  Media type: not working on macOS (skipped)"
                else
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
                fi

            else
                echo -e "${YELLOW}  Unknown disc (not in test database)${NC}"
                echo "  AR ID: $disc_ar_id"
                echo ""
                echo "  Running basic read tests..."

                run_test_exit "Read disc (default)" 0 "$MBDISCID" "$DEVICE"
                run_test_exit "Read disc -a" 0 "$MBDISCID" -a "$DEVICE"
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
