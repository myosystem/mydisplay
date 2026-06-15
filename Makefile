# ==============================================================
# mydisplay — display 서브시스템 (user-space .efi)
# ==============================================================
# Usage:
#   make              → Debug 빌드  → bin/x64/Debug/mydisplay.efi
#                                   → ../_bootpartition/.../display.o
#   make CONFIG=Release
#   make clean
# ==============================================================

CONFIG   ?= Debug
PLATFORM := x64

CXX     := g++
LD      := g++
OBJCOPY := objcopy

CXXFLAGS := \
  -m64 -std=c++20 -masm=intel \
  -ffreestanding -fno-rtti -fno-exceptions \
  -fno-zero-initialized-in-bss -fno-common \
  -mno-sse -mno-sse2 -mno-mmx -mno-3dnow -mno-80387 \
  -msoft-float -mno-red-zone -g

ifeq ($(CONFIG),Release)
  CXXFLAGS += -O2
else
  CXXFLAGS += -O0
endif

LIBC     := ../mylibc/bin/$(PLATFORM)/$(CONFIG)/libc.a
INCLUDES := -I../mylibc/include

LDFLAGS  := -nostdlib -T linker.ld

OUTDIR   := bin/$(PLATFORM)/$(CONFIG)
TARGET   := $(OUTDIR)/mydisplay.efi
BOOT_OUT := ../_bootpartition/$(PLATFORM)/$(CONFIG)/display.o

SRCS := $(wildcard *.cpp)
OBJS := $(addprefix $(OUTDIR)/,$(SRCS:.cpp=.o))

# ---------------------------------------------------------------

.PHONY: all clean

all: $(TARGET)
	@mkdir -p $(dir $(BOOT_OUT))
	$(OBJCOPY) -O binary \
	  --only-section=.text --only-section=.rodata --only-section=.data \
	  $(TARGET) $(BOOT_OUT)
	@echo "[mydisplay] done → $(TARGET)"
	@echo "[mydisplay] objcopy → $(BOOT_OUT)"

$(TARGET): $(OBJS) $(LIBC)
	@mkdir -p $(OUTDIR)
	$(LD) $(LDFLAGS) -o $@ $(OBJS) $(LIBC)

$(OUTDIR)/%.o: %.cpp
	@mkdir -p $(OUTDIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

clean:
	rm -rf bin
