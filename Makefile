# Makefile — HP LaserJet 1022 macOS 27 Native ARM64 Driver Suite (v3.1)
CC       = clang
CFLAGS   = -O3 -Wall -Wextra -Wno-deprecated-declarations -arch arm64 -arch x86_64 $(shell cups-config --cflags 2>/dev/null)
LDFLAGS  = $(shell cups-config --image --libs 2>/dev/null || echo "-lcups -lcupsimage")
TARGET   = rastertozj1022
PPD      = HP-LaserJet-1022-macOS27.ppd

.PHONY: all clean sign test install

all: $(TARGET) sign

$(TARGET): rastertozj1022.c
	$(CC) $(CFLAGS) rastertozj1022.c $(LDFLAGS) -o $(TARGET)

sign: $(TARGET)
	xattr -cr $(TARGET)
	codesign --force --sign - --timestamp=none $(TARGET)

test: $(TARGET)
	./$(TARGET) --self-test > selftest_1022.pcl
	@ls -lh selftest_1022.pcl

install: all
	chmod +x install-lj1022-macos27.sh
	./install-lj1022-macos27.sh

clean:
	rm -f $(TARGET) selftest_1022.pcl

