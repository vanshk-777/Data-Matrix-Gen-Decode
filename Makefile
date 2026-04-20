CXX      = clang++
CXXFLAGS = -std=c++17 -O2 -Wall -Wextra \
           -I/opt/homebrew/include \
           -I$(SRC_DIR)
LDFLAGS  = -L/opt/homebrew/lib -ldmtx

SRC_DIR  = src
TARGET   = dmgen
SRC      = $(SRC_DIR)/dmgen.cpp

# ── Targets ────────────────────────────────────────────────────────────────

.PHONY: all clean test

all: $(TARGET)

$(TARGET): $(SRC) $(SRC_DIR)/stb_image_write.h
	$(CXX) $(CXXFLAGS) -o $@ $< $(LDFLAGS)

# Quick smoke-test
test: $(TARGET)
	@mkdir -p test_out
	@echo "==> Single SVG"
	./$(TARGET) -d "HELLO-WORLD" -o test_out/hello.svg
	@echo "==> Single PNG (module=12)"
	./$(TARGET) -d "HELLO-WORLD" -o test_out/hello.png -m 12
	@echo "==> Fixed size 24x24"
	./$(TARGET) -d "FIXED-SIZE" -o test_out/fixed24.svg -s 24x24
	@echo "==> Batch JSON"
	./$(TARGET) --json examples/labels.json --out-dir test_out/json
	@echo "==> Batch CSV"
	./$(TARGET) --csv examples/labels.csv --out-dir test_out/csv --fmt png
	@echo ""
	@echo "Output files:"
	@find test_out -type f | sort

clean:
	rm -f $(TARGET)
	rm -rf test_out
