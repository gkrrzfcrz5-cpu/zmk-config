# Copyright (c) 2026 The AI Companion Contributors
# SPDX-License-Identifier: MIT
#
# Top-level CMake for the in-repo Zephyr module (detected by zephyr/module.yml,
# passed to the build as -DZMK_EXTRA_MODULES by build-user-config.yml@v0.3).
# Compiles our custom out-of-tree ZMK behaviors.
#
# Guarded by the Kconfig symbol so builds that don't include the behavior
# (e.g. the tester_xiao shield) don't create an empty zephyr_library.

if(CONFIG_ZMK_BEHAVIOR_HAPTIC)
  zephyr_library()
  zephyr_library_sources(src/behavior_haptic.c)

  # ZMK exposes drivers/behavior.h and zmk/behavior.h only to its own PRIVATE
  # `app` target (app/CMakeLists.txt: target_include_directories(app PRIVATE
  # include)), so an out-of-tree module does not inherit them. Locate ZMK's
  # app/include and add it explicitly. Verify the header actually exists at
  # configure time so a wrong path fails loudly here instead of at compile time.
  set(_zmk_app_include "")
  foreach(_cand
      "${APPLICATION_SOURCE_DIR}/include"
      "${ZEPHYR_MODULE_MODULE_DIR}/../include")
    if(EXISTS "${_cand}/drivers/behavior.h")
      set(_zmk_app_include "${_cand}")
      break()
    endif()
  endforeach()
  message(STATUS "ai_companion haptic: APPLICATION_SOURCE_DIR=${APPLICATION_SOURCE_DIR}")
  message(STATUS "ai_companion haptic: resolved ZMK app include=${_zmk_app_include}")
  if(_zmk_app_include)
    zephyr_library_include_directories("${_zmk_app_include}")
  else()
    message(WARNING "ai_companion haptic: could not locate ZMK app/include; "
                    "<drivers/behavior.h> will not resolve")
  endif()
endif()
