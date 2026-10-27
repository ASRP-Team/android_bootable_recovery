cmake_minimum_required(VERSION 3.16)
project(aera_ui_tests LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(LVGL_DIR "" CACHE PATH "Recovery's external/lvgl directory")
if(NOT EXISTS "${LVGL_DIR}/lv_conf.h")
  message(FATAL_ERROR "Set LVGL_DIR to the recovery tree's configured external/lvgl directory")
endif()
set(LV_BUILD_CONF_PATH "${LVGL_DIR}/lv_conf.h" CACHE PATH "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "" FORCE)
add_subdirectory("${LVGL_DIR}" lvgl)
find_package(Freetype REQUIRED)
find_package(PNG REQUIRED)
find_package(JPEG REQUIRED)
find_package(ZLIB REQUIRED)
find_package(Threads REQUIRED)
target_link_libraries(lvgl PUBLIC Freetype::Freetype)
add_executable(ui_check ui_check.cpp plugin_runtime_ui_test.cpp
  ../components/plugin_desktop.cpp
  ../features/plugin_api/session.cpp ../features/plugin_api/activity.cpp
  ../scenes/developer_scene.cpp
  ../scenes/mobile_data_scene.cpp
  ../scenes/update_scene.cpp
  ../features/developer/kernel_settings.cpp
  ../scenes/reboot_scene.cpp ../components/power_transition.cpp
  ../scenes/lock_scene.cpp
  ../scenes/package_review.cpp
  ../scenes/launcher_scene.cpp ../components/retroarch_icon.cpp
  ../components/home_plugin_grid.cpp
  ../components/webkit_icon.cpp
  ../scenes/about_scene.cpp ../scenes/artillery_scene.cpp
  ../features/artillery/game.cpp ../components/aera_logo.cpp
  ../components/matrix_engraving.cpp
  ../scenes/home_scene.cpp ../features/files/file_manager.cpp
  ../components/text_edit.cpp
  ../localization/fonts/aera_fallback.cpp ../localization/i18n.cpp
  ../localization/catalog/catalog.cpp
  ../localization/catalog/catalog_generated.cpp
  ../scenes/tool_scene.cpp ../scenes/sideload_scene.cpp
  ../scenes/font_browser.cpp ../features/fonts/fontsource.cpp
  ../features/fonts/font_library.cpp
  ../features/fonts/fontsource_download.cpp
  ../scenes/wifi_scene.cpp ../components/picture_decode.cpp
  ../components/picture_viewer.cpp ../components/wallpaper.cpp
  ../scenes/operation_scene.cpp
  ../scenes/decrypt_scene.cpp ../features/browser/web_scene.cpp
  ../features/browser/session.cpp ../features/browser/launcher.cpp)
target_include_directories(ui_check PRIVATE .. ../include ../components ../assets
  ../features ../features/files ../localization ../localization/fonts
  ../scenes)
target_compile_definitions(ui_check PRIVATE AERA_DEFAULT_LANGUAGE="en" AERA_EXTRA_LANGUAGES)
target_compile_definitions(ui_check PRIVATE AERA_ENABLE_MOBILE_DATA=1)
target_compile_definitions(ui_check PRIVATE
  AERA_FONT_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/../localization/fonts/assets"
  AERA_COMMON_FONT_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/../localization/fonts/assets")
target_compile_options(ui_check PRIVATE -UNDEBUG)
target_link_libraries(ui_check PRIVATE lvgl PNG::PNG JPEG::JPEG Threads::Threads ZLIB::ZLIB)
enable_testing()
foreach(mode unset disabled enabled)
  add_executable(mobile_data_flag_${mode}_test mobile_data_flag_test.cpp)
  target_include_directories(mobile_data_flag_${mode}_test PRIVATE ../include)
  target_compile_options(mobile_data_flag_${mode}_test PRIVATE -UNDEBUG)
  add_test(NAME mobile_data_flag_${mode} COMMAND mobile_data_flag_${mode}_test)
endforeach()
target_compile_definitions(mobile_data_flag_disabled_test PRIVATE AERA_ENABLE_MOBILE_DATA=0)
target_compile_definitions(mobile_data_flag_enabled_test PRIVATE AERA_ENABLE_MOBILE_DATA=1 EXPECT_MOBILE_DATA=1)
add_executable(wifi_ip_config_test wifi_ip_config_test.cpp)
target_compile_options(wifi_ip_config_test PRIVATE -UNDEBUG -Wall -Wextra -Werror)
add_test(NAME wifi_static_ip_validation COMMAND wifi_ip_config_test)
add_executable(wifi_dhcp_test wifi_dhcp_test.cpp)
target_compile_options(wifi_dhcp_test PRIVATE -UNDEBUG -Wall -Wextra -Werror)
add_test(NAME wifi_dhcp_client COMMAND wifi_dhcp_test)
add_test(NAME wifi_ip_settings_ui COMMAND ui_check --wifi-ip)
add_test(NAME browser_settings_ui COMMAND ui_check --browser-settings)
add_test(NAME mobile_sim_pin_ui COMMAND ui_check --sim-pin)
add_test(NAME network_tabs_and_mobile_cards COMMAND ui_check --mobile)
add_test(NAME recovery_channel_install_ui COMMAND ui_check --update-channel)
add_executable(artillery_test artillery_test.cpp ../features/artillery/game.cpp)
target_compile_options(artillery_test PRIVATE -UNDEBUG -Wall -Wextra -Werror)
add_test(NAME artillery_physics_and_unlock COMMAND artillery_test)
add_test(NAME artillery_ui_and_lifetime COMMAND ui_check --artillery)
add_executable(home_layout_test home_layout_test.cpp)
target_include_directories(home_layout_test PRIVATE ../include)
target_compile_options(home_layout_test PRIVATE -UNDEBUG -Wall -Wextra -Werror)
add_test(NAME home_plugin_packing COMMAND home_layout_test)
add_test(NAME home_plugin_editing COMMAND ui_check --home-edit)
add_test(NAME home_drag_stability COMMAND ui_check --home-drag-stability)
add_test(NAME home_gap_packing COMMAND ui_check --home-gap-packing)
add_executable(font_staging_test font_staging_test.cpp)
target_compile_options(font_staging_test PRIVATE -UNDEBUG -Wall -Wextra -Werror)
add_test(NAME font_storage_independence COMMAND font_staging_test)
add_test(NAME wipe_partition_row_layout COMMAND ui_check --wipe-layout)
add_executable(flash_log_test flash_log_test.cpp ../../aera_flash_log.cpp)
target_compile_options(flash_log_test PRIVATE -UNDEBUG -Wall -Wextra -Werror)
target_link_libraries(flash_log_test PRIVATE Threads::Threads)
add_test(NAME automatic_flash_diagnostics COMMAND flash_log_test)
set(AERA_TINYXML2_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../../external/tinyxml2")
add_executable(android_packages_xml_test android_packages_xml_test.cpp
  ../features/backup/android_packages_xml.cpp "${AERA_TINYXML2_DIR}/tinyxml2.cpp")
target_include_directories(android_packages_xml_test PRIVATE "${AERA_TINYXML2_DIR}")
target_compile_options(android_packages_xml_test PRIVATE -UNDEBUG -fno-exceptions)
add_test(NAME android_package_xml_codec COMMAND android_packages_xml_test)
add_executable(unencrypted_users_test unencrypted_users_test.cpp
  ../platform/unencrypted_users.cpp ../features/backup/android_packages_xml.cpp
  "${AERA_TINYXML2_DIR}/tinyxml2.cpp")
target_include_directories(unencrypted_users_test PRIVATE ../include "${AERA_TINYXML2_DIR}")
target_compile_options(unencrypted_users_test PRIVATE -UNDEBUG -fno-exceptions -Wall -Wextra -Werror)
add_test(NAME unencrypted_android_users COMMAND unencrypted_users_test)
add_executable(app_registration_test app_registration_test.cpp
  ../features/backup/android_packages_xml.cpp ../features/backup/app_registration.cpp
  "${AERA_TINYXML2_DIR}/tinyxml2.cpp")
target_include_directories(app_registration_test PRIVATE "${AERA_TINYXML2_DIR}")
target_compile_options(app_registration_test PRIVATE -UNDEBUG -fno-exceptions)
add_test(NAME app_backup_package_registration COMMAND app_registration_test)
add_executable(hardware_keys_test hardware_keys_test.cpp)
target_compile_options(hardware_keys_test PRIVATE -UNDEBUG)
add_test(NAME physical_keyboard_mapping COMMAND hardware_keys_test)
add_test(NAME physical_keyboard_ui COMMAND ui_check --hardware-keyboard)
add_executable(operation_affinity_test operation_affinity_test.cpp)
target_include_directories(operation_affinity_test PRIVATE ../core)
target_link_libraries(operation_affinity_test PRIVATE Threads::Threads)
target_compile_options(operation_affinity_test PRIVATE -UNDEBUG)
add_test(NAME operation_affinity COMMAND operation_affinity_test)
add_executable(aera_log_history_test aera_log_history_test.cpp)
target_compile_options(aera_log_history_test PRIVATE -UNDEBUG)
add_test(NAME recovery_log_retention COMMAND aera_log_history_test)
add_executable(navigation_dock_test navigation_dock_test.cpp)
target_include_directories(navigation_dock_test PRIVATE .. ../include
  ../components ../localization ../scenes ../features)
target_compile_options(navigation_dock_test PRIVATE -UNDEBUG)
target_link_libraries(navigation_dock_test PRIVATE lvgl PNG::PNG)
add_test(NAME navigation_dock_layouts COMMAND navigation_dock_test)
add_executable(edge_swipe_test edge_swipe_test.cpp)
target_include_directories(edge_swipe_test PRIVATE ../core)
target_compile_options(edge_swipe_test PRIVATE -UNDEBUG)
add_test(NAME edge_swipe_cancel COMMAND edge_swipe_test)
add_executable(button_cancel_test button_cancel_test.cpp)
target_compile_options(button_cancel_test PRIVATE -UNDEBUG)
target_link_libraries(button_cancel_test PRIVATE lvgl)
add_test(NAME button_drag_cancel COMMAND button_cancel_test)
add_test(NAME ui_confirmation_navigation_preferences
  COMMAND ui_check "${LVGL_DIR}/examples/libs/libpng/png_demo.png")
add_test(NAME file_timestamp_layout COMMAND ui_check --file-dates)
add_test(NAME file_sort_preferences_and_toggle COMMAND ui_check --file-sort)
add_executable(plugin_download_test plugin_download_test.cpp)
target_include_directories(plugin_download_test PRIVATE ../features)
target_compile_options(plugin_download_test PRIVATE -UNDEBUG -Wall -Wextra -Werror)
target_link_options(plugin_download_test PRIVATE -Wl,--wrap=execv)
target_link_libraries(plugin_download_test PRIVATE Threads::Threads)
add_test(NAME plugin_download_inactivity_and_cancellation COMMAND plugin_download_test)
add_test(NAME settings_autosave_and_reset COMMAND ui_check --settings)
add_test(NAME lock_screen_customisation COMMAND ui_check --lock-theme)
add_test(NAME themed_home_artwork COMMAND ui_check --art)
add_test(NAME themed_home_layout COMMAND ui_check --home-art)
add_test(NAME manual_slot_switch_warning COMMAND ui_check --slot-warning)
add_executable(kernel_settings_test kernel_settings_test.cpp ../features/developer/kernel_settings.cpp)
target_include_directories(kernel_settings_test PRIVATE ../include)
target_compile_options(kernel_settings_test PRIVATE -UNDEBUG -Wall -Wextra -Werror)
add_test(NAME developer_kernel_settings COMMAND kernel_settings_test)
add_test(NAME developer_settings_ui COMMAND ui_check --developer-settings)
add_executable(splash_layout_test splash_layout_test.cpp)
target_compile_options(splash_layout_test PRIVATE -UNDEBUG -Wall -Wextra -Werror)
add_test(NAME splash_partition_discovery COMMAND splash_layout_test)
add_test(NAME zip_partition_preservation
  COMMAND python3 "${CMAKE_CURRENT_SOURCE_DIR}/partition_preservation_test.py")
add_test(NAME splash_preservation_ui_and_payload COMMAND ui_check --splash-preservation)
add_test(NAME pc_connection_settings_layout COMMAND ui_check --pc-settings)
add_test(NAME settings_navigation_and_language_viewport COMMAND ui_check --settings-navigation)
add_test(NAME preferences_layout COMMAND ui_check --preferences-layout)
add_test(NAME russian_translation_corrections COMMAND ui_check --russian-corrections)
add_test(NAME pc_backup_progress_labels COMMAND ui_check --pc-backup-progress)
add_test(NAME native_backup_controls_and_nas_progress COMMAND ui_check --backup-controls)
add_test(NAME pc_upload_banner_and_details COMMAND ui_check --pc-upload-progress)
add_test(NAME payload_preservation_selection COMMAND ui_check --payload-protection)
add_executable(root_module_assets_test root_module_assets_test.cpp)
target_compile_options(root_module_assets_test PRIVATE -UNDEBUG)
add_test(NAME root_module_release_asset_names COMMAND root_module_assets_test)
foreach(mode enabled disabled missing)
  add_executable(root_manager_${mode}_test root_manager_flag_test.cpp)
  if(mode STREQUAL "enabled")
    target_compile_definitions(root_manager_${mode}_test PRIVATE
      AERA_ENABLE_ROOT_MANAGER=1 EXPECT_ROOT_ENABLED=1)
  else()
    target_compile_definitions(root_manager_${mode}_test PRIVATE EXPECT_ROOT_ENABLED=0)
    if(mode STREQUAL "disabled")
      target_compile_definitions(root_manager_${mode}_test PRIVATE AERA_ENABLE_ROOT_MANAGER=0)
    endif()
  endif()
  add_test(NAME root_manager_flag_${mode} COMMAND root_manager_${mode}_test)
endforeach()
add_test(NAME advanced_payload_review COMMAND ui_check --payload-review)
add_test(NAME parallel_payload_progress COMMAND ui_check --payload-parallel)
add_test(NAME parallel_payload_progress_landscape COMMAND ui_check --payload-parallel-landscape)
add_executable(display_transform_test display_transform_test.cpp)
target_include_directories(display_transform_test PRIVATE ../include)
add_test(NAME adaptive_display_transform COMMAND display_transform_test)
add_executable(plugin_api_session_test plugin_api_session_test.cpp
  ../features/plugin_api/session.cpp)
target_include_directories(plugin_api_session_test PRIVATE ..
  ../features)
target_compile_options(plugin_api_session_test PRIVATE -UNDEBUG)
add_test(NAME plugin_api_negotiation_and_limits COMMAND plugin_api_session_test)
add_executable(plugin_clipboard_test plugin_clipboard_test.cpp)
target_compile_options(plugin_clipboard_test PRIVATE -UNDEBUG -Wall -Wextra -Werror)
add_test(NAME plugin_clipboard_transfers COMMAND plugin_clipboard_test)
add_executable(plugin_descriptor_test plugin_descriptor_test.cpp)
target_compile_options(plugin_descriptor_test PRIVATE -UNDEBUG -Wall -Wextra -Werror)
add_test(NAME plugin_descriptor_kernel_compatibility COMMAND plugin_descriptor_test)
add_executable(console_command_test console_command_test.cpp ../features/plugins/console_commands.cpp)
target_compile_options(console_command_test PRIVATE -UNDEBUG -Wall -Wextra -Werror)
add_test(NAME plugin_console_command_registry COMMAND console_command_test)
add_executable(plugin_activity_test plugin_activity_test.cpp
  ../features/plugin_api/activity.cpp)
target_include_directories(plugin_activity_test PRIVATE ../features)
target_compile_options(plugin_activity_test PRIVATE -UNDEBUG -Wall -Wextra -Werror)
target_link_libraries(plugin_activity_test PRIVATE Threads::Threads)
add_test(NAME plugin_background_storage_claims COMMAND plugin_activity_test)
add_test(NAME plugin_background_view_lifetime COMMAND ui_check --plugin-runtime)
add_executable(sideload_ui_test sideload_ui_test.cpp
  ../localization/fonts/aera_fallback.cpp ../localization/i18n.cpp
  ../localization/catalog/catalog_generated.cpp
  ../scenes/sideload_scene.cpp ../scenes/operation_scene.cpp)
target_include_directories(sideload_ui_test PRIVATE .. ../include ../components
  ../localization ../localization/fonts ../scenes)
target_compile_definitions(sideload_ui_test PRIVATE AERA_DEFAULT_LANGUAGE="en")
target_compile_definitions(sideload_ui_test PRIVATE
  AERA_FONT_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/../localization/fonts/assets"
  AERA_COMMON_FONT_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/../localization/fonts/assets")
target_compile_options(sideload_ui_test PRIVATE -UNDEBUG)
target_link_libraries(sideload_ui_test PRIVATE lvgl Threads::Threads)
add_test(NAME sideload_scene_and_progress COMMAND sideload_ui_test)
add_executable(file_manager_test file_manager_test.cpp
  ../features/files/file_manager.cpp)
target_include_directories(file_manager_test PRIVATE .. ../features/files)
target_compile_options(file_manager_test PRIVATE -UNDEBUG)
target_link_libraries(file_manager_test PRIVATE Threads::Threads)
add_test(NAME file_manager_operations COMMAND file_manager_test)
add_test(NAME file_search_ui COMMAND ui_check --file-search)

set(JSONCPP_WITH_TESTS OFF CACHE BOOL "" FORCE)
set(JSONCPP_WITH_POST_BUILD_UNITTEST OFF CACHE BOOL "" FORCE)
add_subdirectory("${LVGL_DIR}/../jsoncpp" jsoncpp)
target_link_libraries(ui_check PRIVATE jsoncpp_static)
add_executable(font_library_test font_library_test.cpp ../features/fonts/font_library.cpp)
target_include_directories(font_library_test PRIVATE .. ../include ../features)
target_compile_options(font_library_test PRIVATE -UNDEBUG -Wall -Wextra -Werror)
target_link_libraries(font_library_test PRIVATE jsoncpp_static OpenSSL::Crypto)
add_test(NAME font_library_persistence_and_cleanup COMMAND font_library_test)
add_executable(fontsource_test fontsource_test.cpp ../features/fonts/fontsource.cpp)
target_include_directories(fontsource_test PRIVATE ../features)
target_compile_options(fontsource_test PRIVATE -UNDEBUG -Wall -Wextra -Werror)
target_link_libraries(fontsource_test PRIVATE jsoncpp_static)
add_test(NAME fontsource_catalog_and_variants COMMAND fontsource_test)
add_executable(fontsource_download_test fontsource_download_test.cpp
  ../features/fonts/fontsource.cpp ../features/fonts/fontsource_download.cpp)
target_include_directories(fontsource_download_test PRIVATE ../features)
target_compile_options(fontsource_download_test PRIVATE -UNDEBUG -Wall -Wextra -Werror)
target_link_options(fontsource_download_test PRIVATE -Wl,--wrap=execv)
target_link_libraries(fontsource_download_test PRIVATE jsoncpp_static Threads::Threads)
add_test(NAME fontsource_download_cancellation COMMAND fontsource_download_test)
add_test(NAME font_browser_ui COMMAND ui_check --font-browser)
add_executable(app_vault_progress_test app_vault_progress_test.cpp)
target_compile_options(app_vault_progress_test PRIVATE -UNDEBUG -Wall -Wextra -Werror)
target_link_libraries(app_vault_progress_test PRIVATE jsoncpp_static)
add_test(NAME app_vault_backup_restore_progress COMMAND app_vault_progress_test)
add_executable(smb_browser_test smb_browser_test.cpp ../features/nas/smb_browser.cpp)
target_include_directories(smb_browser_test PRIVATE ../include ../features)
target_compile_options(smb_browser_test PRIVATE -UNDEBUG -Wall -Wextra -Werror)
target_link_options(smb_browser_test PRIVATE -Wl,--wrap=posix_spawn)
target_link_libraries(smb_browser_test PRIVATE jsoncpp_static Threads::Threads)
add_test(NAME smb_browse_paths_and_cancellation COMMAND smb_browser_test)

find_package(OpenSSL REQUIRED)
target_link_libraries(ui_check PRIVATE OpenSSL::Crypto)
add_executable(custom_font_storage_test custom_font_storage_test.cpp)
target_include_directories(custom_font_storage_test PRIVATE ..)
target_compile_options(custom_font_storage_test PRIVATE -UNDEBUG)
target_link_libraries(custom_font_storage_test PRIVATE OpenSSL::Crypto)
add_test(NAME custom_font_storage COMMAND custom_font_storage_test)
add_test(NAME custom_font_ui COMMAND ui_check --custom-font)
add_executable(pc_storage_test pc_storage_test.cpp ../../aera_remote/pc_storage.cpp)
target_compile_definitions(pc_storage_test PRIVATE AERA_PC_HOST_TEST=1)
target_compile_options(pc_storage_test PRIVATE -UNDEBUG -Wall -Wextra -Werror)
target_link_libraries(pc_storage_test PRIVATE jsoncpp_static Threads::Threads)
add_test(NAME pc_storage_blocks_ownership_and_disconnect COMMAND pc_storage_test)
add_executable(backup_cancel_test backup_cancel_test.cpp)
target_compile_options(backup_cancel_test PRIVATE -UNDEBUG -Wall -Wextra -Werror)
target_link_libraries(backup_cancel_test PRIVATE Threads::Threads)
add_test(NAME pc_backup_child_cancellation COMMAND backup_cancel_test)
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
  pkg_check_modules(AERA_HOST_JSONC QUIET IMPORTED_TARGET json-c)
  if(AERA_HOST_JSONC_FOUND)
    add_executable(nas_upload_test nas_upload_test.c)
    target_include_directories(nas_upload_test PRIVATE "${LVGL_DIR}/../libfuse/include")
    target_compile_options(nas_upload_test PRIVATE -UNDEBUG -ffunction-sections -fdata-sections)
    target_link_options(nas_upload_test PRIVATE -Wl,--gc-sections)
    target_link_libraries(nas_upload_test PRIVATE PkgConfig::AERA_HOST_JSONC)
    add_test(NAME nas_cached_upload_progress_and_cancel COMMAND nas_upload_test)
    find_package(Threads REQUIRED)
    target_link_libraries(nas_upload_test PRIVATE Threads::Threads)
    add_executable(nas_deadline_test nas_deadline_test.c ../../nas/native/deadline.c)
    target_compile_options(nas_deadline_test PRIVATE -UNDEBUG -Wall -Wextra -Werror)
    target_link_libraries(nas_deadline_test PRIVATE Threads::Threads)
    add_test(NAME nas_network_inactivity_deadline COMMAND nas_deadline_test)
  endif()
endif()
add_executable(kernel_kmi_test kernel_kmi_test.cpp)
target_compile_options(kernel_kmi_test PRIVATE -UNDEBUG -Wall -Wextra -Werror)
add_test(NAME target_slot_kernel_kmi COMMAND kernel_kmi_test)
add_executable(image_targets_test image_targets_test.cpp)
target_include_directories(image_targets_test PRIVATE ../include)
target_compile_options(image_targets_test PRIVATE -UNDEBUG -Wall -Wextra -Werror)
add_test(NAME physical_firmware_image_targets COMMAND image_targets_test)
find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_executable(pc_connection_host pc_connection_host.cpp ../../aera_remote/pc_connection.cpp ../../aera_remote/pc_storage.cpp ../../aera_remote/pc_terminal.cpp ../../aera_flash_log.cpp)
target_include_directories(pc_connection_host PRIVATE ../include)
set(AERA_TEST_ROOT_MANAGER "1" CACHE STRING "Root Manager feature flag for the PC host fixture")
find_program(AERA_TEST_TTYD NAMES ttyd)
if(AERA_TEST_TTYD)
  target_compile_definitions(pc_connection_host PRIVATE
    AERA_PC_TERMINAL_BINARY="${AERA_TEST_TTYD}" AERA_PC_TERMINAL_SHELL="/bin/sh")
endif()
target_compile_definitions(pc_connection_host PRIVATE OF_ENABLE_WLAN=1 AERA_PC_HOST_TEST=1
  AERA_ENABLE_ROOT_MANAGER=${AERA_TEST_ROOT_MANAGER}
  AERA_PC_PRIVATE_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}/pc-private"
  AERA_PC_CLIENT_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../../aera_remote/client/pc/index.html")
target_compile_options(pc_connection_host PRIVATE -Wall -Wextra -Wno-deprecated-declarations)
target_link_libraries(pc_connection_host PRIVATE jsoncpp_static OpenSSL::SSL OpenSSL::Crypto Threads::Threads)
add_test(NAME pc_connection_approval_transfer_and_install
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/pc_connection_test.py"
  "$<TARGET_FILE:pc_connection_host>" "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(pc_connection_approval_transfer_and_install PROPERTIES RUN_SERIAL TRUE TIMEOUT 45)

add_executable(nas_ui_test nas_ui_test.cpp ../scenes/nas_scene.cpp
  ../localization/fonts/aera_fallback.cpp ../localization/i18n.cpp
  ../localization/catalog/catalog_generated.cpp)
target_include_directories(nas_ui_test PRIVATE .. ../include ../components
  ../features ../localization ../localization/fonts ../scenes)
target_compile_definitions(nas_ui_test PRIVATE AERA_DEFAULT_LANGUAGE="en" AERA_EXTRA_LANGUAGES)
target_compile_definitions(nas_ui_test PRIVATE
  AERA_FONT_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/../localization/fonts/assets"
  AERA_COMMON_FONT_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/../localization/fonts/assets")
target_compile_options(nas_ui_test PRIVATE -UNDEBUG)
target_link_libraries(nas_ui_test PRIVATE lvgl Threads::Threads PNG::PNG)
add_test(NAME nas_picker_navigation_and_layout COMMAND nas_ui_test)

add_executable(phone_keyboard_test phone_keyboard_test.cpp
  ../localization/fonts/aera_fallback.cpp)
target_include_directories(phone_keyboard_test PRIVATE .. ../include ../components
  ../localization ../localization/fonts)
target_compile_definitions(phone_keyboard_test PRIVATE
  AERA_FONT_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/../localization/fonts/assets"
  AERA_COMMON_FONT_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/../localization/fonts/assets")
target_compile_options(phone_keyboard_test PRIVATE -UNDEBUG)
target_link_libraries(phone_keyboard_test PRIVATE lvgl PNG::PNG)
add_test(NAME phone_keyboard_shift_and_symbols COMMAND phone_keyboard_test)

foreach(target ui_check navigation_dock_test sideload_ui_test nas_ui_test)
  target_sources(${target} PRIVATE ../components/text_edit.cpp)
  target_include_directories(${target} PRIVATE ../features)
endforeach()

add_executable(text_edit_test text_edit_test.cpp ../components/text_edit.cpp
  ../localization/fonts/aera_fallback.cpp ../localization/i18n.cpp
  ../localization/catalog/catalog_generated.cpp)
target_include_directories(text_edit_test PRIVATE .. ../include ../components
  ../localization ../localization/fonts ../scenes ../features)
target_compile_definitions(text_edit_test PRIVATE AERA_DEFAULT_LANGUAGE="en" AERA_EXTRA_LANGUAGES
  AERA_FONT_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/../localization/fonts/assets"
  AERA_COMMON_FONT_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/../localization/fonts/assets")
target_compile_options(text_edit_test PRIVATE -UNDEBUG)
target_link_libraries(text_edit_test PRIVATE lvgl PNG::PNG)
add_test(NAME text_edit_selection_and_clipboard COMMAND text_edit_test)

foreach(target ui_check sideload_ui_test nas_ui_test text_edit_test)
  target_sources(${target} PRIVATE ../localization/catalog/catalog.cpp)
  target_link_libraries(${target} PRIVATE ZLIB::ZLIB)
endforeach()

foreach(target catalog_test catalog_english_test)
  add_executable(${target} catalog_test.cpp ../localization/i18n.cpp
    ../localization/catalog/catalog.cpp ../localization/catalog/catalog_generated.cpp)
  target_include_directories(${target} PRIVATE ../include ..)
  target_compile_definitions(${target} PRIVATE AERA_DEFAULT_LANGUAGE="en")
  target_compile_options(${target} PRIVATE -UNDEBUG)
  target_link_libraries(${target} PRIVATE lvgl jsoncpp_static ZLIB::ZLIB)
endforeach()
target_compile_definitions(catalog_test PRIVATE AERA_EXTRA_LANGUAGES)
add_test(NAME native_catalog_round_trip COMMAND catalog_test
  "${CMAKE_CURRENT_SOURCE_DIR}/../localization/catalog/catalog.json")
add_test(NAME native_catalog_english_only COMMAND catalog_english_test
  "${CMAKE_CURRENT_SOURCE_DIR}/../localization/catalog/catalog.json")

find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
  add_test(NAME wireless_adb_stop_preserves_usb COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/adb_shutdown_test.py")
  add_test(NAME chinese_catalog_coverage COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/chinese_catalog_test.py")
endif()

add_executable(plugin_store_test plugin_store_test.cpp ../scenes/plugin_scene.cpp
  ../features/plugins/store_metadata.cpp ../components/retroarch_icon.cpp
  ../components/text_edit.cpp ../localization/fonts/aera_fallback.cpp
  ../localization/i18n.cpp ../localization/catalog/catalog.cpp
  ../localization/catalog/catalog_generated.cpp)
target_include_directories(plugin_store_test PRIVATE .. ../include ../components
  ../features ../assets ../localization ../localization/fonts ../scenes)
target_compile_definitions(plugin_store_test PRIVATE AERA_DEFAULT_LANGUAGE="en" AERA_EXTRA_LANGUAGES
  AERA_FONT_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/../localization/fonts/assets"
  AERA_COMMON_FONT_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/../localization/fonts/assets")
target_compile_options(plugin_store_test PRIVATE -UNDEBUG)
target_link_libraries(plugin_store_test PRIVATE lvgl PNG::PNG Threads::Threads jsoncpp_static ZLIB::ZLIB)
add_test(NAME plugin_store_views_and_layout COMMAND plugin_store_test)

add_executable(touch_input_test touch_input_test.cpp)
target_include_directories(touch_input_test PRIVATE ../.. ../../minuitwrp/include ../../twrpinstall/include)
target_compile_definitions(touch_input_test PRIVATE BUILD_TWRPTAR_MAIN TW_NO_HAPTICS)
target_compile_options(touch_input_test PRIVATE -UNDEBUG)
add_test(NAME touch_coordinate_retention COMMAND touch_input_test)
add_test(NAME touch_single_axis_retention COMMAND touch_input_test single)
add_test(NAME touch_legacy_release_retention COMMAND touch_input_test legacy)

add_executable(status_gesture_test status_gesture_test.cpp
  ../features/plugin_api/activity.cpp
  ../components/text_edit.cpp
  ../localization/fonts/aera_fallback.cpp ../localization/i18n.cpp
  ../localization/catalog/catalog.cpp ../localization/catalog/catalog_generated.cpp)
target_include_directories(status_gesture_test PRIVATE .. ../include ../components
  ../features ../localization ../localization/fonts ../scenes)
target_compile_definitions(status_gesture_test PRIVATE AERA_DEFAULT_LANGUAGE="en")
target_compile_options(status_gesture_test PRIVATE -UNDEBUG)
target_link_libraries(status_gesture_test PRIVATE lvgl PNG::PNG Threads::Threads jsoncpp_static ZLIB::ZLIB)
add_test(NAME status_gesture_cancellation COMMAND status_gesture_test)
get_target_property(STATUS_GESTURE_SOURCES status_gesture_test SOURCES)
add_executable(quick_settings_mobile_test ${STATUS_GESTURE_SOURCES})
target_include_directories(quick_settings_mobile_test PRIVATE .. ../include ../components
  ../features ../localization ../localization/fonts ../scenes)
target_compile_definitions(quick_settings_mobile_test PRIVATE AERA_DEFAULT_LANGUAGE="en" AERA_ENABLE_MOBILE_DATA=1)
target_compile_options(quick_settings_mobile_test PRIVATE -UNDEBUG)
target_link_libraries(quick_settings_mobile_test PRIVATE lvgl PNG::PNG Threads::Threads jsoncpp_static ZLIB::ZLIB)
add_test(NAME quick_settings_mobile_tiles COMMAND quick_settings_mobile_test)
add_test(NAME status_bar_display_settings COMMAND ui_check --status-bar-settings)
add_executable(theme_preset_test theme_preset_test.cpp)
target_include_directories(theme_preset_test PRIVATE ../include)
target_compile_options(theme_preset_test PRIVATE -UNDEBUG -Wall -Wextra -Werror)
add_test(NAME native_theme_pack_table COMMAND theme_preset_test)
add_test(NAME theme_engine_categories_and_layout COMMAND ui_check --theme-categories)
add_test(NAME wallpaper_layout_and_editor COMMAND ui_check --wallpaper-layout)

add_executable(temperature_reader_test temperature_reader_test.cpp)
target_include_directories(temperature_reader_test PRIVATE ../include)
target_compile_options(temperature_reader_test PRIVATE -UNDEBUG -Wall -Wextra -Werror)
target_link_libraries(temperature_reader_test PRIVATE Threads::Threads)
add_test(NAME temperature_sensor_readings COMMAND temperature_reader_test)

find_program(AERA_NODE_EXECUTABLE node)
if(AERA_NODE_EXECUTABLE)
  add_test(NAME pc_snapshot_label COMMAND "${AERA_NODE_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/pc_snapshot_label_test.js")
endif()

find_package(Protobuf QUIET)
find_path(AERA_INCFS_HEADERS linux/incrementalfs.h
  HINTS "${CMAKE_CURRENT_SOURCE_DIR}/../../../../system/incremental_delivery/incfs/kernel-headers")
if(Protobuf_FOUND AND AERA_INCFS_HEADERS)
  add_executable(incremental_apks_test incremental_apks_test.cpp
    ../features/backup/incremental_apks.cpp)
  target_include_directories(incremental_apks_test PRIVATE "${AERA_INCFS_HEADERS}")
  target_compile_options(incremental_apks_test PRIVATE -UNDEBUG -fno-exceptions
    -Wall -Wextra -Werror)
  target_link_libraries(incremental_apks_test PRIVATE protobuf::libprotobuf-lite Threads::Threads)
  add_test(NAME incremental_apk_mapping_and_bind_records COMMAND incremental_apks_test)
endif()

foreach(target frp_wipe_enabled_test frp_wipe_disabled_test)
  add_executable(${target} frp_wipe_test.cpp)
  target_compile_options(${target} PRIVATE -UNDEBUG)
  add_test(NAME ${target} COMMAND ${target})
endforeach()
target_compile_definitions(frp_wipe_enabled_test PRIVATE AERA_ENABLE_FRP_WIPE=1)

add_executable(shared_storage_test shared_storage_test.cpp)
target_compile_options(shared_storage_test PRIVATE -UNDEBUG -Wall -Wextra -Werror)
add_test(NAME shared_storage_android_access COMMAND shared_storage_test)

add_executable(public_ip_test public_ip_test.cpp)
target_compile_options(public_ip_test PRIVATE -UNDEBUG -Wall -Wextra -Werror)
target_link_libraries(public_ip_test PRIVATE resolv Threads::Threads)
add_test(NAME mobile_public_ipv4_parsing COMMAND public_ip_test)
