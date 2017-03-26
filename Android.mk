LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)

LOCAL_SRC_FILES:=         \
        stagecap.cpp

LOCAL_SHARED_LIBRARIES := \
	libstagefright liblog libutils libbinder

LOCAL_C_INCLUDES:= \
	$(JNI_H_INCLUDE) \
	frameworks/base/media/libstagefright \
	$(TOP)/frameworks/base/include/media/stagefright/openmax

LOCAL_CFLAGS += -Wno-multichar

LOCAL_MODULE_TAGS := debug

LOCAL_MODULE:= stagecap

include $(BUILD_EXECUTABLE)
