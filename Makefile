# ==========================================
# Standalone STM32F429ZI Makefile (C/C++ Mixed)
# ==========================================

PROJECT = my_lcd_firmware

# Setup libopencm3 dependency local to project
OPENCM3_DIR = libopencm3

# --- 1. SEPARATE C AND C++ SOURCES ---
# libopencm3 drivers and your standard C files
C_SOURCES = clock/clock.c console/console.c font/font-7x12.c \
            gfx/gfx.c lcd_driver/lcd-spi.c sdram/sdram.c usart/usart.c \
            touch/touch.c

# Your application code. The MNIST dense model is compiled from
# mnist_weights.h, so TensorFlow Lite Micro is not linked for this target.
CXX_SOURCES = main.cpp



# Combine object files
OBJS = $(C_SOURCES:.c=.o) $(CXX_SOURCES:.cpp=.o)

PREFIX = arm-none-eabi-
CC = $(PREFIX)gcc
CXX = $(PREFIX)g++
OBJCOPY = $(PREFIX)objcopy
SIZE = $(PREFIX)size

INCLUDES = -I$(OPENCM3_DIR)/include -Ilcd_driver -Iclock -Iconsole \
           -Igfx -Isdram -Ifont -Iusart -I. \
           -Itflite-micro \
           -Itflite-micro/third_party/flatbuffers/include \
           -Itflite-micro/third_party/gemmlowp \
           -Itflite-micro/third_party/ruy

# --- 2. COMPILER FLAGS ---
# Common flags for both C and C++
COMMON_FLAGS = -Os -g3 -Wall -Wextra $(INCLUDES) \
               -mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16 -DSTM32F4 \
               -fdata-sections -ffunction-sections \
               -DTF_LITE_STATIC_MEMORY -DNDEBUG

# C-specific flags
CFLAGS = $(COMMON_FLAGS)

# C++-specific flags (Crucial for TinyML on bare-metal)
# C++-specific flags (Using GNU++ to support C99 Flexible Array Members)
CXXFLAGS = $(COMMON_FLAGS) -std=gnu++14 -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-unwind-tables

LDSCRIPT = stm32f429zi.ld

# --- 3. LINKER FLAGS ---
LDFLAGS = -L$(OPENCM3_DIR)/lib -T$(LDSCRIPT) -nostartfiles \
          -mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16 \
          -specs=nano.specs -specs=nosys.specs -Wl,--gc-sections

LDLIBS = -lopencm3_stm32f4 -lm

# ==========================================
# Build Targets
# ==========================================

all: $(OPENCM3_DIR)/lib/libopencm3_stm32f4.a $(PROJECT).elf $(PROJECT).bin size

$(OPENCM3_DIR)/Makefile:
	git submodule update --init

$(OPENCM3_DIR)/lib/libopencm3_stm32f4.a: $(OPENCM3_DIR)/Makefile
	$(MAKE) -C $(OPENCM3_DIR) TARGETS=stm32/f4

$(OBJS): $(OPENCM3_DIR)/lib/libopencm3_stm32f4.a

# Rule for C files
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Rule for C++ files
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

# --- 4. USE G++ FOR LINKING ---
$(PROJECT).elf: $(OBJS) $(LDSCRIPT)
	$(CXX) $(OBJS) $(LDFLAGS) $(LDLIBS) -o $@

$(PROJECT).bin: $(PROJECT).elf
	$(OBJCOPY) -O binary $< $@

size: $(PROJECT).elf
	$(SIZE) $<

clean:
	rm -f *.elf *.bin *.o lcd_driver/*.o clock/*.o console/*.o gfx/*.o sdram/*.o font/*.o usart/*.o touch/*.o

clobber: clean
	rm -rf $(OPENCM3_DIR)

flash: $(PROJECT).elf
	openocd -f board/stm32f429discovery.cfg -c "program $(PROJECT).elf verify reset exit"

.PHONY: all clean clobber flash size
