# packrat - Text Compression Tool
# BWT + MTF + RLE + Huffman pipeline

CC = gcc
CFLAGS = -Wall -Wextra -O3 -march=native -I./include
LDFLAGS =

SRCDIR = src
INCDIR = include
OBJDIR = obj

SRC = $(wildcard $(SRCDIR)/*.c)
OBJ = $(SRC:$(SRCDIR)/%.c=$(OBJDIR)/%.o)
BIN = packrat

.PHONY: all clean test bench

all: $(OBJDIR) $(BIN)

$(OBJDIR):
	mkdir -p $(OBJDIR)

$(BIN): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(OBJDIR)/%.o: $(SRCDIR)/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

# Dependencies
$(OBJDIR)/main.o: $(SRCDIR)/main.c $(INCDIR)/packrat.h $(INCDIR)/archive.h $(INCDIR)/archive_v3.h
$(OBJDIR)/packrat.o: $(SRCDIR)/packrat.c $(INCDIR)/packrat.h $(INCDIR)/bwt.h $(INCDIR)/mtf.h $(INCDIR)/rle.h $(INCDIR)/huffman.h
$(OBJDIR)/archive.o: $(SRCDIR)/archive.c $(INCDIR)/archive.h $(INCDIR)/packrat.h
$(OBJDIR)/archive_v3.o: $(SRCDIR)/archive_v3.c $(INCDIR)/archive_v3.h $(INCDIR)/packrat.h
$(OBJDIR)/bwt.o: $(SRCDIR)/bwt.c $(INCDIR)/bwt.h
$(OBJDIR)/mtf.o: $(SRCDIR)/mtf.c $(INCDIR)/mtf.h
$(OBJDIR)/rle.o: $(SRCDIR)/rle.c $(INCDIR)/rle.h
$(OBJDIR)/huffman.o: $(SRCDIR)/huffman.c $(INCDIR)/huffman.h

# Math library needed for log2f in entropy calculation
# Pthread for parallel compression
LDFLAGS = -lm -lpthread

clean:
	rm -rf $(OBJDIR) $(BIN)
	rm -f /tmp/packrat_test*

test: $(BIN)
	@echo "=== packrat Compression Tests ==="
	@echo ""
	@echo "--- Test 1: Simple text ---"
	@echo "The quick brown fox jumps over the lazy dog" > /tmp/packrat_test1.txt
	@./$(BIN) -c /tmp/packrat_test1.txt /tmp/packrat_test1.prt
	@./$(BIN) -d /tmp/packrat_test1.prt /tmp/packrat_test1_restored.txt
	@diff /tmp/packrat_test1.txt /tmp/packrat_test1_restored.txt && echo "PASS: Roundtrip matches"
	@echo ""
	@echo "--- Test 2: Repeated text ---"
	@python3 -c "print('abcdefgh' * 1000)" > /tmp/packrat_test2.txt
	@./$(BIN) -c /tmp/packrat_test2.txt /tmp/packrat_test2.prt
	@./$(BIN) -d /tmp/packrat_test2.prt /tmp/packrat_test2_restored.txt
	@diff /tmp/packrat_test2.txt /tmp/packrat_test2_restored.txt && echo "PASS: Roundtrip matches"
	@echo ""
	@echo "--- Test 3: Source code (this Makefile) ---"
	@./$(BIN) -c Makefile /tmp/packrat_test3.prt
	@./$(BIN) -d /tmp/packrat_test3.prt /tmp/packrat_test3_restored.txt
	@diff Makefile /tmp/packrat_test3_restored.txt && echo "PASS: Roundtrip matches"
	@echo ""
	@echo "=== All tests passed ==="

bench: $(BIN)
	@echo "=== Compression Benchmark ==="
	@echo ""
	@echo "Creating 100KB test file..."
	@python3 -c "import random; print(''.join(random.choices('abcdefghijklmnopqrstuvwxyz ABCDEFGHIJKLMNOPQRSTUVWXYZ\\n.,:;!?0123456789', k=100000)))" > /tmp/packrat_bench.txt
	@echo ""
	@echo "--- packrat ---"
	@./$(BIN) -c /tmp/packrat_bench.txt /tmp/packrat_bench.prt
	@echo ""
	@echo "--- gzip ---"
	@gzip -kf /tmp/packrat_bench.txt
	@ls -la /tmp/packrat_bench.txt.gz | awk '{print "Size: " $$5 " bytes"}'
	@echo ""
	@echo "--- bzip2 ---"
	@bzip2 -kf /tmp/packrat_bench.txt
	@ls -la /tmp/packrat_bench.txt.bz2 | awk '{print "Size: " $$5 " bytes"}'
	@echo ""
	@echo "--- Size comparison ---"
	@ls -la /tmp/packrat_bench.txt /tmp/packrat_bench.prt /tmp/packrat_bench.txt.gz /tmp/packrat_bench.txt.bz2 2>/dev/null | awk '{print $$9 ": " $$5 " bytes"}'
