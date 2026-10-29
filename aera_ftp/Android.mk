LOCAL_PATH := $(call my-dir)

# Standalone plain-FTP server. Credentials are the SSH root password.
include $(CLEAR_VARS)

LOCAL_MODULE := aera_ftp_server
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE_CLASS := RECOVERY_EXECUTABLES
LOCAL_MODULE_PATH := $(TARGET_RECOVERY_ROOT_OUT)/system/bin
LOCAL_SRC_FILES := ftp_server.cpp
LOCAL_SHARED_LIBRARIES := libcrypto
LOCAL_CFLAGS := -D_FILE_OFFSET_BITS=64 -Wno-sign-compare

include $(BUILD_EXECUTABLE)
