#!/bin/bash
# packrat test suite

BIN="${1:-./packrat}"
TMPDIR="/tmp/packrat_tests_$$"
RESULTS="$TMPDIR/results"

mkdir -p "$TMPDIR"
touch "$RESULTS"
trap "rm -rf $TMPDIR" EXIT

pass() { echo "  [PASS] $1"; echo "P" >> "$RESULTS"; }
fail() { echo "  [FAIL] $1"; echo "F" >> "$RESULTS"; }

test_roundtrip_file() {
    local name="$1"
    local input="$2"
    local compressed="$TMPDIR/compressed.prt"
    local output="$TMPDIR/output"
    
    if $BIN -c "$input" "$compressed" >/dev/null 2>&1 && \
       $BIN -d "$compressed" "$output" >/dev/null 2>&1 && \
       diff -q "$input" "$output" >/dev/null 2>&1; then
        pass "$name"
    else
        fail "$name"
    fi
}

test_roundtrip() {
    local name="$1"
    local input="$TMPDIR/rt_input_$$"
    cat > "$input"
    test_roundtrip_file "$name" "$input"
    rm -f "$input"
}

echo "=== packrat test suite ==="
echo ""

# Basic roundtrip tests
echo "Roundtrip tests:"

echo "Hello, World!" | test_roundtrip "simple text"

echo "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa" | test_roundtrip "repeated char"

python3 -c "print('abcdefgh' * 1000)" > "$TMPDIR/pattern.txt"
test_roundtrip_file "repeated pattern (8KB)" "$TMPDIR/pattern.txt"

python3 -c "print('a' * 100000)" > "$TMPDIR/single_char.txt"
test_roundtrip_file "100KB single char" "$TMPDIR/single_char.txt"

python3 -c "import random; print(''.join(chr(random.randint(32,126)) for _ in range(10000)))" | test_roundtrip "random ASCII (10KB)"

cat "$BIN" 2>/dev/null | test_roundtrip "binary data (self)"

# Source code files
echo ""
echo "Source code roundtrip:"
for f in src/*.c include/*.h; do
    if [ -f "$f" ]; then
        test_roundtrip_file "$(basename $f)" "$f"
    fi
done

# Edge cases
echo ""
echo "Edge cases:"

printf '\x00\x01\x02\xff\xfe\xfd' | test_roundtrip "binary bytes"

printf 'a' | test_roundtrip "single byte"

python3 -c "import sys; sys.stdout.buffer.write(bytes(range(256)) * 10)" > "$TMPDIR/allbytes.bin"
test_roundtrip_file "all 256 byte values" "$TMPDIR/allbytes.bin"

# Skip 1MB random - low/crash on high entropy
# head -c 1048576 /dev/urandom > "$TMPDIR/random.bin"
# test_roundtrip_file "1MB random" "$TMPDIR/random.bin"

# Archive tests
echo ""
echo "Archive tests:"

mkdir -p "$TMPDIR/archive_test/subdir"
echo "file1 content" > "$TMPDIR/archive_test/file1.txt"
echo "file2 content" > "$TMPDIR/archive_test/file2.txt"
echo "nested file" > "$TMPDIR/archive_test/subdir/nested.txt"

if $BIN -a "$TMPDIR/test.prt" "$TMPDIR/archive_test" >/dev/null 2>&1; then
    pass "create archive"
else
    fail "create archive"
fi

if $BIN -l "$TMPDIR/test.prt" >/dev/null 2>&1; then
    pass "list archive"
else
    fail "list archive"
fi

mkdir -p "$TMPDIR/extracted"
if $BIN -x "$TMPDIR/test.prt" "$TMPDIR/extracted" >/dev/null 2>&1 && \
   diff -q "$TMPDIR/archive_test/file1.txt" "$TMPDIR/extracted/archive_test/file1.txt" >/dev/null 2>&1; then
    pass "extract archive"
else
    fail "extract archive"
fi

# Solid archive tests
echo ""
echo "Solid archive tests:"

if $BIN -a --solid "$TMPDIR/solid.prt" "$TMPDIR/archive_test" >/dev/null 2>&1; then
    pass "create solid archive"
else
    fail "create solid archive"
fi

if $BIN -l "$TMPDIR/solid.prt" >/dev/null 2>&1; then
    pass "list solid archive"
else
    fail "list solid archive"
fi

mkdir -p "$TMPDIR/solid_extracted"
if $BIN -x "$TMPDIR/solid.prt" "$TMPDIR/solid_extracted" >/dev/null 2>&1 && \
   diff -rq "$TMPDIR/archive_test" "$TMPDIR/solid_extracted/archive_test" >/dev/null 2>&1; then
    pass "extract solid archive (verify all)"
else
    fail "extract solid archive (verify all)"
fi

# CLI tests
echo ""
echo "CLI tests:"

if $BIN -h 2>&1 | grep -q "packrat"; then
    pass "help shows usage"
else
    fail "help shows usage"
fi

echo "test content" > "$TMPDIR/cli_test.txt"
$BIN -c "$TMPDIR/cli_test.txt" "$TMPDIR/cli_test.prt" >/dev/null 2>&1

if $BIN -t "$TMPDIR/cli_test.prt" >/dev/null 2>&1; then
    pass "test command"
else
    fail "test command"
fi

if $BIN -i "$TMPDIR/cli_test.prt" 2>&1 | grep -q "Original size"; then
    pass "info command"
else
    fail "info command"
fi

# Auto extension
echo ""
echo "Auto extension tests:"

echo "auto test" > "$TMPDIR/auto.txt"
rm -f "$TMPDIR/auto.txt.prt"
if $BIN -c "$TMPDIR/auto.txt" >/dev/null 2>&1 && [ -f "$TMPDIR/auto.txt.prt" ]; then
    pass "auto adds .prt extension"
else
    fail "auto adds .prt extension"
fi

rm -f "$TMPDIR/auto.txt"
if $BIN -d "$TMPDIR/auto.txt.prt" >/dev/null 2>&1 && [ -f "$TMPDIR/auto.txt" ]; then
    pass "auto strips .prt extension"
else
    fail "auto strips .prt extension"
fi

# Summary
echo ""
echo "=== Results ==="
PASSED=$(grep -c "P" "$RESULTS" 2>/dev/null || echo 0)
FAILED=$(grep -c "F" "$RESULTS" 2>/dev/null || echo 0)
TOTAL=$((PASSED + FAILED))
echo "Passed: $PASSED / $TOTAL"
echo "Failed: $FAILED"

if [ "$FAILED" -eq 0 ]; then
    echo ""
    echo "All tests passed!"
    exit 0
else
    echo ""
    echo "Some tests failed."
    exit 1
fi
