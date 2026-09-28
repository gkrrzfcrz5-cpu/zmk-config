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
  # ZMK exposes drivers/behavior.h and zmk/behavior.h only to its own `app`
  # target (app/CMakeLists.txt: target_include_directories(app PRIVATE include)),
  # so an out-of-tree module does not inherit them. Add ZMK's app/include dir
  # explicitly. APPLICATION_SOURCE_DIR resolves to <workspace>/zmk/app at build.
  zephyr_library_include_directories(${APPLICATION_SOURCE_DIR}/include)
endif()
