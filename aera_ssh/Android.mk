LOCAL_PATH := $(call my-dir)

# SFTP v3 subsystem binary spawned by Dropbear for "sftp" sessions.
include $(CLEAR_VARS)

LOCAL_MODULE := aera_sftp_server
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE_CLASS := RECOVERY_EXECUTABLES
LOCAL_MODULE_PATH := $(TARGET_RECOVERY_ROOT_OUT)/system/bin
LOCAL_SRC_FILES := sftp_server.cpp
LOCAL_CFLAGS := -D_FILE_OFFSET_BITS=64 -Wno-sign-compare

include $(BUILD_EXECUTABLE)
