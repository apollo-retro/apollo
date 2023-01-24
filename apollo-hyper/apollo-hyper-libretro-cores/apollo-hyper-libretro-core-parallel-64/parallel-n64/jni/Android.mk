LOCAL_PATH := $(call my-dir)

ROOT_DIR      := $(LOCAL_PATH)/..
LIBRETRO_DIR  := $(ROOT_DIR)/libretro

SOURCES_C     :=
SOURCES_CXX   :=
SOURCES_ASM   :=
INCFLAGS      :=
CFLAGS        :=
CXXFLAGS      :=
DYNAFLAGS     :=
HAVE_NEON     := 0
WITH_DYNAREC  :=

HAVE_OPENGL   := 1
GLES          := 1
HAVE_PARALLEL := 1
HAVE_PARALLEL_RSP := 1
HAVE_THR_AL   := 1

ifeq ($(TARGET_ARCH_ABI),arm64-v8a)
  WITH_DYNAREC := aarch64
else ifeq ($(TARGET_ARCH_ABI),armeabi-v7a)
  WITH_DYNAREC := arm
  HAVE_NEON := 1
else ifeq ($(TARGET_ARCH_ABI),x86)
  # X86 dynarec isn't position independent, so it will not run on api 23+
  # This core uses vulkan which is api 24+, so dynarec cannot be used
  WITH_DYNAREC := bogus
else ifeq ($(TARGET_ARCH_ABI),x86_64)
  WITH_DYNAREC := x86_64
endif

include $(ROOT_DIR)/Makefile.common

COREFLAGS := -ffast-math -DM64P_CORE_PROTOTYPES -D_ENDUSER_RELEASE -DM64P_PLUGIN_API -D__LIBRETRO__ -DINLINE="inline" -DANDROID -DARM_FIX $(GLFLAGS) $(INCFLAGS) $(DYNAFLAGS)

GIT_VERSION := " $(shell git rev-parse --short HEAD || echo unknown)"
ifneq ($(GIT_VERSION)," unknown")
  COREFLAGS += -DGIT_VERSION=\"$(GIT_VERSION)\"
endif

include $(CLEAR_VARS)
LOCAL_MODULE       := retro
LOCAL_SRC_FILES    := $(SOURCES_CXX) $(SOURCES_C) $(SOURCES_ASM)
LOCAL_CFLAGS       := $(COREFLAGS) $(CFLAGS)
LOCAL_CXXFLAGS     := -std=c++11 $(COREFLAGS) $(CXXFLAGS)
LOCAL_LDFLAGS      := -Wl,-version-script=$(LIBRETRO_DIR)/link.T
LOCAL_LDLIBS       := -lGLESv2 -llog
LOCAL_CPP_FEATURES := exceptions
LOCAL_ARM_NEON     := true
LOCAL_ARM_MODE     := arm
include $(BUILD_SHARED_LIBRARY)
