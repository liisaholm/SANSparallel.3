#!/usr/bin/env bash
# =============================================================================
# tests/run_test.sh — SANSparallel integration test
#
# Builds a mini suffix array database from a small sample FASTA,
# starts the server, runs the client, parses output, checks for a
# known hit, then cleans up.
#
# Usage:
#   bash tests/run_test.sh [path/to/sansparallel/root]
#
# The optional argument overrides SANS_ROOT (default: parent of this script).
# =============================================================================

set -euo pipefail

# --- paths -------------------------------------------------------------------

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SANS_ROOT="${1:-$(dirname "$SCRIPT_DIR")}"

SA_DIR="$SANS_ROOT/SA"
SERVER_BIN="$SANS_ROOT/server/server"
CLIENT_BIN="$SANS_ROOT/client/client"
XMLPARSER="$SANS_ROOT/scripts/XMLParser.py"

# Test data (committed to tests/)
SAMPLE_FASTA="$SCRIPT_DIR/data/sample.fasta"   # small FASTA for database + query
QUERY_FASTA="$SCRIPT_DIR/data/query.fasta"     # one or two sequences to search

# ---- FILL IN AFTER FIRST SUCCESSFUL RUN ------------------------------------
EXPECTED_HIT="sp|Q6GZX4|001R_FRG3G"   # e.g. "sp|P00533|EGFR_HUMAN"
# ----------------------------------------------------------------------------

# Ephemeral database and server config
TEST_DB_DIR="$SCRIPT_DIR/tmp_db"
TEST_DB="$TEST_DB_DIR/testdb"
TEST_PORT=54399
SERVER_LOG="$TEST_DB_DIR/server.log"
SERVER_PID_FILE="$TEST_DB_DIR/server.pid"

# --- colours -----------------------------------------------------------------

RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; NC='\033[0m'
pass() { echo -e "${GREEN}PASS${NC}  $*"; }
fail() { echo -e "${RED}FAIL${NC}  $*"; exit 1; }
info() { echo -e "${YELLOW}----${NC}  $*"; }

# --- cleanup (always runs) ---------------------------------------------------

cleanup() {
    if [ -f "$SERVER_PID_FILE" ]; then
        local pid
        pid=$(cat "$SERVER_PID_FILE")
        if kill -0 "$pid" 2>/dev/null; then
            info "Stopping server (pid $pid) ..."
            kill "$pid" && wait "$pid" 2>/dev/null || true
        fi
        rm -f "$SERVER_PID_FILE"
    fi
    rm -rf "$TEST_DB_DIR"
}
trap cleanup EXIT

# --- 1. preflight checks -----------------------------------------------------

MPIRUN="${MPIRUN:-/usr/lib64/openmpi/bin/mpirun}"

info "Checking binaries ..."
for bin in "$MPIRUN" "$SERVER_BIN" "$CLIENT_BIN"; do
    [ -x "$bin" ] || fail "Binary not found or not executable: $bin"
done
[ -f "$XMLPARSER" ]    || fail "XMLParser.py not found: $XMLPARSER"
[ -f "$SAMPLE_FASTA" ] || fail "Sample FASTA not found: $SAMPLE_FASTA"
[ -f "$QUERY_FASTA" ]  || fail "Query FASTA not found: $QUERY_FASTA"
pass "Binaries and test data found."

# --- 2. build mini database --------------------------------------------------

info "Building mini suffix array database ..."
mkdir -p "$TEST_DB_DIR"
(
    cd "$SA_DIR"
    perl saisformatdb.pl "$TEST_DB" "$SAMPLE_FASTA"
) || fail "saisformatdb.pl failed."

for ext in psq SAP SRES; do
    [ -f "${TEST_DB}.${ext}" ] || fail "Database file missing: ${TEST_DB}.${ext}"
done
pass "Database built."

# --- 3. start server ---------------------------------------------------------

info "Starting server on port $TEST_PORT (mpirun -np 3) ..."
MPIRUN="${MPIRUN:-/usr/lib64/openmpi/bin/mpirun}"
[ -x "$MPIRUN" ] || fail "mpirun not found: $MPIRUN  (override with MPIRUN=/path/to/mpirun)"

nohup "$MPIRUN" -np 3 -output-filename "$TEST_DB_DIR/mpi" \
    "$SERVER_BIN" "$TEST_DB" "$TEST_PORT" "test.$(date +%Y%m%d)" \
    > "$SERVER_LOG" 2>&1 &
echo $! > "$SERVER_PID_FILE"

# Wait for server to become ready (up to 30 seconds — MPI startup is slower)
READY=0
for i in $(seq 1 30); do
    if bash -c "echo > /dev/tcp/localhost/$TEST_PORT" 2>/dev/null; then
        READY=1; break
    fi
    sleep 1
done
[ "$READY" -eq 1 ] || fail "Server did not start within 30 seconds. See $SERVER_LOG and $TEST_DB_DIR/mpi.*"
pass "Server started (pid $(cat "$SERVER_PID_FILE"))."

# --- 4. run client -----------------------------------------------------------

info "Running client search ..."
RESULT_XML="$TEST_DB_DIR/result.xml"
"$CLIENT_BIN" -H localhost -P "$TEST_PORT" < "$QUERY_FASTA" > "$RESULT_XML" \
    || fail "Client exited with error."

[ -s "$RESULT_XML" ] || fail "Client returned empty output."
pass "Client search completed."

# --- 5. parse XML to TSV -----------------------------------------------------

info "Parsing XML output ..."
RESULT_TSV="$TEST_DB_DIR/result.tsv"
python3 "$XMLPARSER" < "$RESULT_XML" > "$RESULT_TSV" \
    || fail "XMLParser.py failed."

[ -s "$RESULT_TSV" ] || fail "XMLParser.py produced empty output."
pass "XML parsed to TSV."

# --- 6. check for known hit --------------------------------------------------

info "Checking for expected hit: $EXPECTED_HIT ..."
if [ "$EXPECTED_HIT" = "REPLACE_WITH_KNOWN_UNIPROT_ID" ]; then
    echo -e "${YELLOW}SKIP${NC}  EXPECTED_HIT not set — run once and fill in the value."
else
    grep -q "$EXPECTED_HIT" "$RESULT_TSV" \
        || fail "Expected hit '$EXPECTED_HIT' not found in output."
    pass "Expected hit found."
fi

# --- done --------------------------------------------------------------------

echo ""
pass "All tests passed."
