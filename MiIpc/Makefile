CC=$(cc)
CXX=$(cxx)
strip=$(STRIP)

ifeq ($(strip $(CC)),)
CC=gcc
endif

ifeq ($(strip $(CXX)),)
CXX=g++
endif

ifeq ($(strip $(strip)),)
strip=strip
endif

OUT_LIB=../output/$(DevType)$(CpuType)lib
OUT_BIN=../output/$(DevType)$(CpuType)bin

BASEDIR=$(BaseDir) \
	-I. \
	-I./App \
	-I./Audio \
	-I./Broadcast \
	-I./Config \
	-I./DevMsg \
	-I./Http \
	-I./JSON \
	-I./Log \
	-I./Rtsp \
	-I./SdStorage \
	-I./Signal \
	-I./Soc \
	-I../audio_processing/include \
	-I../libmov/include \
	-I../libmp4/include \
	-I../MiBase/include \
	-I../MiBase/include/socket \
	-I../MiOnvif/include \
	-I../MiRtc/include \
	-I../MiRtc/src \
	-I../MiRtc/src/p2p \
	-I../MiRtc/src/p2p/base \
	-I../MiRtc/src/rtp/include \
	-I../mbedtls/include \
	-I../libsrtp/include \
	-I../libsrtp/config \
	-I../libsrtp/crypto/include \
	-I../uKcp \
	-I../uNet

MIIPC_DEFS=-DNDEBUG -DMG_MAX_RECV_SIZE=1048576UL
LDFLAGS=$(BaseLibDir) -L$(OUT_LIB) -Wl,-rpath-link,$(OUT_LIB) -Wl,-rpath,'$$ORIGIN' -Wl,-rpath,'$$ORIGIN/../lib'
CFLAGS=$(CompileOpion) $(AppExtFlg) $(BASECFLAGS) $(MIIPC_DEFS) $(BASEDIR)
CXXFLAGS=$(CompileOpion) $(CompileOpionCpp) $(AppExtFlg) $(BASECFLAGS) $(MIIPC_DEFS) $(BASEDIR)
EXTRA_TARGETS=
MPP_CSRCS=
RUN_SCRIPT=

LIB_CSRCS=App/miipcappinfo.o \
	App/miipcosd.o \
	Audio/miipc_aec.o \
	Audio/miipc_audio_process.o \
	Audio/miipc_g711.o \
	Broadcast/miipcbroadcast.o \
	Config/miipcappconfig.o \
	Config/miipccfgdb.o \
	Config/miipcsignalconfig.o \
	DevMsg/midevmsg.o \
	Http/mihttpserver.o \
	Http/miwebrtcbridge.o \
	Http/mongoose.o \
	JSON/cJSON.o \
	Log/miipclog.o \
	Network/miipcnetroute.o \
	Rtsp/miipcrtspserver.o \
	SdMgr.o \
	SdStorage/miipcsdstorage.o \
	../libmp4/source/libmp4.o \
	../libmp4/source/libmp4_writer.o \
	../libmp4/source/libmp4_reader.o \
	../libmp4/source/libmp4_h264.o \
	../libmp4/source/libmp4_h265.o \
	../libmp4/source/libmp4_g711.o \
	Signal/midevicesignal.o \
	Signal/midevicesignalconsumer.o \
	MiIpc.o

APP_SRC_DIR=../APP
APP_CSRCS=APP.o
CXXSRCS=

ifeq ($(DevType),ts5110/)
BASEDIR += -I../SOCSDK/TS5110/staging/include \
	-I../SOCSDK/TS5110/staging/include/algstore \
	-I./Soc/TS5110 \
	-I./Soc/TS5110/common

LIB_CSRCS += Soc/TS5110/common/loadbmp.o \
	Soc/TS5110/common/sample_comm_audio.o \
	Soc/TS5110/common/sample_comm_isp.o \
	Soc/TS5110/common/sample_comm_region.o \
	Soc/TS5110/common/sample_comm_sys.o \
	Soc/TS5110/common/sample_comm_util.o \
	Soc/TS5110/common/sample_comm_venc.o \
	Soc/TS5110/common/sample_comm_vi.o \
	Soc/TS5110/common/sample_comm_vpss.o \
	Soc/TS5110/common/soft_line.o \
	Soc/TS5110/common/video_common_utils.o \
	Soc/TS5110/common/osd/bitmap.o \
	Soc/TS5110/common/osd/osd_timestamp.o \
	Soc/TS5110/common/osd/osd_truetype.o \
	Soc/TS5110/SocImp.o \
	Soc/TS5110/gpio.o \
	Soc/TS5110/vi0_venc.o \
	Soc/TS5110/sample_audio.o

MDK_OUT_LIB=../SOCSDK/TS5110
MDK_LIBS=$(MDK_OUT_LIB)/staging/usr/lib/libmpp_sys.a \
	$(MDK_OUT_LIB)/staging/usr/lib/libmpi.a \
	$(MDK_OUT_LIB)/staging/usr/lib/libomxc.a \
	$(MDK_OUT_LIB)/staging/usr/lib/libomxc_adec.a \
	$(MDK_OUT_LIB)/staging/usr/lib/libomxc_aenc.a \
	$(MDK_OUT_LIB)/staging/usr/lib/libomxc_ai.a \
	$(MDK_OUT_LIB)/staging/usr/lib/libomxc_ao.a \
	$(MDK_OUT_LIB)/staging/usr/lib/libomxc_cpm.a \
	$(MDK_OUT_LIB)/staging/usr/lib/libomxc_soft_vgs.a \
	$(MDK_OUT_LIB)/staging/usr/lib/libomxc_split.a \
	$(MDK_OUT_LIB)/staging/usr/lib/libomxc_videosplit.a \
	$(MDK_OUT_LIB)/staging/usr/lib/libomxc_vipipe.a \
	$(MDK_OUT_LIB)/staging/usr/lib/libomxc_vo.a \
	$(MDK_OUT_LIB)/staging/usr/lib/libomxc_vpe.a \
	$(MDK_OUT_LIB)/staging/usr/lib/libomxc_vpu.a \
	$(MDK_OUT_LIB)/staging/usr/lib/libvpu_sw.a \
	$(MDK_OUT_LIB)/staging/usr/lib/libvpu_comm.a \
	$(MDK_OUT_LIB)/staging/usr/lib/libisp_comm_hal.a \
	$(MDK_OUT_LIB)/staging/usr/lib/libaiisp_comm_hal.a \
	$(MDK_OUT_LIB)/staging/usr/lib/libtinyalsa.a \
	$(MDK_OUT_LIB)/staging/usr/lib/libup_vqe.a \
	$(MDK_OUT_LIB)/staging/usr/lib/libdown_vqe.a \
	$(MDK_OUT_LIB)/staging/usr/lib/libvdi.a \
	$(MDK_OUT_LIB)/staging/usr/lib/libcjson.a \
	$(MDK_OUT_LIB)/staging/usr/lib/libjson_convert.a \
	$(MDK_OUT_LIB)/staging/usr/lib/libomx.a \
	$(MDK_OUT_LIB)/staging/usr/lib/librne_common.a \
	$(MDK_OUT_LIB)/staging/usr/lib/librne_pal_linux.a \
	$(MDK_OUT_LIB)/staging/usr/lib/librne_rt.a \
	$(MDK_OUT_LIB)/staging/usr/lib/libalgvqesolwebrtc3a.a \
	$(MDK_OUT_LIB)/staging/usr/lib/algstore/libalgwebrtc3a.a \
	$(MDK_OUT_LIB)/staging/usr/lib/algstore/libalgomlsa.a \
	$(MDK_OUT_LIB)/staging/usr/lib/algstore/libalganrnn.a \
	$(MDK_OUT_LIB)/staging/usr/lib/algstore/libalgutil_sp.a \
	$(MDK_OUT_LIB)/staging/usr/lib/algstore/libalgnoisegate.a \
	$(MDK_OUT_LIB)/staging/usr/lib/algstore/libNE10_asm.a \
	$(MDK_OUT_LIB)/staging/usr/lib/algstore/libNE10_intrinsic.a \
	$(MDK_OUT_LIB)/staging/usr/lib/algstore/libalgmmsens.a \
	$(MDK_OUT_LIB)/staging/usr/lib/algstore/libalgutil_cv.a \
	$(MDK_OUT_LIB)/staging/usr/lib/libisp.a \
	$(MDK_OUT_LIB)/staging/usr/lib/lib3a.a \
	$(MDK_OUT_LIB)/staging/usr/lib/libbuf_mgr.a \
	$(MDK_OUT_LIB)/staging/usr/lib/libcmdr.a \
	$(MDK_OUT_LIB)/staging/usr/lib/libfrm_mgr.a \
	$(MDK_OUT_LIB)/staging/usr/lib/libisp_hal.a \
	$(MDK_OUT_LIB)/staging/usr/lib/libisp_osif.a \
	$(MDK_OUT_LIB)/staging/usr/lib/libisp_osutils.a \
	$(MDK_OUT_LIB)/staging/usr/lib/libisp_utils.a \
	$(MDK_OUT_LIB)/staging/usr/lib/libpproc.a \
	$(MDK_OUT_LIB)/staging/usr/lib/libsensor.a \
	$(MDK_OUT_LIB)/staging/usr/lib/libsen_modules.a \
	$(MDK_OUT_LIB)/staging/usr/lib/libsensor_sc2331.a \
	$(MDK_OUT_LIB)/staging/usr/lib/libcalibration_sc2331.a \
	$(MDK_OUT_LIB)/staging/usr/lib/libcsi2d_sc2331.a \
	$(MDK_OUT_LIB)/staging/usr/lib/libtuning_sc2331_normal.a \
	$(MDK_OUT_LIB)/staging/usr/lib/libcalibration_sc2336p.a \
	$(MDK_OUT_LIB)/staging/usr/lib/libcsi2d_sc2336p.a \
	$(MDK_OUT_LIB)/staging/usr/lib/libsensor_sc2336p.a \
	$(MDK_OUT_LIB)/staging/usr/lib/libtuning_sc2336p_normal.a \
	$(MDK_OUT_LIB)/staging/usr/lib/libcalibration_sc2337p.a \
	$(MDK_OUT_LIB)/staging/usr/lib/libcsi2d_sc2337p.a \
	$(MDK_OUT_LIB)/staging/usr/lib/libsensor_sc2337p.a \
	$(MDK_OUT_LIB)/staging/usr/lib/libtuning_sc2337p_normal.a \
	$(MDK_OUT_LIB)/staging/usr/lib/algstore/libalgcommon.a

else ifeq ($(DevType),cv610/)
BASEDIR += -I../SOCSDK/cv610/include \
	-I../SOCSDK/cv610/include/exp_inc \
	-I../SOCSDK/cv610/include/module \
	-I./Soc/cv610 \
	-I./Soc/cv610/common

AppExtFlg += -DSENSOR0_TYPE=SC4336P_MIPI_4M_30FPS_10BIT -DMI_CV610_VPSS_CHN_NUM=2

LIB_CSRCS += Soc/cv610/common/sample_comm_isp.o \
	Soc/cv610/common/sample_comm_sys.o \
	Soc/cv610/common/sample_comm_venc.o \
	Soc/cv610/common/sample_comm_vi.o \
	Soc/cv610/common/sample_comm_vpss.o \
	Soc/cv610/SocImp.o \
	Soc/cv610/higpio.o \
	Soc/cv610/micv610osd.o

MPP_CSRCS=Soc/cv610/mpp.o \
	Soc/cv610/cv610warp.o \
	Soc/cv610/common/sample_comm_isp.o \
	Soc/cv610/common/sample_comm_sys.o \
	Soc/cv610/common/sample_comm_venc.o \
	Soc/cv610/common/sample_comm_vi.o \
	Soc/cv610/common/sample_comm_vpss.o

EXTRA_TARGETS=mpp
RUN_SCRIPT=Soc/cv610/Run.sh

MDK_OUT_LIB=../SOCSDK/cv610
MDK_LIB_DIR=$(MDK_OUT_LIB)/lib
MDK_LIBS=$(MDK_LIB_DIR)/libhi_mpi.a \
	$(MDK_LIB_DIR)/libhi_mpi_sysbind.a \
	$(MDK_LIB_DIR)/libhi_mpi_sysmem.a \
	$(MDK_LIB_DIR)/libhi_mpi_isp.a \
	$(MDK_LIB_DIR)/libhi_mpi_ae.a \
	$(MDK_LIB_DIR)/libhi_mpi_awb.a \
	$(MDK_LIB_DIR)/libot_mpi_isp.a \
	$(MDK_LIB_DIR)/libldci.a \
	$(MDK_LIB_DIR)/libcalcflicker.a \
	$(MDK_LIB_DIR)/libextend_stats.a \
	$(MDK_LIB_DIR)/libdehaze.a \
	$(MDK_LIB_DIR)/libbnr.a \
	$(MDK_LIB_DIR)/libacs.a \
	$(MDK_LIB_DIR)/libir_auto.a \
	$(MDK_LIB_DIR)/libdrc.a \
	$(MDK_LIB_DIR)/libsns_sc4336p.a \
	$(MDK_LIB_DIR)/libhi_mpi_syskol.a \
	$(MDK_LIB_DIR)/libhi_mpi_devstat.a \
	$(MDK_LIB_DIR)/libot_osal.a \
	$(MDK_LIB_DIR)/libsecurec.a

else ifeq ($(DevType),pclinux/)

else
$(error MiIpc Makefile currently supports DevType=pclinux/ ts5110/ or cv610/)
endif

PROJECT_LIBS=$(OUT_LIB)/libMiRtc.so \
	$(OUT_LIB)/libMiOnvif.a \
	$(OUT_LIB)/libaudio_processing.a \
	$(OUT_LIB)/libmov.a \
	$(OUT_LIB)/libuNet.a \
	$(OUT_LIB)/libuKcp.a \
	$(OUT_LIB)/libMiBase.a \
	$(OUT_LIB)/libmbedtls.a \
	$(OUT_LIB)/libsrtp.a

# Linux/TS5110 uses libc getopt/getopt_long through <getopt.h>.
# Do not add MiIpc/GetOpt/getopt.c here; that file is only for Windows builds.

TARGET_LIB=libMiIpc.so
TARGET_APP=Ffrock

.PHONY: all clean

all: $(TARGET_LIB) $(TARGET_APP) $(EXTRA_TARGETS)

$(TARGET_LIB): $(LIB_CSRCS) $(CXXSRCS)
	$(CC) -shared -Wl,-soname,$(TARGET_LIB) -Wl,--gc-sections -o $(TARGET_LIB) $(LIB_CSRCS) $(CXXSRCS) $(LDFLAGS) -Wl,--start-group $(PROJECT_LIBS) $(MDK_LIBS) -Wl,--end-group -lpthread -ldl -lm
	$(strip) --strip-unneeded $(TARGET_LIB)
	mkdir -p $(OUT_LIB)
	cp -f $(TARGET_LIB) $(OUT_LIB)/
	@echo '$(TARGET_LIB) done'

$(TARGET_APP): $(APP_CSRCS) $(TARGET_LIB)
	$(CC) $(CFLAGS) $(PdbOpion) -o $(TARGET_APP) $(APP_CSRCS) $(LDFLAGS) -lMiIpc -lpthread -ldl -lm
	$(strip) --strip-unneeded $(TARGET_APP)
	mkdir -p $(OUT_BIN)
	cp -f $(TARGET_APP) $(OUT_BIN)/
	cp -f $(TARGET_APP) $(OUT_BIN)/$(TARGET_APP)
	cp -f $(TARGET_LIB) $(OUT_BIN)/
	if [ -f "$(OUT_LIB)/libMiRtc.so" ]; then cp -f "$(OUT_LIB)/libMiRtc.so" "$(OUT_BIN)/"; fi
	if [ -f "$(APP_SRC_DIR)/Cfg.ini" ]; then cp -f "$(APP_SRC_DIR)/Cfg.ini" "$(OUT_BIN)/"; fi
	if [ -f "$(APP_SRC_DIR)/app.ini" ]; then cp -f "$(APP_SRC_DIR)/app.ini" "$(OUT_BIN)/"; fi
	if [ -n "$(RUN_SCRIPT)" ]; then cp -f $(RUN_SCRIPT) $(OUT_BIN)/Run.sh; chmod +x $(OUT_BIN)/Run.sh; fi
	@echo '$(TARGET_APP) done'

mpp: $(MPP_CSRCS)
	$(CC) $(CFLAGS) $(PdbOpion) -o mpp $(MPP_CSRCS) $(LDFLAGS) -Wl,--start-group $(MDK_LIBS) -Wl,--end-group -lpthread -ldl -lm
	$(strip) --strip-unneeded mpp
	mkdir -p $(OUT_BIN)
	cp -f mpp $(OUT_BIN)/
	if [ -n "$(RUN_SCRIPT)" ]; then cp -f $(RUN_SCRIPT) $(OUT_BIN)/Run.sh; chmod +x $(OUT_BIN)/Run.sh; fi
	@echo 'mpp done'

$(APP_CSRCS): %.o: $(APP_SRC_DIR)/%.c
	$(CC) $(CFLAGS) -fvisibility=hidden -ffunction-sections -fdata-sections -Wl,--gc-sections -c $< -o $@

$(LIB_CSRCS): %o:%c
	$(CC) $(CFLAGS) -fvisibility=hidden -ffunction-sections -fdata-sections -Wl,--gc-sections -c $< -o $@

$(CXXSRCS): %o:%cpp
	$(CXX) $(CXXFLAGS) -fvisibility=hidden -ffunction-sections -fdata-sections -Wl,--gc-sections -c $< -o $@

clean:
	rm -f $(TARGET_LIB) $(TARGET_APP) $(EXTRA_TARGETS) $(LIB_CSRCS) $(APP_CSRCS) $(CXXSRCS) $(MPP_CSRCS) $(OUT_LIB)/$(TARGET_LIB) $(OUT_BIN)/$(TARGET_LIB) $(OUT_BIN)/$(TARGET_APP) $(OUT_BIN)/$(TARGET_APP) $(OUT_BIN)/mpp
