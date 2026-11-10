CC ?= gcc
AR ?= ar
RANLIB ?= ranlib

CFLAGS ?= -O2 -Wall -Wextra
CFLAGS += -Iinclude

TARGET = libmp4.a
OBJS = source/libmp4.o \
	source/libmp4_writer.o \
	source/libmp4_reader.o \
	source/libmp4_h264.o \
	source/libmp4_h265.o \
	source/libmp4_g711.o
CHECK_CODEC_BIN = verify/codec_config
CHECK_READER_BIN = verify/reader_limits

.PHONY: all clean check

all: $(TARGET)

$(TARGET): $(OBJS)
	$(AR) rcs $@ $(OBJS)
	$(RANLIB) $@

check: $(CHECK_CODEC_BIN) $(CHECK_READER_BIN)
	./$(CHECK_CODEC_BIN)
	./$(CHECK_READER_BIN)



$(CHECK_CODEC_BIN): verify/codec_config.c $(TARGET)
	$(CC) $(CFLAGS) $< $(TARGET) -o $@

$(CHECK_READER_BIN): verify/reader_limits.c $(TARGET)
	$(CC) $(CFLAGS) $< $(TARGET) -o $@

source/%.o: source/%.c include/libmp4.h source/libmp4_internal.h
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(TARGET) $(OBJS) $(CHECK_CODEC_BIN) $(CHECK_READER_BIN)
