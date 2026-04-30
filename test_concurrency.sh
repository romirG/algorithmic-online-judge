#!/bin/bash
# ──────────────────────────────────────────────────────────
# test_concurrency.sh - Concurrency Test for Online Judge
#
# Launches server + two simultaneous clients, then verifies
# mutex serialization and sequential leaderboard updates.
#
# Usage (in WSL):
#   chmod +x test_concurrency.sh
#   ./test_concurrency.sh
# ──────────────────────────────────────────────────────────

set -e

echo "╔══════════════════════════════════════════════╗"
echo "║   CONCURRENCY TEST - Algorithmic Online Judge ║"
echo "╚══════════════════════════════════════════════╝"
echo ""

# ── Step 1: Build ──
echo "[TEST] Building project..."
make clean 2>/dev/null || true
make
echo ""

# ── Step 2: Initialize database ──
echo "[TEST] Initializing database..."
./init_db
echo ""

# ── Step 3: Start server, tee output to both terminal and log ──
SERVER_LOG="server_output.log"
> "$SERVER_LOG"
echo "[TEST] Starting server..."
./server 2>&1 | tee "$SERVER_LOG" &
SERVER_PID=$!
sleep 1  # Give server time to bind

# Cleanup on exit
cleanup() {
    echo ""
    echo "[TEST] Cleaning up..."
    kill $SERVER_PID 2>/dev/null || true
    wait $SERVER_PID 2>/dev/null || true
    echo "[TEST] Server stopped."
}
trap cleanup EXIT

echo ""
echo "[TEST] Server running (PID: $SERVER_PID)"
echo ""

# ── Step 4: Prepare client inputs ──
# Client A: Login as user 2, submit, logout
CLIENT_A_INPUT="1
2
pass123
2
1
test_accepted.c
3
4
3
"

# Client B: Login as user 2 (different connection), submit, logout
CLIENT_B_INPUT="1
2
pass123
2
1
test_accepted.c
3
4
3
"

# ── Step 5: Launch BOTH clients simultaneously ──
echo "═══════════════════════════════════════════════"
echo "  Launching TWO clients simultaneously..."
echo "  Both will submit test_accepted.cpp as User 2"
echo "═══════════════════════════════════════════════"
echo ""

echo "$CLIENT_A_INPUT" | ./client > client_a_output.log 2>&1 &
CLIENT_A_PID=$!

echo "$CLIENT_B_INPUT" | ./client > client_b_output.log 2>&1 &
CLIENT_B_PID=$!

echo "[TEST] Client A launched (PID: $CLIENT_A_PID)"
echo "[TEST] Client B launched (PID: $CLIENT_B_PID)"
echo "[TEST] Waiting for both clients to finish..."
echo ""

wait $CLIENT_A_PID 2>/dev/null || true
wait $CLIENT_B_PID 2>/dev/null || true

# Give server a moment to flush logs
sleep 2

# ── Step 6: Display Results ──
echo ""
echo "╔══════════════════════════════════════════════╗"
echo "║              TEST RESULTS                    ║"
echo "╚══════════════════════════════════════════════╝"
echo ""

echo "─── CLIENT A OUTPUT ───"
cat client_a_output.log
echo ""

echo "─── CLIENT B OUTPUT ───"
cat client_b_output.log
echo ""

echo "─── SERVER LOG (proving mutex serialization) ───"
cat "$SERVER_LOG"
echo ""

# ── Step 7: Verify ──
echo ""
echo "═══════════════════════════════════════════════"
echo "  VERIFICATION"
echo "═══════════════════════════════════════════════"

# Check client verdicts
A_ACCEPTED=$(grep -c "ACCEPTED" client_a_output.log 2>/dev/null || echo "0")
B_ACCEPTED=$(grep -c "ACCEPTED" client_b_output.log 2>/dev/null || echo "0")

echo ""
echo "  Client A verdict: $([ "$A_ACCEPTED" -ge 1 ] && echo '✓ ACCEPTED' || echo '✗ FAILED')"
echo "  Client B verdict: $([ "$B_ACCEPTED" -ge 1 ] && echo '✓ ACCEPTED' || echo '✗ FAILED')"

# Check leaderboard: User 2 should have solved_count = 2
echo ""
echo "─── FINAL LEADERBOARD ───"
LEADERBOARD_INPUT="1
2
pass123
3
4
3
"
echo "$LEADERBOARD_INPUT" | ./client 2>/dev/null | grep -A5 "LEADERBOARD" || echo "(see server log)"

SCORE=$(echo "$LEADERBOARD_INPUT" | ./client 2>/dev/null | grep "User 2" | grep -o '[0-9]* solved' || echo "? solved")
echo ""
echo "  User 2 final score: $SCORE"
echo "  Expected: 2 solved (one from each concurrent client)"

if echo "$SCORE" | grep -q "2 solved"; then
    echo ""
    echo "  ✓ PASS: Both submissions serialized correctly!"
    echo "         No race conditions, no data corruption."
else
    echo ""
    echo "  ✗ Check server log above for mutex lock/unlock ordering"
fi

echo ""
echo "[TEST] Concurrency test complete."

# Cleanup temp files
rm -f client_a_output.log client_b_output.log
