
PSP_EBOOT_TITLE = NullDC/PSP
TARGET = nullDCpsp
OBJS = \
 ../cl/cl.o \
 ../config/config.o \
 ../dc/aica/aica_if.o \
 ../dc/arm7/arm7.o \
 ../dc/arm7/mem.o \
 ../dc/arm7/SoundOut.o \
 ../dc/arm7/vbaARM.o \
 ../dc/asic/asic.o \
 ../dc/dc.o \
 ../dc/gdrom/gdromv3.o \
 ../dc/gdrom/gdrom_response.o \
 ../dc/gdrom/disc_common.o \
 ../dc/maple/maple_if.o \
 ../dc/maple/maple_devs.o \
 ../dc/maple/maple_cfg.o \
 ../dc/maple/maple_helper.o \
 ../dc/mem/memutil.o \
 ../dc/mem/mmu.o \
 ../dc/mem/sb.o \
 ../dc/mem/sh4_area0.o \
 ../dc/mem/sh4_internal_reg.o \
 ../dc/mem/sh4_mem.o \
 ../dc/mem/_vmem.o \
 ../dc/pvr/pvrLock.o \
 ../dc/pvr/pvr_if.o \
 ../dc/pvr/pvr_sb_regs.o \
 ../dc/sh4/bsc.o \
 ../dc/sh4/ccn.o \
 ../dc/sh4/cpg.o \
 ../dc/sh4/dmac.o \
 ../dc/sh4/intc.o \
 ../dc/sh4/rtc.o \
 ../dc/sh4/sci.o \
 ../dc/sh4/scif.o \
 ../dc/sh4/sh4_cpu.o \
 ../dc/sh4/sh4_fpu.o \
 ../dc/sh4/sh4_if.o \
 ../dc/sh4/sh4_interpreter.o \
 ../dc/sh4/sh4_opcode_list.o \
 ../dc/sh4/sh4_registers.o \
 ../dc/sh4/sh4_sched.o \
 ../dc/sh4/tmu.o \
 ../dc/sh4/ubc.o \
 ../dc/sh4/rec_v2/blockmanager.o \
 ../dc/sh4/rec_v2/driver.o \
 ../dc/sh4/rec_v2/decoder.o \
 ../dc/sh4/rec_v2/shil.o \
 ../dc/sh4/rec_v2/ssa.o \
 ../dc/sh4/rec_v2/sinTable.o \
 ../dc/sh4/rec_v2/xxhash/xxhash.o \
 ../nullDC.o \
 ../plugins/plugin_manager.o \
 ../plugins/plugin_types.o \
 ../stdclass.o \
 ../plugs/nullExtDev/nullExtDev.o \
 ../plugs/drkMapleDevices/drkMapleDevices.o \
 ../plugs/EmptyAICA/aica_hax.o \
 ../plugs/EmptyAICA/aica_hle.o \
 ../plugs/EmptyAICA/EmptyAICA.o \
 ../plugs/nullAICA/aica.o \
 ../plugs/nullAICA/mem.o \
 ../plugs/nullAICA/sgc_if.o \
 ../plugs/ImgReader/ioctl.o \
 ../plugs/ImgReader/cdi.o \
 ../plugs/ImgReader/gdi.o \
 ../plugs/ImgReader/chd.o \
 ../plugs/ImgReader/cue.o \
 ../plugs/ImgReader/chd.o \
 ../deps/cdipsr/cdipsr.o \
 ../deps/chdr/bitstream.o \
 ../deps/chdr/cdrom.o \
 ../deps/chdr/chd.o \
 ../deps/chdr/flac.o \
 ../deps/chdr/huffman.o \
 ../deps/coreio/coreio.o \
 ../deps/crypto/md5.o \
 ../deps/crypto/sha1.o \
 ../deps/crypto/sha256.o \
 ../plugs/ImgReader/imgread_common.o \
 ../plugs/drkPvr/drkPvr.o \
 ../plugs/drkPvr/threaded.o \
 ../plugs/drkPvr/regs.o \
 ../plugs/drkPvr/nullRend.o \
 ../plugs/drkPvr/Renderer_if.o \
 ../plugs/drkPvr/SPG.o \
 ../plugs/drkPvr/ta.o \
 VramExt.o 
 
 #PSP only files
 OBJS+= \
 ../psp/dc/sh4/rec_v2/psp_driver.o \
 ../psp/dc/sh4/rec_v2/fast_access.o \
  ../psp/main.o \

INCDIR = ../  ./include
CFLAGS = -O3 -g 
CFLAGS += -mno-gpopt
CFLAGS += -Wall -mno-check-zero-division
CFLAGS +=  -fno-use-cxa-atexit -flive-range-shrinkage -fsplit-loops -funswitch-loops -funsafe-math-optimizations -fassociative-math -freorder-blocks-and-partition -fira-region=all -fivopts -frename-registers -fno-strict-aliasing -fsingle-precision-constant -fno-zero-initialized-in-bss -fmodulo-sched-allow-regmoves -fmodulo-sched -fgcse-sm -fomit-frame-pointer -fmerge-all-constants -ffast-math -funroll-loops 
CFLAGS += -DRELEASE
#CFLAGS += -save-temps
CXXFLAGS = -fno-exceptions -fno-rtti -D _PSP
ASFLAGS = -c -x assembler-with-cpp

EXTRA_TARGETS = EBOOT.PBP
PSP_EBOOT_ICON = ICON0.PNG

LIBDIR = ./libs
LIBS =  -lme -lzlib -lpspdmac -lpspkubridge -lpspgum -lpspgu -lpspaudio -lm -lstdc++ -lpsprtc -lpsppower -lpspusb
LDFLAGS =

PSP_LARGE_MEMORY=1

PSPSDK=$(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak
