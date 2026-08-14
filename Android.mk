# SPDX-License-Identifier: Apache-2.0

LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE := moonlight_haptics_core
LOCAL_SRC_FILES := src/core/audio_haptics_engine.cpp \
                   src/core/authored_haptics_engine.cpp \
                   src/core/causal_onset_detector.cpp \
                   src/core/causal_rhythm_clock.cpp \
                   src/core/feature_extractor.cpp \
                   src/core/game_scene_author.cpp \
                   src/core/music_scene_author.cpp \
                   src/core/rhythm_activation_extractor.cpp \
                   src/core/speech_detector.cpp \
                   src/core/speech_presence_estimator.cpp \
                   src/dsp/aosp_haptic_envelope.cpp \
                   src/dsp/real_fft.cpp \
                   third_party/libfvad/src/fvad.c \
                   third_party/libfvad/src/signal_processing/division_operations.c \
                   third_party/libfvad/src/signal_processing/energy.c \
                   third_party/libfvad/src/signal_processing/get_scaling_square.c \
                   third_party/libfvad/src/signal_processing/resample_48khz.c \
                   third_party/libfvad/src/signal_processing/resample_by_2_internal.c \
                   third_party/libfvad/src/signal_processing/resample_fractional.c \
                   third_party/libfvad/src/signal_processing/spl_inl.c \
                   third_party/libfvad/src/vad/vad_core.c \
                   third_party/libfvad/src/vad/vad_filterbank.c \
                   third_party/libfvad/src/vad/vad_gmm.c \
                   third_party/libfvad/src/vad/vad_sp.c
LOCAL_C_INCLUDES := $(LOCAL_PATH)/include \
                    $(LOCAL_PATH)/src \
                    $(LOCAL_PATH)/third_party/libfvad/include \
                    $(LOCAL_PATH)/third_party/libfvad/src
LOCAL_EXPORT_C_INCLUDES := $(LOCAL_PATH)/include
LOCAL_CPPFLAGS := -std=c++17 -O2 -DNDEBUG \
                  -Wall -Wextra -Wpedantic -Wconversion -Wshadow
LOCAL_CPP_FEATURES := exceptions
LOCAL_EXPORT_CFLAGS := -DMOONLIGHT_HAPTICS_STATIC=1
LOCAL_CFLAGS := -DMOONLIGHT_HAPTICS_STATIC=1
LOCAL_BRANCH_PROTECTION := standard
include $(BUILD_STATIC_LIBRARY)
