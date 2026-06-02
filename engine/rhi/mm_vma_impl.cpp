// Copyright (c) 2025 Markmos
// SPDX-License-Identifier: MIT

// Single translation unit providing VMA implementation.
// Must be compiled exactly once when USE_VULKAN_BACKEND is defined.

#define VMA_IMPLEMENTATION
#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#include "../thirdparty/VMA/include/vk_mem_alloc.h"
