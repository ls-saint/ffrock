CC=$(cc)
strip=$(STRIP)

ifeq ($(strip $(CC)),)
CC=gcc
endif

ifeq ($(strip $(strip)),)
strip=strip
endif

OUT_LIB=../output/$(DevType)$(CpuType)lib
OUT_BIN=../output/$(DevType)$(CpuType)bin

BASEDIR=$(BaseDir) \
	-I. \
	-I../MiIpc \
	-I../MiIpc/Soc \
	-I../MiBase/include \
	-I../MiBase/include/socket

MIIPC_APP_DEFS=-DNDEBUG
LDFLAGS=$(BaseLibDir) -L$(OUT_LIB) -Wl,-rpath-link,$(OUT_LIB) -Wl,-rpath,'$$ORIGIN' -Wl,-rpath,'$$ORIGIN/../lib'
CFLAGS=$(AppExtFlg) $(BASECFLAGS) $(MIIPC_APP_DEFS) $(BASEDIR)

TARGET_APP=APP
APP_CSRCS=APP.o

.PHONY: all clean

all: $(TARGET_APP)

$(TARGET_APP): $(APP_CSRCS)
	$(CC) $(CFLAGS) $(PdbOpion) -o $(TARGET_APP) $(APP_CSRCS) $(LDFLAGS) -lMiIpc -lpthread -ldl -lm
	$(strip) --strip-unneeded $(TARGET_APP)
	mkdir -p $(OUT_BIN)
	cp -f $(TARGET_APP) $(OUT_BIN)/
	cp -f $(TARGET_APP) $(OUT_BIN)/MiIpc
	@if [ -f "$(OUT_LIB)/libMiIpc.so" ]; then cp -f "$(OUT_LIB)/libMiIpc.so" "$(OUT_BIN)/"; fi
	@if [ -f "$(OUT_LIB)/libMiRtc.so" ]; then cp -f "$(OUT_LIB)/libMiRtc.so" "$(OUT_BIN)/"; fi
	@if [ -f "Cfg.ini" ]; then cp -f "Cfg.ini" "$(OUT_BIN)/"; fi
	@if [ -f "app.ini" ]; then cp -f "app.ini" "$(OUT_BIN)/"; fi
	@echo '$(TARGET_APP) done'

$(APP_CSRCS): %.o: %.c
	$(CC) $(CFLAGS) -fvisibility=hidden -ffunction-sections -fdata-sections -Wl,--gc-sections -c $< -o $@

clean:
	rm -f $(TARGET_APP) $(APP_CSRCS) $(OUT_BIN)/$(TARGET_APP) $(OUT_BIN)/MiIpc
