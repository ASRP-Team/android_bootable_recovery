#
#	This file is part of the OrangeFox Recovery Project
# 	Copyright (C) 2026 The OrangeFox Recovery Project
#	
#	OrangeFox is free software: you can redistribute it and/or modify
#	it under the terms of the GNU General Public License as published by
#	the Free Software Foundation, either version 3 of the License, or
#	any later version.
#
#	OrangeFox is distributed in the hope that it will be useful,
#	but WITHOUT ANY WARRANTY; without even the implied warranty of
#	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#	GNU General Public License for more details.
#
# 	This software is released under GPL version 3 or any later version.
#	See <http://www.gnu.org/licenses/>.
# 	
# 	Please maintain this if you use this script or any part of it
#

# additional soong variables
# The validation hook reads both the public AERA_* spelling and the internal
# TW_* name so a mapping change cannot silently hide a conflict.
EXPORT_TO_SOONG += \
   OF_LANDSCAPE_MODE \
   BOARD_BOOT_HEADER_VERSION \
   AERA_VENDOR_BOOT_RECOVERY \
   AERA_INCLUDE_CRYPTO \
   TW_INCLUDE_CRYPTO \
   AERA_INCLUDE_CRYPTO_FBE \
   TW_INCLUDE_CRYPTO_FBE \
   AERA_INCLUDE_FBE_METADATA_DECRYPT \
   TW_INCLUDE_FBE_METADATA_DECRYPT \
   AERA_INCLUDE_OMAPI \
   TW_INCLUDE_OMAPI \
   AERA_CUSTOM_POWER_BUTTON \
   TW_CUSTOM_POWER_BUTTON \
   AERA_USE_KEY_CODE_TOUCH_SYNC \
   TW_USE_KEY_CODE_TOUCH_SYNC \
   AERA_QCOM_ATS_OFFSET \
   TW_QCOM_ATS_OFFSET \
   AERA_CLOCK_OFFSET \
   TW_CLOCK_OFFSET \
   AERA_USE_FSCRYPT_POLICY \
   AERA_AB_DEVICE \
   AERA_VIRTUAL_AB_DEVICE \
   AERA_VANILLA_BUILD \
   AERA_AB_DEVICE_WITH_RECOVERY_PARTITION \
   AERA_USE_DMCTL \
   AERA_USE_DMSETUP \
   AERA_ENABLE_WLAN \
   AERA_ENABLE_LAB \
   AERA_USE_NANO_EDITOR \
   AERA_ALLOW_EARLY_SETTINGS_LOAD \
   AERA_USE_DATA_RECOVERY_FOR_SETTINGS \
   AERA_SUPPORT_OZIP_DECRYPTION \
   AERA_USE_MEIZU_TOUCH_MAPPING
#
