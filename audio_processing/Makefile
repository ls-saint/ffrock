CC=$(cc)
ar=$(AR)
strip=$(STRIP)

ifeq ($(strip $(CC)),)
CC=gcc
endif

ifeq ($(strip $(ar)),)
ar=ar
endif

ifeq ($(strip $(strip)),)
strip=strip
endif

TARGET=libaudio_processing.a
OUT_LIB_DIR=../output/$(DevType)$(CpuType)lib

ifeq ($(OS),Windows_NT)
OUT_LIB_DIR_NATIVE=$(subst /,\,$(OUT_LIB_DIR))
RM_FILES=cmd /C del /F /Q
MKDIR_OUT=cmd /C if not exist "$(OUT_LIB_DIR_NATIVE)" mkdir "$(OUT_LIB_DIR_NATIVE)"
COPY_TARGET=cmd /C copy /Y "$(TARGET)" "$(OUT_LIB_DIR_NATIVE)\$(TARGET)" >NUL
else
RM_FILES=rm -f
MKDIR_OUT=mkdir -p $(OUT_LIB_DIR)
COPY_TARGET=cp -f $(TARGET) $(OUT_LIB_DIR)/
endif

BASE_DIR= -I. -I./include -I./aecm -I./ns -I./utility -I./signal_processing -I./signal_processing/include
ifeq ($(strip $(CompileOpion)),)
OPT_FLAGS=-O2
endif

RING_BUFFER_RENAME_DEFS= \
	-DWebRtc_CreateBuffer=MiAudio_CreateBuffer \
	-DWebRtc_InitBuffer=MiAudio_InitBuffer \
	-DWebRtc_FreeBuffer=MiAudio_FreeBuffer \
	-DWebRtc_ReadBuffer=MiAudio_ReadBuffer \
	-DWebRtc_WriteBuffer=MiAudio_WriteBuffer \
	-DWebRtc_MoveReadPtr=MiAudio_MoveReadPtr \
	-DWebRtc_available_read=MiAudio_available_read \
	-DWebRtc_available_write=MiAudio_available_write

ENABLE_AECM ?= 1
ENABLE_NS ?= 1
ENABLE_NSX ?= 1

ifeq ($(DevType),ts5110/)
override ENABLE_NS = 0
override ENABLE_NSX = 0
endif

CFLAGS=$(CompileOpion) $(OPT_FLAGS) -std=c99 -DNDEBUG \
	-DMI_AUDIO_PROCESSING_ENABLE_AECM=$(ENABLE_AECM) \
	-DMI_AUDIO_PROCESSING_ENABLE_NS=$(ENABLE_NS) \
	-DMI_AUDIO_PROCESSING_ENABLE_NSX=$(ENABLE_NSX) \
	$(RING_BUFFER_RENAME_DEFS) \
	$(BaseDir) $(BASE_DIR)
LDFLAGS=$(BaseLibDir)

COMMON_SRCS = ./mi_audio_processor.o \
	./System/cpu_features.o \
	./signal_processing/complex_bit_reverse.o \
	./signal_processing/complex_fft.o \
	./signal_processing/copy_set_operations.o \
	./signal_processing/cross_correlation.o \
	./signal_processing/division_operations.o \
	./signal_processing/downsample_fast.o \
	./signal_processing/energy.o \
	./signal_processing/get_scaling_square.o \
	./signal_processing/min_max_operations.o \
	./signal_processing/randomization_functions.o \
	./signal_processing/real_fft.o \
	./signal_processing/spl_init.o \
	./signal_processing/spl_inl.o \
	./signal_processing/spl_sqrt_floor.o \
	./signal_processing/vector_scaling_operations.o

AECM_SRCS = ./ring_buffer.o \
	./utility/delay_estimator.o \
	./utility/delay_estimator_wrapper.o \
	./aecm/aecm_core.o \
	./aecm/aecm_core_c.o \
	./aecm/echo_control_mobile.o

NS_SRCS = ./fft4g.o \
	./ns/noise_suppression.o \
	./ns/ns_core.o

NSX_SRCS = ./ns/noise_suppression_x.o \
	./ns/nsx_core.o \
	./ns/nsx_core_c.o

SRCS = $(COMMON_SRCS)
ifeq ($(ENABLE_AECM),1)
SRCS += $(AECM_SRCS)
endif
ifeq ($(ENABLE_NS),1)
SRCS += $(NS_SRCS)
endif
ifeq ($(ENABLE_NSX),1)
SRCS += $(NSX_SRCS)
endif

ALL_SRCS = $(COMMON_SRCS) $(AECM_SRCS) $(NS_SRCS) $(NSX_SRCS)

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(SRCS)
	$(ar) rcs $(TARGET) $(SRCS)
	$(MKDIR_OUT)
	$(COPY_TARGET)
	@echo '$(TARGET) done'

$(SRCS): %.o: %.c
	$(CC) $(CFLAGS) $(LDFLAGS) $(DefHideFun) -ffunction-sections -fdata-sections -c $< -o $@

clean:
ifeq ($(OS),Windows_NT)
	$(RM_FILES) $(subst /,\,$(TARGET) $(ALL_SRCS) $(OUT_LIB_DIR)/$(TARGET)) 2>NUL || exit 0
else
	$(RM_FILES) $(TARGET) $(ALL_SRCS) $(OUT_LIB_DIR)/$(TARGET)
endif
