CXX      = clang++
CXXFLAGS = -std=c++17 -O2 -Wall -Wextra \
           -I/opt/homebrew/include \
           -I$(SRC_DIR)
LDFLAGS  = -L/opt/homebrew/lib -ldmtx

CV_FLAGS = $(shell /opt/homebrew/bin/pkg-config --cflags opencv4)
CV_LIBS  = $(shell /opt/homebrew/bin/pkg-config --libs opencv4)

SRC_DIR  = src

.PHONY: all clean test

all: dmgen dmdecode

dmgen: $(SRC_DIR)/dmgen.cpp $(SRC_DIR)/stb_image_write.h
	$(CXX) $(CXXFLAGS) -o $@ $< $(LDFLAGS)

dmdecode: $(SRC_DIR)/dmdecode.cpp
	$(CXX) $(CXXFLAGS) $(CV_FLAGS) -o $@ $< $(LDFLAGS) $(CV_LIBS)

test: dmgen dmdecode
	@mkdir -p test_out
	@echo "==> Encode"
	./dmgen -d "pallet 234" -o test_out/pallet_234.png --max-dim 120
	./dmgen -d "SM3"        -o test_out/sm3.png        --max-dim 120
	./dmgen -d "rack 17"    -o test_out/rack_17.png    --max-dim 120
	@echo "==> Decode (round-trip)"
	./dmdecode test_out/pallet_234.png
	./dmdecode test_out/sm3.png
	./dmdecode test_out/rack_17.png
	@echo "==> Batch decode"
	./dmdecode --dir test_out --ext png

clean:
	rm -f dmgen dmdecode
	rm -rf test_out
