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
endif()
