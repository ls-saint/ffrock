BASECFLAGS=
CC=$(cc)
ar=$(AR)

ifeq ($(strip $(CC)),)
CC=gcc
endif

ifeq ($(strip $(ar)),)
ar=ar
endif

BASEDIR=$(BaseDir) -I./include -I./include/socket
LDFLAGS=$(BaseLibDir)

CFLAGS=$(CompileOpion) $(BASECFLAGS) $(BASEDIR)

SRCS = ./src/mibase.o \
	./src/Ini/ini.o \
	./src/miatomic.o \
	./src/mimutex.o \
	./src/misemaphore.o \
	./src/mithread.o \
	./src/mithreadpool.o \
	./src/mitimer.o \
	./src/socket/miaddr.o \
	./src/socket/mimdns.o \
	./src/socket/miudpskt.o

TARGET= libMiBase.a
.PHONY: all clean

all:$(TARGET)

$(TARGET): $(SRCS)
	$(ar) rcs $(TARGET) $(SRCS)
	mkdir -p ../output/$(DevType)$(CpuType)lib/
	cp -f $(TARGET) ../output/$(DevType)$(CpuType)lib/
	@echo '$(TARGET) done'

$(SRCS):%o:%c
	$(CC) $(CFLAGS) $(LDFLAGS) $(DefHideFun) -ffunction-sections -fdata-sections -Wl,--gc-sections -c $< -o $@

clean:
	rm -f $(TARGET) $(SRCS) ../output/$(DevType)$(CpuType)lib/$(TARGET)
