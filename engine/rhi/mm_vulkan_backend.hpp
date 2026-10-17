// Copyright (c) 2025 Markmos
// SPDX-License-Identifier: MIT

#pragma once
#include "../core/mm_expected.hpp"
#include "core/mm_types.h"
#include "../core/mm_handle.hpp"
#include "../core/mm_log.hpp"
#include "../core/mm_slotmap.hpp"
#include "mm_rhi_concept.hpp"
#include <cstddef>
#include <cstdint>
#include <vector>

// Vulkan Backend — vk-bootstrap + VMA, zero manual vkCreateInstance
// Cache reason:
//   - No per-frame vkAllocateMemory (delegated to VMA)
//   - vk-bootstrap reduces 500+ lines of boilerplate to ~20
//   - VMA pools for device-local + host-visible heaps
//   - VkPipelineCache for faster PSO creation
// Design:
//   - Vulkan 1.3 Dynamic Rendering (no VkRenderPass legacy)
//   - Timeline Semaphores for GPU-GPU sync (no vkDeviceWaitIdle)
//   - VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE for GPU-only resources
//   - VMA_MEMORY_USAGE_AUTO_PREFER_HOST for staging

#if defined(USE_VULKAN_BACKEND)

#    include "../thirdparty/VMA/include/vk_mem_alloc.h"
#    include "../thirdparty/vk-bootstrap/VkBootstrap.h"
#    include <vulkan/vulkan.h>
#    include <cstring>

// ─── Push-descriptor compatibility shim ───────────────────────────────────────
// VK_KHR_push_descriptor was promoted to Vulkan 1.2 core, but the Vulkan header
// the NDK ships (VK_HEADER_VERSION 275 in NDK 27) declares neither the core
// entry point nor the 1.2 feature bit — only the extension NAME macro, which is
// not enough to call anything. Declaring what we need here keeps this backend
// buildable against that header and against newer ones, where the names are
// spelled differently, by using names of our own that cannot collide.
//
// The feature is therefore queried and enabled through the KHR struct rather
// than VkPhysicalDeviceVulkan12Features, which means the device must actually
// advertise the extension — checked in init() before anything is requested.
#ifndef VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PUSH_DESCRIPTOR_FEATURES_KHR
#    define VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PUSH_DESCRIPTOR_FEATURES_KHR ((VkStructureType)1000545001)
#endif

#if defined(VK_USE_PLATFORM_ANDROID_KHR)
#    include <android/native_window.h>  // ANativeWindow_getWidth/Height
#endif

struct MmPushDescriptorFeatures {
    VkStructureType sType;
    void          *pNext;
    VkBool32       pushDescriptor;
};

using MmCmdPushDescriptorSets = void(VKAPI_PTR *)(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint, VkPipelineLayout layout, u32 set,
                                                 u32 descriptorWriteCount, const VkWriteDescriptorSet *pDescriptorWrites);

// Also loaded by name: the NDK's reduced header/stub does not export
// vkGetPhysicalDeviceFeatures2 (Vulkan 1.1 core), so linking it directly fails.
using MmGetPhysicalDeviceFeatures2 = void(VKAPI_PTR *)(VkPhysicalDevice physicalDevice, VkPhysicalDeviceFeatures2 *pFeatures);

struct VulkanBuffer {
    VkBuffer      buffer;
    VmaAllocation alloc;
    u32      size;
    BufferType    type;
};

struct VulkanTexture {
    VkImage       image;
    VmaAllocation alloc;
    VkImageView   view;
    u32      width, height;
    PixelFormat   format;
};

struct VulkanPipeline {
    VkPipeline            pipeline;
    VkPipelineLayout      layout;
    VkDescriptorSetLayout desc_set_layout;
    VkDescriptorSet       desc_set;
    VkImageView           current_view;
    VkSampler             current_sampler;
    VkBuffer              current_ubos[8];

    // The bindings this pipeline's layout actually declares, in layout order.
    // A push must cover exactly the declared bindings — pushing an undeclared one
    // is a validation error, omitting a declared one leaves the shader reading an
    // undefined descriptor (i.e. white).
    u8                     desc_binding_count = 0;
    u32                    desc_binding_index[8]{};
    DescriptorType         desc_binding_type[8]{};

    // One descriptor set per (texture, sampler) this pipeline has actually drawn.
    //
    // WHY NOT ONE SET FOR THE WHOLE PIPELINE: vkUpdateDescriptorSets mutates a
    // set immediately, but the draws that use it execute later, at
    // vkQueueSubmit. One set therefore cannot describe two textures drawn in the
    // same frame — every draw reads whatever the LAST write left there. That is
    // why cards rendered white on Vulkan and fine on Metal (setFragmentTexture:
    // is recorded per draw; this recorded nothing at all), and why the one card
    // that DID show its face was the selected one: it goes through
    // rounded_sprite_glow_pipeline, whose only texture is the atlas, so nothing
    // ever overwrote it.
    //
    // Push descriptors solve this outright (see flush_descriptors), but
    // VK_KHR_push_descriptor is not universally available — the Android emulator
    // does not advertise it — so this is the portable path.
    struct DescSlot {
        VkImageView      view = VK_NULL_HANDLE;
        VkSampler        sampler = VK_NULL_HANDLE;
        VkBuffer         ubos[8]{};
        VkDescriptorSet  set = VK_NULL_HANDLE;
        bool             ubos_written = false;
    };
    // One slot per (pipeline, texture) pair that draws in a frame. 8 was tight
    // enough that the shader lab's 9 distinct pairs fell through to the shared
    // slot on every draw — correct, but it means a wrong texture. The pool
    // allows 512 sets (kDescPoolMaxSets) and a slot is ~96 bytes, so the
    // headroom is free. Past this, flush_descriptors degrades to slot 0 and
    // says so once, rather than reallocating per draw.
    static constexpr u32 MAX_DESC_SLOTS = 32;
    DescSlot desc_slots[MAX_DESC_SLOTS]{};
    u32      desc_slot_count = 0;
};

struct VulkanSampler {
    VkSampler sampler;
};

struct VulkanBackend {
    // vk-bootstrap handles
    vkb::Instance              vkb_instance;
    vkb::Device                vkb_device;
    vkb::Swapchain             vkb_swapchain;

    VkInstance                 instance;
    VkDevice                   device;
    VkPhysicalDevice           phys_device;
    bool                       sampler_aniso_supported = false; // queried at init; gates anisotropyEnable
    // Push descriptors (VK_KHR_push_descriptor, core in Vulkan 1.2). Queried at
    // init and loaded through vkGetDeviceProcAddr so a device without it falls
    // back instead of failing to link. See flush_descriptors() for why the whole
    // mechanism exists.
    bool                       push_descriptors_supported = false;
    MmCmdPushDescriptorSets      pfn_push_descriptor_sets  = nullptr;
    // Stand-in buffer for a declared UBO binding that nothing has bound yet.
    // Push descriptors have to cover every declared binding, and a null VkBuffer
    // in a descriptor is undefined rather than merely empty.
    VkBuffer                   desc_fallback_ubo     = VK_NULL_HANDLE;
    VmaAllocation              desc_fallback_ubo_alloc = VK_NULL_HANDLE;
    VkQueue                    graphics_queue;
    VkQueue                    present_queue;
    u32                   graphics_family;
    u32                   present_family;

    // VMA allocator
    VmaAllocator               allocator;

    // Sync
    VkSemaphore                acquire_sem;
    VkSemaphore                release_sem;
    VkFence                    frame_fence;
    VkCommandPool              cmd_pool;
    VkCommandBuffer            cmd_buf;
    u64                   timeline_value;

    // Surface + Swapchain
    VkSurfaceKHR               surface;
    VkSwapchainKHR             swapchain;
    VkExtent2D                 swap_extent;
    VkFormat                   swap_format;
    std::vector<VkImage>       swap_images;
    std::vector<VkImageView>   swap_views;
    u32                   swap_index;

    // Slotmaps
    Slotmap<VulkanBuffer>      buffers;
    Slotmap<VulkanTexture>     textures;
    Slotmap<VulkanPipeline>    pipelines;
    Slotmap<VulkanSampler>     samplers;

    // Descriptor pool
    VkDescriptorPool           desc_pool;

    // Currently bound pipeline (for descriptor updates)
    PipelineHandle             current_pipeline_handle;

    // Dynamic rendering
    PFN_vkCmdBeginRenderingKHR vkCmdBeginRenderingKHR;
    PFN_vkCmdEndRenderingKHR   vkCmdEndRenderingKHR;

    u32                   frame_index;

    Expected<void, RHIError>   init(void *window_handle) noexcept {
        if (instance != VK_NULL_HANDLE) {
            MM_LOG("VulkanBackend::init() - Already initialized (Instance: %p), skipping vkb setup.", (void *)instance);
            return {};
        }
        // Step 1: vkb::InstanceBuilder — no manual vkCreateInstance
        vkb::InstanceBuilder inst_builder;
        inst_builder.set_app_name("Markmos").set_engine_name("Markmos Engine").require_api_version(1, 2, 0);

#    if !defined(NDEBUG) && !defined(VK_USE_PLATFORM_ANDROID_KHR)
        // Validation layers are often missing on Android devices, causing instance creation to fail.
        // We only enable them on desktop/other platforms for now.
        inst_builder.request_validation_layers().use_default_debug_messenger();
#    endif

        auto inst_ret = inst_builder.build();
        if (!inst_ret) {
            // Fallback to Vulkan 1.0 if 1.1 is not available or if extensions failed
            inst_builder.require_api_version(1, 0, 0);
            inst_ret = inst_builder.build();
        }

        if (!inst_ret) {
            // If it still fails, try one last time with absolutely no extras
            vkb::InstanceBuilder simple_builder;
            inst_ret = simple_builder.set_app_name("Markmos").require_api_version(1, 0, 0).build();
        }

        if (!inst_ret) {
            MM_ERROR("Failed to create Vulkan instance: %s", inst_ret.error().message().c_str());
            return make_unexpected(RHIError::BackendError);
        }

        vkb_instance = inst_ret.value();
        instance     = vkb_instance.instance;

        // Step 2: Surface creation (platform-specific)
        surface      = VK_NULL_HANDLE;
#    if defined(VK_USE_PLATFORM_ANDROID_KHR)
        MM_LOG("Creating Android Surface (window_handle: %p)", window_handle);
        if (window_handle) {
            VkAndroidSurfaceCreateInfoKHR sci{};
            sci.sType  = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR;
            sci.window = static_cast<ANativeWindow *>(window_handle);
            if (vkCreateAndroidSurfaceKHR(instance, &sci, nullptr, &surface) != VK_SUCCESS) {
                MM_ERROR("vkCreateAndroidSurfaceKHR failed");
                vkb::destroy_instance(vkb_instance);
                return make_unexpected(RHIError::BackendError);
            }
            MM_LOG("Android Surface created: %p", surface);
        }
#    endif

        // Step 3: Select physical device
        MM_LOG("Selecting Physical Device");

        vkb::PhysicalDeviceSelector phys_dev_selector(vkb_instance);
        auto                        phys_dev_ret = phys_dev_selector.set_minimum_version(1, 2)
                                .set_surface(surface) // Pass surface explicitly to ensure presentation support
                                .add_required_extension(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME)
                                .select();
        if (!phys_dev_ret) {
            MM_ERROR("Failed to select physical device: %s", phys_dev_ret.error().message().c_str());
            vkb::destroy_instance(vkb_instance);
            return make_unexpected(RHIError::BackendError);
        }
        vkb::PhysicalDevice physical_device = phys_dev_ret.value();
        phys_device                         = physical_device.physical_device;
        MM_LOG("Physical device selected: %s", physical_device.name.c_str());

        // Vertex colors ride on B8G8R8A8_UNORM (see to_vk_vertex_format) —
        // warn (don't fail) if the device can't bind it as a vertex format.
        {
            VkFormatProperties fp{};
            vkGetPhysicalDeviceFormatProperties(phys_device, VK_FORMAT_B8G8R8A8_UNORM, &fp);
            if ((fp.bufferFeatures & VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT) == 0) {
                MM_ERROR("B8G8R8A8_UNORM lacks VERTEX_BUFFER support — vertex colors will render R/B-swapped");
            }
        }

        // Step 4: vkb::DeviceBuilder — no manual vkCreateDevice
        MM_LOG("Creating Logical Device");

        // Dynamic rendering features
        VkPhysicalDeviceDynamicRenderingFeaturesKHR dynamic_rendering_features{};
        dynamic_rendering_features.sType            = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES_KHR;
        dynamic_rendering_features.dynamicRendering = VK_TRUE;

        // Vulkan 1.2 features
        VkPhysicalDeviceVulkan12Features features12{};
        features12.sType               = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
        features12.timelineSemaphore   = VK_TRUE;
        features12.bufferDeviceAddress = VK_TRUE;
        features12.descriptorIndexing  = VK_TRUE;
        features12.pNext               = &dynamic_rendering_features;

        // Push descriptors. Asked through the KHR feature struct because the header
        // has no 1.2 feature bit for it, which means the EXTENSION has to be one the
        // device advertises — not merely the functionality, which on a 1.2 driver
        // may only be reachable through the core struct.
        //
        // Requesting a feature the device lacks is a validation-layer error, and
        // this build runs without validation layers on device, so the request would
        // "succeed" and the driver would then behave unpredictably.
        bool ext_advertised = false;
        u32  ext_total    = 0;
        bool saw_known    = false;
        {
            // Enumerated directly rather than read off vkb::PhysicalDevice:
            // available_extensions is private there, and the extension list is a
            // property of the physical device, not of the bootstrap wrapper.
            vkEnumerateDeviceExtensionProperties(phys_device, nullptr, &ext_total, nullptr);
            std::vector<VkExtensionProperties> exts(ext_total);
            if (ext_total > 0) {
                vkEnumerateDeviceExtensionProperties(phys_device, nullptr, &ext_total, exts.data());
            }
            for (const auto &e : exts) {
                if (std::strcmp(e.extensionName, VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME) == 0) {
                    ext_advertised = true;
                }
                // Cross-check that enumeration itself works: dynamic rendering is
                // required to get this far, so its absence means the list is empty
                // for some other reason and "absent" would be a lie.
                if (std::strcmp(e.extensionName, VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME) == 0) {
                    saw_known = true;
                }
            }
        }
        if (!saw_known) {
            MM_ERROR("push descriptors: extension enumeration returned %u entries and none is %s — treat the 'absent' below as suspect", ext_total,
                     VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
        }

        bool feature_available = false;
        if (ext_advertised) {
            MmPushDescriptorFeatures avail_push{};
            avail_push.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PUSH_DESCRIPTOR_FEATURES_KHR;

            VkPhysicalDeviceVulkan12Features avail12{};
            avail12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
            avail12.pNext = &avail_push;

            VkPhysicalDeviceFeatures2 avail2{};
            avail2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
            avail2.pNext = &avail12;

            auto pfn_get_features2 = reinterpret_cast<MmGetPhysicalDeviceFeatures2>(vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceFeatures2"));
            if (pfn_get_features2) {
                pfn_get_features2(phys_device, &avail2);
                feature_available = (avail_push.pushDescriptor == VK_TRUE);
            } else {
                MM_ERROR("vkGetPhysicalDeviceFeatures2 unavailable — cannot verify the pushDescriptor feature, treating it as absent");
            }
        }

        // Chain it in only when it is actually wanted, so a device without the
        // feature never sees the struct at device creation.
        MmPushDescriptorFeatures push_features{};
        push_features.sType          = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PUSH_DESCRIPTOR_FEATURES_KHR;
        push_features.pushDescriptor = feature_available ? VK_TRUE : VK_FALSE;
        if (feature_available) {
            dynamic_rendering_features.pNext = &push_features;
        }

        MM_LOG("push descriptors: extension %s, feature %s", ext_advertised ? "advertised" : "absent", feature_available ? "yes" : "no");

        VkPhysicalDeviceFeatures2 features2{};
        features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        features2.pNext = &features12;

        // Anisotropy (minified-atlas cleanup): enable the device feature
        // only when the physical device offers it; create_sampler checks
        // this flag before setting anisotropyEnable.
        VkPhysicalDeviceFeatures avail_features{};
        vkGetPhysicalDeviceFeatures(phys_device, &avail_features);
        sampler_aniso_supported                  = (avail_features.samplerAnisotropy == VK_TRUE);
        features2.features.samplerAnisotropy     = sampler_aniso_supported ? VK_TRUE : VK_FALSE;

        vkb::DeviceBuilder device_builder(physical_device);
        auto               dev_ret = device_builder.add_pNext(&features2).build();
        if (!dev_ret) {
            MM_ERROR("Failed to create logical device: %s", dev_ret.error().message().c_str());
            vkb::destroy_instance(vkb_instance);
            return make_unexpected(RHIError::BackendError);
        }

        vkb_device = dev_ret.value();
        device     = vkb_device.device;
        MM_LOG("Logical device created");

        // Get queues
        auto gq = vkb_device.get_queue(vkb::QueueType::graphics);
        auto pq = vkb_device.get_queue(vkb::QueueType::present);
        if (!gq || !pq) {
            vkb::destroy_device(vkb_device);
            vkb::destroy_instance(vkb_instance);
            return make_unexpected(RHIError::BackendError);
        }
        graphics_queue  = gq.value();
        present_queue   = pq.value();

        auto gi         = vkb_device.get_queue_index(vkb::QueueType::graphics);
        auto pi         = vkb_device.get_queue_index(vkb::QueueType::present);
        graphics_family = gi.value();
        present_family  = pi.value();

        // Step 4: Swapchain via vkb::SwapchainBuilder
        MM_LOG("Creating Swapchain");
        vkb::SwapchainBuilder swap_builder(vkb_device, surface);
        auto                  swap_ret = swap_builder.set_desired_format({VK_FORMAT_B8G8R8A8_SRGB, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR})
                            .set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR)
                            .build();
        if (!swap_ret) {
            MM_ERROR("Failed to create swapchain: %s", swap_ret.error().message().c_str());
            vkb::destroy_device(vkb_device);
            vkb::destroy_instance(vkb_instance);
            return make_unexpected(RHIError::BackendError);
        }
        vkb_swapchain = swap_ret.value();
        swapchain     = vkb_swapchain.swapchain;
        swap_extent   = vkb_swapchain.extent;
        swap_format   = vkb_swapchain.image_format;
        MM_LOG("Swapchain created: %dx%d", swap_extent.width, swap_extent.height);

        swap_images = vkb_swapchain.get_images().value();
        swap_views  = vkb_swapchain.get_image_views().value();

        // Step 5: VMA — all allocations through VMA, no vkAllocateMemory
        MM_LOG("Creating VMA Allocator");
        VmaVulkanFunctions vma_funcs{};
        vma_funcs.vkGetInstanceProcAddr               = vkGetInstanceProcAddr;
        vma_funcs.vkGetDeviceProcAddr                 = vkGetDeviceProcAddr;
        vma_funcs.vkGetPhysicalDeviceProperties       = vkGetPhysicalDeviceProperties;
        vma_funcs.vkGetPhysicalDeviceMemoryProperties = vkGetPhysicalDeviceMemoryProperties;
        vma_funcs.vkAllocateMemory                    = vkAllocateMemory;
        vma_funcs.vkFreeMemory                        = vkFreeMemory;
        vma_funcs.vkMapMemory                         = vkMapMemory;
        vma_funcs.vkUnmapMemory                       = vkUnmapMemory;
        vma_funcs.vkFlushMappedMemoryRanges           = vkFlushMappedMemoryRanges;
        vma_funcs.vkInvalidateMappedMemoryRanges      = vkInvalidateMappedMemoryRanges;
        vma_funcs.vkBindBufferMemory                  = vkBindBufferMemory;
        vma_funcs.vkBindImageMemory                   = vkBindImageMemory;
        vma_funcs.vkGetBufferMemoryRequirements       = vkGetBufferMemoryRequirements;
        vma_funcs.vkGetImageMemoryRequirements        = vkGetImageMemoryRequirements;
        vma_funcs.vkCreateBuffer                      = vkCreateBuffer;
        vma_funcs.vkDestroyBuffer                     = vkDestroyBuffer;
        vma_funcs.vkCreateImage                       = vkCreateImage;
        vma_funcs.vkDestroyImage                      = vkDestroyImage;
        vma_funcs.vkCmdCopyBuffer                     = vkCmdCopyBuffer;

        VmaAllocatorCreateInfo alloc_info{};
        alloc_info.device           = device;
        alloc_info.physicalDevice   = phys_device;
        alloc_info.instance         = instance;
        alloc_info.pVulkanFunctions = &vma_funcs;
        alloc_info.vulkanApiVersion = VK_API_VERSION_1_2;
        VkResult vma_res            = vmaCreateAllocator(&alloc_info, &allocator);
        if (vma_res != VK_SUCCESS) {
            MM_ERROR("Failed to create VMA allocator, result: %d", (int)vma_res);
            for (auto v : swap_views) {
                vkDestroyImageView(device, v, nullptr);
            }
            swap_views.clear();
            vkb::destroy_swapchain(vkb_swapchain);
            vkb::destroy_device(vkb_device);
            if (surface) {
                vkDestroySurfaceKHR(instance, surface, nullptr);
            }
            vkb::destroy_instance(vkb_instance);
            return make_unexpected(RHIError::BackendError);
        }
        MM_LOG("VMA allocator created");

        // Step 6: Sync objects
        VkSemaphoreCreateInfo sem_info{};
        sem_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        vkCreateSemaphore(device, &sem_info, nullptr, &acquire_sem);
        vkCreateSemaphore(device, &sem_info, nullptr, &release_sem);

        VkFenceCreateInfo fence_info{};
        fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        vkCreateFence(device, &fence_info, nullptr, &frame_fence);

        // Command pool
        VkCommandPoolCreateInfo pool_info{};
        pool_info.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        pool_info.queueFamilyIndex = graphics_family;
        pool_info.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        vkCreateCommandPool(device, &pool_info, nullptr, &cmd_pool);

        // Command buffer
        VkCommandBufferAllocateInfo alloc_cmd{};
        alloc_cmd.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        alloc_cmd.commandPool        = cmd_pool;
        alloc_cmd.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        alloc_cmd.commandBufferCount = 1;
        vkAllocateCommandBuffers(device, &alloc_cmd, &cmd_buf);

        // Load dynamic rendering functions
        vkCmdBeginRenderingKHR = reinterpret_cast<PFN_vkCmdBeginRenderingKHR>(vkGetDeviceProcAddr(device, "vkCmdBeginRenderingKHR"));
        vkCmdEndRenderingKHR   = reinterpret_cast<PFN_vkCmdEndRenderingKHR>(vkGetDeviceProcAddr(device, "vkCmdEndRenderingKHR"));

        // Push descriptors are loaded by name rather than called directly: the header
        // this builds against may declare neither spelling, and the feature flag
        // above is what says the device has it while this is what says the entry
        // point is reachable. If the feature is on but the pointer is null, treat
        // it as unsupported rather than crash later.
        if (feature_available) {
            pfn_push_descriptor_sets = reinterpret_cast<MmCmdPushDescriptorSets>(vkGetDeviceProcAddr(device, "vkCmdPushDescriptorSetsKHR"));
            if (!pfn_push_descriptor_sets) {
                pfn_push_descriptor_sets = reinterpret_cast<MmCmdPushDescriptorSets>(vkGetDeviceProcAddr(device, "vkCmdPushDescriptorSets"));
            }
            push_descriptors_supported = (pfn_push_descriptor_sets != nullptr);
            MM_LOG("push descriptors: %s", push_descriptors_supported ? "enabled" : "entry point missing — using descriptor sets");
        } else {
            MM_LOG("push descriptors: unavailable — using one descriptor set per (pipeline, texture)");
        }

        // Stand-in UBO for a declared binding nothing has bound. Push descriptors
        // must cover every declared binding, and a null buffer in a descriptor is
        // undefined rather than empty.
        {
            VkBufferCreateInfo fbi{};
            fbi.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            fbi.size        = 256;
            fbi.usage       = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
            VmaAllocationCreateInfo fai{};
            fai.usage       = VMA_MEMORY_USAGE_GPU_ONLY;
            VkResult fres   = vmaCreateBuffer(allocator, &fbi, &fai, &desc_fallback_ubo, &desc_fallback_ubo_alloc, nullptr);
            if (fres != VK_SUCCESS) {
                desc_fallback_ubo = VK_NULL_HANDLE;
                MM_ERROR("push descriptors: fallback UBO alloc failed (%d) — a pipeline with an unbound UBO will sample garbage", (int)fres);
            }
        }

        if (!vkCmdBeginRenderingKHR || !vkCmdEndRenderingKHR) {
            MM_ERROR("Failed to load vkCmdBeginRenderingKHR or vkCmdEndRenderingKHR");
            for (auto v : swap_views) {
                vkDestroyImageView(device, v, nullptr);
            }
            swap_views.clear();
            vkb::destroy_swapchain(vkb_swapchain);
            vkb::destroy_device(vkb_device);
            if (surface) {
                vkDestroySurfaceKHR(instance, surface, nullptr);
            }
            vkb::destroy_instance(vkb_instance);
            vkDestroyCommandPool(device, cmd_pool, nullptr);
            vkDestroyFence(device, frame_fence, nullptr);
            vkDestroySemaphore(device, acquire_sem, nullptr);
            vkDestroySemaphore(device, release_sem, nullptr);
            vkb::destroy_device(vkb_device);
            vkb::destroy_instance(vkb_instance);

            return make_unexpected(RHIError::BackendError);
        }

        // Descriptor pool.
        //
        // Every pipeline that has a descriptor set layout allocates ONE set from
        // this pool when it is created, and holds it until the pipeline is
        // destroyed (vkFreeDescriptorSets in destroy()). So the pool must hold
        // the peak number of SIMULTANEOUSLY LIVE sets, not the average.
        //
        // This was 16, which is smaller than the 23 pipelines create_default_pipelines()
        // makes at startup, so the default set could never fit. It usually worked
        // anyway and failed sometimes — observed on an Android emulator as
        // "create_pipeline: vkAllocateDescriptorSets failed" followed by a Scudo
        // abort in the error path, at plastic_pipeline (index 16) on one run and
        // not at all on the next. Metal has no equivalent per-device limit,
        // which is why no desktop build ever hit it.
        //
        // Sized for the per-(pipeline, texture) sets in VulkanPipeline::desc_slots, not
        // for pipelines. Each slot costs one combined-image-sampler plus one
        // uniform buffer per bound UBO, and slots are freed with their pipeline
        // (FREE_DESCRIPTOR_SET_BIT), so the live count tracks distinct
        // (pipeline, texture) pairs rather than the pipeline count. Still
        // sub-allocated from device memory, so the headroom is a few KB.
        static constexpr u32 kDescPoolMaxSets       = 512;
        static constexpr u32 kDescPoolDescriptors   = 1024; // per type

        VkDescriptorPoolSize pool_sizes[2] = {};
        pool_sizes[0].type                 = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        pool_sizes[0].descriptorCount      = kDescPoolDescriptors;
        pool_sizes[1].type                 = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        pool_sizes[1].descriptorCount      = kDescPoolDescriptors;

        VkDescriptorPoolCreateInfo dpci{};
        dpci.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        dpci.maxSets       = kDescPoolMaxSets;
        dpci.poolSizeCount = 2;
        dpci.pPoolSizes    = pool_sizes;
        dpci.flags         = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        vkCreateDescriptorPool(device, &dpci, nullptr, &desc_pool);

        timeline_value = 0;
        frame_index    = 0;

        return {};
    }

    void shutdown() noexcept {
        vkDeviceWaitIdle(device);

        // Destroy all remaining resources in slotmaps first
        {
            auto it = buffers.iter();
            while (auto *b = it.next()) {
                vmaDestroyBuffer(allocator, b->buffer, b->alloc);
            }
        }

        {
            auto it = textures.iter();
            while (auto *t = it.next()) {
                vkDestroyImageView(device, t->view, nullptr);
                vmaDestroyImage(allocator, t->image, t->alloc);
            }
        }

        {
            auto it = pipelines.iter();
            while (auto *p = it.next()) {
                if (p->desc_set) {
                    vkFreeDescriptorSets(device, desc_pool, 1, &p->desc_set);
                }
                for (u32 i = 0; i < p->desc_slot_count; ++i) {
                    if (p->desc_slots[i].set) {
                        vkFreeDescriptorSets(device, desc_pool, 1, &p->desc_slots[i].set);
                        p->desc_slots[i].set = VK_NULL_HANDLE;
                    }
                }
                if (p->desc_set_layout) {
                    vkDestroyDescriptorSetLayout(device, p->desc_set_layout, nullptr);
                }
                vkDestroyPipeline(device, p->pipeline, nullptr);
                vkDestroyPipelineLayout(device, p->layout, nullptr);
            }
        }

        {
            auto it = samplers.iter();
            while (auto *s = it.next()) {
                vkDestroySampler(device, s->sampler, nullptr);
            }
        }

        if (desc_fallback_ubo) {
            vmaDestroyBuffer(allocator, desc_fallback_ubo, desc_fallback_ubo_alloc);
            desc_fallback_ubo        = VK_NULL_HANDLE;
            desc_fallback_ubo_alloc  = VK_NULL_HANDLE;
        }

        vkDestroyFence(device, frame_fence, nullptr);
        frame_fence = VK_NULL_HANDLE;
        vkDestroySemaphore(device, acquire_sem, nullptr);
        acquire_sem = VK_NULL_HANDLE;
        vkDestroySemaphore(device, release_sem, nullptr);
        release_sem = VK_NULL_HANDLE;
        vkDestroyCommandPool(device, cmd_pool, nullptr);
        cmd_pool = VK_NULL_HANDLE;

        vkDestroyDescriptorPool(device, desc_pool, nullptr);
        desc_pool = VK_NULL_HANDLE;
        for (auto v : swap_views) {
            vkDestroyImageView(device, v, nullptr);
        }
        swap_views.clear();
        swap_images.clear();

        vmaDestroyAllocator(allocator);
        allocator = VK_NULL_HANDLE;
        if (surface) {
            vkDestroySurfaceKHR(instance, surface, nullptr);
            surface = VK_NULL_HANDLE;
        }
        vkb::destroy_swapchain(vkb_swapchain);
        vkb::destroy_device(vkb_device);
        vkb::destroy_instance(vkb_instance);

        instance       = VK_NULL_HANDLE;
        device         = VK_NULL_HANDLE;
        phys_device    = VK_NULL_HANDLE;
        graphics_queue = VK_NULL_HANDLE;
        present_queue  = VK_NULL_HANDLE;
        swapchain      = VK_NULL_HANDLE;
        cmd_buf        = VK_NULL_HANDLE;
        vkb_swapchain  = {};
        vkb_device     = {};
        vkb_instance   = {};
    }

    Expected<BufferHandle, RHIError> create_buffer(const BufferDesc &desc) noexcept {
        // MM_LOG("create_buffer entering: type=%d size=%u cpu_visible=%d", (int)desc.type, (u32)desc.size, (int)desc.cpu_visible);
        if (!allocator) {
            MM_ERROR("create_buffer: allocator is NULL!");
            return make_unexpected(RHIError::BackendError);
        }

        // MM_LOG("create_buffer: allocator=%p, device=%p", (void *)allocator, (void *)device);
        VkBufferCreateInfo buf_info{};
        buf_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        buf_info.size  = desc.size;
        buf_info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;

        switch (desc.type) {
        case BufferType::Vertex:
            buf_info.usage |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
            break;
        case BufferType::Index:
            buf_info.usage |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
            break;
        case BufferType::Uniform:
            buf_info.usage |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
            break;
        case BufferType::Storage:
            buf_info.usage |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
            break;
        case BufferType::Indirect:
            buf_info.usage |= VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
            break;
        default:
            break;
        }
        if (desc.cpu_visible) {
            buf_info.usage |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        }

        // MM_LOG("create_buffer: setting up VmaAllocationCreateInfo");
        VmaAllocationCreateInfo alloc_info{};
        // Use older enums for better emulator compatibility
        alloc_info.usage    = desc.cpu_visible ? VMA_MEMORY_USAGE_CPU_TO_GPU : VMA_MEMORY_USAGE_GPU_ONLY;
        alloc_info.flags    = desc.cpu_visible ? VMA_ALLOCATION_CREATE_MAPPED_BIT : 0;

        VkBuffer      buf   = VK_NULL_HANDLE;
        VmaAllocation alloc = VK_NULL_HANDLE;

        // MM_LOG("create_buffer: calling vmaCreateBuffer (device=%p)", (void *)device);
        VkResult      res   = vmaCreateBuffer(allocator, &buf_info, &alloc_info, &buf, &alloc, nullptr);
        if (res != VK_SUCCESS) {
            MM_ERROR("create_buffer: vmaCreateBuffer failed with result %d", (int)res);
            return make_unexpected(RHIError::OutOfMemory);
        }
        // MM_LOG("create_buffer: vmaCreateBuffer OK: buffer=%p, alloc=%p", (void *)buf, (void *)alloc);

        VulkanBuffer vb{buf, alloc, (u32)desc.size, desc.type};

        // MM_LOG("create_buffer: emplace into slotmap");
        SlotHandle   sh = buffers.emplace(vb);
        // MM_LOG("create_buffer: emplace OK: id=%u, gen=%u", sh.id, sh.gen);

        return BufferHandle{sh};
    }

    Expected<TextureHandle, RHIError> create_texture(const TextureDesc &desc) noexcept {
        // MM_LOG("create_texture entering: %ux%d fmt=%d", desc.width, desc.height, (int)desc.format);
        VkImageCreateInfo img_info{};
        img_info.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        img_info.imageType     = VK_IMAGE_TYPE_2D;
        img_info.format        = to_vk_format(desc.format);
        img_info.extent        = {desc.width, desc.height, 1};
        img_info.mipLevels     = desc.mip_levels;
        img_info.arrayLayers   = desc.array_layers;
        img_info.samples       = VK_SAMPLE_COUNT_1_BIT;
        img_info.tiling        = VK_IMAGE_TILING_OPTIMAL;
        img_info.usage         = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        img_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        VmaAllocationCreateInfo alloc_info{};
        // Use more compatible GPU usage
        alloc_info.usage    = VMA_MEMORY_USAGE_GPU_ONLY;

        VkImage       img   = VK_NULL_HANDLE;
        VmaAllocation alloc = VK_NULL_HANDLE;
        // MM_LOG("create_texture: calling vmaCreateImage");
        VkResult      res   = vmaCreateImage(allocator, &img_info, &alloc_info, &img, &alloc, nullptr);
        if (res != VK_SUCCESS) {
            MM_ERROR("create_texture: vmaCreateImage failed with result %d", (int)res);
            return make_unexpected(RHIError::OutOfMemory);
        }
        // MM_LOG("create_texture: vmaCreateImage OK: image=%p", (void *)img);

        // Create image view
        VkImageViewCreateInfo view_info{};
        view_info.sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view_info.image            = img;
        view_info.viewType         = VK_IMAGE_VIEW_TYPE_2D;
        view_info.format           = to_vk_format(desc.format);
        view_info.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, desc.mip_levels, 0, desc.array_layers};

        VkImageView view;
        if (vkCreateImageView(device, &view_info, nullptr, &view) != VK_SUCCESS) {
            MM_ERROR("create_texture: vkCreateImageView failed");
            vmaDestroyImage(allocator, img, alloc);
            return make_unexpected(RHIError::BackendError);
        }
        // MM_LOG("create_texture: vkCreateImageView OK: view=%p", (void *)view);

        VulkanTexture vt{img, alloc, view, desc.width, desc.height, desc.format};
        SlotHandle    sh = textures.emplace(vt);
        return TextureHandle{sh};
    }

    Expected<SamplerHandle, RHIError> create_sampler(const SamplerDesc &desc) noexcept {
        VkSamplerCreateInfo info{};
        info.sType         = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        info.magFilter     = desc.mag_filter == SamplerFilter::Linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
        info.minFilter     = desc.min_filter == SamplerFilter::Linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
        info.mipmapMode    = desc.mip_filter == SamplerFilter::Linear ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
        info.minLod        = 0.0f;
        info.maxLod        = 16.0f; // else only level 0 is ever sampled (default maxLod = 0)
        info.addressModeU  = to_vk_address(desc.address_u);
        info.addressModeV  = to_vk_address(desc.address_v);
        info.addressModeW  = to_vk_address(desc.address_w);
        info.anisotropyEnable = (desc.max_anisotropy > 1.0f && sampler_aniso_supported) ? VK_TRUE : VK_FALSE;
        info.maxAnisotropy = desc.max_anisotropy;
        info.compareOp     = to_vk_compare(desc.compare);

        VkSampler sampler;
        if (vkCreateSampler(device, &info, nullptr, &sampler) != VK_SUCCESS) {
            MM_ERROR("create_sampler: vkCreateSampler failed");
            return make_unexpected(RHIError::BackendError);
        }

        VulkanSampler vs{sampler};
        SlotHandle    sh = samplers.emplace(vs);
        return SamplerHandle{sh};
    }

    Expected<PipelineHandle, RHIError> create_pipeline(const PipelineDesc &desc) noexcept {
        // // MM_LOG("create_pipeline entering: vs_size=%zu fs_size=%zu", desc.vertex_shader.code_size, desc.fragment_shader.code_size);
        // --- Shader modules ---
        VkShaderModuleCreateInfo vsm{};
        vsm.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        vsm.codeSize = desc.vertex_shader.code_size;
        vsm.pCode    = static_cast<const u32 *>(desc.vertex_shader.code);

        VkShaderModule vs_module;
        if (vkCreateShaderModule(device, &vsm, nullptr, &vs_module) != VK_SUCCESS) {
            MM_ERROR("create_pipeline: vertex shader module creation failed");
            return make_unexpected(RHIError::ShaderCompileFail);
        }

        VkShaderModuleCreateInfo fsm{};
        fsm.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        fsm.codeSize = desc.fragment_shader.code_size;
        fsm.pCode    = static_cast<const u32 *>(desc.fragment_shader.code);

        VkShaderModule fs_module;
        if (vkCreateShaderModule(device, &fsm, nullptr, &fs_module) != VK_SUCCESS) {
            MM_ERROR("create_pipeline: fragment shader module creation failed");
            vkDestroyShaderModule(device, vs_module, nullptr);
            return make_unexpected(RHIError::ShaderCompileFail);
        }
        // MM_LOG("create_pipeline: shader modules created");

        // --- Shader stages ---
        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vs_module;
        stages[0].pName  = desc.vertex_shader.entry;

        stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = fs_module;
        stages[1].pName  = desc.fragment_shader.entry;

        // --- Vertex input ---
        VkVertexInputBindingDescription   bindings[16];
        VkVertexInputAttributeDescription attrs[16];
        u32                          binding_count = 0;
        u32                          attr_count    = desc.vertex_attr_count;

        // Group attributes by stride (binding)
        u32                          max_stride    = 0;
        for (u8 i = 0; i < attr_count; ++i) {
            auto &a           = desc.vertex_attrs[i];
            attrs[i].location = a.location;
            attrs[i].binding  = desc.is_instance ? 1 : 0;
            attrs[i].format   = to_vk_vertex_format(a.format);
            attrs[i].offset   = a.offset;
            if (a.stride > max_stride) {
                max_stride = a.stride;
            }
        }
        if (attr_count > 0) {
            bindings[0].binding = desc.is_instance ? 1 : 0;
            bindings[0].stride  = max_stride;
            // bindings[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
            if (desc.is_instance) {
                bindings[0].inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;
            } else {
                bindings[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
            }
            binding_count = 1;
        }

        // // MM_LOG("create_pipeline: vertex input - bindings=%u, attrs=%u", binding_count, attr_count);
        VkPipelineVertexInputStateCreateInfo vi{};
        vi.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vi.vertexBindingDescriptionCount   = binding_count;
        vi.pVertexBindingDescriptions      = bindings;
        vi.vertexAttributeDescriptionCount = attr_count;
        vi.pVertexAttributeDescriptions    = attrs;

        // --- Input assembly ---
        VkPipelineInputAssemblyStateCreateInfo ia{};
        ia.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        ia.topology = to_vk_primitive(desc.prim_type);

        // MM_LOG("create_pipeline: primitive topology = %d", (int)desc.prim_type);

        // --- Rasterization ---
        VkPipelineRasterizationStateCreateInfo rs{};
        rs.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rs.cullMode    = to_vk_cull(desc.cull_mode);
        rs.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rs.lineWidth   = 1.0f;
        rs.polygonMode = VK_POLYGON_MODE_FILL;

        // --- Viewport state (required even with dynamic viewport/scissor) ---
        VkPipelineViewportStateCreateInfo vs{};
        vs.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        vs.viewportCount = 1;
        vs.scissorCount  = 1;

        // --- Multisample ---
        VkPipelineMultisampleStateCreateInfo ms{};
        ms.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        // --- Color blend ---
        VkPipelineColorBlendAttachmentState cb_attachments[4]{};
        for (u8 i = 0; i < desc.color_count && i < 4; ++i) {
            cb_attachments[i].blendEnable         = VK_TRUE;
            cb_attachments[i].srcColorBlendFactor = to_vk_blend(desc.src_blend);
            cb_attachments[i].dstColorBlendFactor = to_vk_blend(desc.dst_blend);
            cb_attachments[i].colorBlendOp        = to_vk_blend_op(desc.blend_op);
            cb_attachments[i].srcAlphaBlendFactor = to_vk_blend(desc.src_blend);
            cb_attachments[i].dstAlphaBlendFactor = to_vk_blend(desc.dst_blend);
            cb_attachments[i].alphaBlendOp        = to_vk_blend_op(desc.blend_op);
            cb_attachments[i].colorWriteMask      = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        }

        VkPipelineColorBlendStateCreateInfo cb{};
        cb.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        cb.attachmentCount = desc.color_count;
        cb.pAttachments    = cb_attachments;

        // --- Depth-stencil ---
        VkPipelineDepthStencilStateCreateInfo ds{};
        ds.sType                                      = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        ds.depthTestEnable                            = desc.depth_test ? VK_TRUE : VK_FALSE;
        ds.depthWriteEnable                           = desc.depth_write ? VK_TRUE : VK_FALSE;
        ds.depthCompareOp                             = to_vk_compare(desc.depth_compare);
        ds.depthBoundsTestEnable                      = VK_FALSE;

        // --- Dynamic states ---
        VkDynamicState                   dyn_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dyn{};
        dyn.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dyn.dynamicStateCount = 2;
        dyn.pDynamicStates    = dyn_states;

        // --- Descriptor set layout ---
        VkDescriptorSetLayoutBinding dsl_bindings[8];
        for (u8 i = 0; i < desc.descriptor_count; ++i) {
            auto &db                           = desc.descriptor_bindings[i];
            dsl_bindings[i].binding            = db.binding;
            dsl_bindings[i].descriptorType     = to_vk_descriptor_type(db.type);
            dsl_bindings[i].descriptorCount    = db.count > 0 ? db.count : 1;
            dsl_bindings[i].stageFlags         = to_vk_shader_stage(db.stage_mask);
            dsl_bindings[i].pImmutableSamplers = nullptr;
        }

        VkDescriptorSetLayoutCreateInfo dsl{};
        dsl.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        dsl.bindingCount = desc.descriptor_count;
        dsl.pBindings    = dsl_bindings;

        VkDescriptorSetLayout desc_set_layout;
        if (dsl.bindingCount > 0) {
            if (vkCreateDescriptorSetLayout(device, &dsl, nullptr, &desc_set_layout) != VK_SUCCESS) {
                vkDestroyShaderModule(device, vs_module, nullptr);
                vkDestroyShaderModule(device, fs_module, nullptr);
                return make_unexpected(RHIError::BackendError);
            }
        } else {
            desc_set_layout = VK_NULL_HANDLE;
        }

        // --- Pipeline layout ---
        VkPipelineLayoutCreateInfo pl{};
        pl.sType          = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pl.setLayoutCount = (desc_set_layout != VK_NULL_HANDLE) ? 1 : 0;
        pl.pSetLayouts    = &desc_set_layout;

        VkPipelineLayout pipeline_layout;
        if (vkCreatePipelineLayout(device, &pl, nullptr, &pipeline_layout) != VK_SUCCESS) {
            if (desc_set_layout) {
                vkDestroyDescriptorSetLayout(device, desc_set_layout, nullptr);
            }
            vkDestroyShaderModule(device, vs_module, nullptr);
            vkDestroyShaderModule(device, fs_module, nullptr);
            return make_unexpected(RHIError::BackendError);
        }

        // --- Dynamic rendering format ---
        VkFormat color_fmts[4];
        for (u8 i = 0; i < desc.color_count && i < 4; ++i) {
            color_fmts[i] = to_vk_format(desc.color_formats[i]);
        }
        VkPipelineRenderingCreateInfo rd{};
        rd.sType                   = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
        rd.colorAttachmentCount    = desc.color_count;
        rd.pColorAttachmentFormats = color_fmts;
        rd.depthAttachmentFormat   = (desc.depth_format != static_cast<PixelFormat>(0)) ? to_vk_format(desc.depth_format) : VK_FORMAT_UNDEFINED;
        rd.stencilAttachmentFormat = VK_FORMAT_UNDEFINED;

        // --- Graphics pipeline ---
        VkGraphicsPipelineCreateInfo gp{};
        gp.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        gp.pNext               = &rd;
        gp.stageCount          = 2;
        gp.pStages             = stages;
        gp.pVertexInputState   = &vi;
        gp.pInputAssemblyState = &ia;
        gp.pViewportState      = &vs;
        gp.pRasterizationState = &rs;
        gp.pMultisampleState   = &ms;
        gp.pDepthStencilState  = &ds;
        gp.pColorBlendState    = &cb;
        gp.pDynamicState       = &dyn;
        gp.layout              = pipeline_layout;

        // MM_LOG("create_pipeline: creating graphics pipeline");
        VkPipeline      pipeline;
        VkPipelineCache cache = VK_NULL_HANDLE;
        VkResult        res   = vkCreateGraphicsPipelines(device, cache, 1, &gp, nullptr, &pipeline);
        vkDestroyShaderModule(device, vs_module, nullptr);
        vkDestroyShaderModule(device, fs_module, nullptr);

        if (res != VK_SUCCESS) {
            MM_ERROR("create_pipeline: vkCreateGraphicsPipelines failed with result %d", (int)res);
            if (desc_set_layout) {
                vkDestroyDescriptorSetLayout(device, desc_set_layout, nullptr);
            }
            vkDestroyPipelineLayout(device, pipeline_layout, nullptr);
            return make_unexpected(RHIError::PipelineCompileFail);
        }

        // MM_LOG("create_pipeline: graphics pipeline created successfully");
        // No descriptor set is allocated here: sets are per (pipeline, texture) and are
        // created on first use by flush_descriptors(), because allocating one per
        // pipeline is exactly the assumption that made every draw share the same
        // texture. See VulkanPipeline::desc_slots.
        VulkanPipeline vp{};
        vp.pipeline         = pipeline;
        vp.layout           = pipeline_layout;
        vp.desc_set_layout  = desc_set_layout;
        vp.desc_set         = VK_NULL_HANDLE;

        // Mirror the layout so a push can cover exactly these bindings — see
        // VulkanPipeline::desc_binding_count.
        vp.desc_binding_count = desc.descriptor_count;
        for (u8 i = 0; i < desc.descriptor_count && i < 8; ++i) {
            vp.desc_binding_index[i] = desc.descriptor_bindings[i].binding;
            vp.desc_binding_type[i]  = desc.descriptor_bindings[i].type;
        }

        // MM_LOG("create_pipeline: emplace into slotmap");
        SlotHandle sh       = pipelines.emplace(vp);
        // MM_LOG("create_pipeline: emplace OK: id=%u, gen=%u", sh.id, sh.gen);
        return PipelineHandle{sh};
    }

    void destroy_buffer(BufferHandle h) noexcept {
        auto *b = buffers.get(h.handle);
        if (b) {
            vmaDestroyBuffer(allocator, b->buffer, b->alloc);
        }
        buffers.free(h.handle);
    }

    void destroy_texture(TextureHandle h) noexcept {
        auto *t = textures.get(h.handle);
        if (t) {
            vkDestroyImageView(device, t->view, nullptr);
            vmaDestroyImage(allocator, t->image, t->alloc);
        }
        textures.free(h.handle);
    }

    void destroy_sampler(SamplerHandle h) noexcept {
        auto *s = samplers.get(h.handle);
        if (s) {
            vkDestroySampler(device, s->sampler, nullptr);
        }
        samplers.free(h.handle);
    }

    void destroy_pipeline(PipelineHandle h) noexcept {
        auto *p = pipelines.get(h.handle);
        if (p) {
            if (p->desc_set) {
                vkFreeDescriptorSets(device, desc_pool, 1, &p->desc_set);
            }
            for (u32 i = 0; i < p->desc_slot_count; ++i) {
                if (p->desc_slots[i].set) {
                    vkFreeDescriptorSets(device, desc_pool, 1, &p->desc_slots[i].set);
                    p->desc_slots[i].set = VK_NULL_HANDLE;
                }
            }
            if (p->desc_set_layout) {
                vkDestroyDescriptorSetLayout(device, p->desc_set_layout, nullptr);
            }
            vkDestroyPipeline(device, p->pipeline, nullptr);
            vkDestroyPipelineLayout(device, p->layout, nullptr);
        }
        pipelines.free(h.handle);
    }

    Expected<void, RHIError> update_buffer(BufferHandle h, const void *data, u32 offset, u32 size) noexcept {
        auto *buf = buffers.get(h.handle);
        if (!buf) {
            return make_unexpected(RHIError::InvalidHandle);
        }

        VmaAllocationInfo info;
        vmaGetAllocationInfo(allocator, buf->alloc, &info);

        if (info.pMappedData) {
            memcpy(static_cast<u8 *>(info.pMappedData) + offset, data, size);
        } else {
            void *mapped;
            vmaMapMemory(allocator, buf->alloc, &mapped);
            memcpy(static_cast<u8 *>(mapped) + offset, data, size);
            vmaUnmapMemory(allocator, buf->alloc);
        }
        return {};
    }

    Expected<void, RHIError> update_texture(TextureHandle h, const void *data, u32 x, u32 y, u32 w, u32 h_, u32 mip,
                                            u32 slice) noexcept {
        //        MM_LOG("update_texture entering: handle=%u %ux%u", h.handle.id, w, h_);
        auto *tex = textures.get(h.handle);
        if (!tex) {
            MM_ERROR("update_texture: invalid texture handle");
            return make_unexpected(RHIError::InvalidHandle);
        }

        // Create staging buffer
        u32           bpp        = get_format_size(tex->format);
        VkDeviceSize       image_size = static_cast<VkDeviceSize>(w) * h_ * bpp;
        VkBufferCreateInfo buf_info{};
        buf_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        buf_info.size  = image_size;
        buf_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

        VmaAllocationCreateInfo alloc_info{};
        alloc_info.usage = VMA_MEMORY_USAGE_CPU_ONLY; // Simplest usage for staging
        alloc_info.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

        VkBuffer          staging_buf;
        VmaAllocation     staging_alloc;
        VmaAllocationInfo staging_info;
        //        MM_LOG("update_texture: creating staging buffer size=%zu (bpp=%u)", (size_t)image_size, bpp);
        if (vmaCreateBuffer(allocator, &buf_info, &alloc_info, &staging_buf, &staging_alloc, &staging_info) != VK_SUCCESS) {
            MM_ERROR("update_texture: staging buffer creation failed");
            return make_unexpected(RHIError::OutOfMemory);
        }

        // MM_LOG("update_texture: copying pixels to staging");
        memcpy(staging_info.pMappedData, data, static_cast<size_t>(image_size));

        // One-shot command buffer for transfer
        VkCommandBufferAllocateInfo cmd_alloc{};
        cmd_alloc.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cmd_alloc.commandPool        = cmd_pool;
        cmd_alloc.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cmd_alloc.commandBufferCount = 1;

        VkCommandBuffer transfer_cmd;
        //        MM_LOG("update_texture: allocating command buffer");
        if (vkAllocateCommandBuffers(device, &cmd_alloc, &transfer_cmd) != VK_SUCCESS) {
            MM_ERROR("update_texture: cmd buffer allocation failed");
            vmaDestroyBuffer(allocator, staging_buf, staging_alloc);
            return make_unexpected(RHIError::BackendError);
        }

        VkCommandBufferBeginInfo begin{};
        begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(transfer_cmd, &begin);

        // Transition: undefined → transfer dst
        VkImageMemoryBarrier barrier{};
        barrier.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.image                           = tex->image;
        barrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel   = mip;
        barrier.subresourceRange.levelCount     = 1;
        barrier.subresourceRange.baseArrayLayer = slice;
        barrier.subresourceRange.layerCount     = 1;
        barrier.oldLayout                       = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout                       = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.srcAccessMask                   = 0;
        barrier.dstAccessMask                   = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
        vkCmdPipelineBarrier(transfer_cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

        // Copy buffer → image
        VkBufferImageCopy region{};
        region.bufferOffset                    = 0;
        region.bufferRowLength                 = 0;
        region.bufferImageHeight               = 0;
        region.imageSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel       = mip;
        region.imageSubresource.baseArrayLayer = slice;
        region.imageSubresource.layerCount     = 1;
        region.imageOffset                     = {static_cast<i32>(x), static_cast<i32>(y), 0};
        region.imageExtent                     = {w, h_, 1};
        //        MM_LOG("update_texture: recording copy command");
        vkCmdCopyBufferToImage(transfer_cmd, staging_buf, tex->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        // Transition: transfer dst → shader read
        barrier.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(transfer_cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

        vkEndCommandBuffer(transfer_cmd);

        VkSubmitInfo submit{};
        submit.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit.commandBufferCount = 1;
        submit.pCommandBuffers    = &transfer_cmd;

        VkFenceCreateInfo fence_info{};
        fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        VkFence transfer_fence;
        vkCreateFence(device, &fence_info, nullptr, &transfer_fence);

        //        MM_LOG("update_texture: submitting to queue");
        VkResult submit_res = vkQueueSubmit(graphics_queue, 1, &submit, transfer_fence);
        if (submit_res != VK_SUCCESS) {
            MM_ERROR("update_texture: vkQueueSubmit failed with %d", (int)submit_res);
            vkDestroyFence(device, transfer_fence, nullptr);
            vkFreeCommandBuffers(device, cmd_pool, 1, &transfer_cmd);
            vmaDestroyBuffer(allocator, staging_buf, staging_alloc);
            return make_unexpected(RHIError::BackendError);
        }

        //        MM_LOG("update_texture: waiting for fence");
        vkWaitForFences(device, 1, &transfer_fence, VK_TRUE, UINT64_MAX);

        //        MM_LOG("update_texture: cleanup");
        vkDestroyFence(device, transfer_fence, nullptr);
        vkFreeCommandBuffers(device, cmd_pool, 1, &transfer_cmd);
        vmaDestroyBuffer(allocator, staging_buf, staging_alloc);
        //        MM_LOG("update_texture: done");

        return {};
    }

    Expected<void, RHIError> begin_frame() noexcept {
        VkResult fence_res = vkWaitForFences(device, 1, &frame_fence, VK_TRUE, UINT64_MAX);
        if (fence_res == VK_ERROR_DEVICE_LOST) {
            MM_ERROR("begin_frame: device lost on vkWaitForFences");
            return make_unexpected(RHIError::DeviceLost);
        }

        VkResult reset_res = vkResetFences(device, 1, &frame_fence);
        if (reset_res == VK_ERROR_DEVICE_LOST) {
            MM_ERROR("begin_frame: device lost on vkResetFences");
            return make_unexpected(RHIError::DeviceLost);
        }

        VkResult result = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, acquire_sem, VK_NULL_HANDLE, &swap_index);
        if (result == VK_ERROR_OUT_OF_DATE_KHR) {
            return make_unexpected(RHIError::DeviceLost);
        }
        if (result == VK_ERROR_DEVICE_LOST) {
            MM_ERROR("begin_frame: device lost on vkAcquireNextImageKHR");
            return make_unexpected(RHIError::DeviceLost);
        }

        vkResetCommandBuffer(cmd_buf, 0);

        VkCommandBufferBeginInfo begin{};
        begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd_buf, &begin);

        return {};
    }

    // ─── Surface lifetime (separate from the device's) ────────────────────
    // Android sends APP_CMD_TERM_WINDOW every time the activity is backgrounded,
    // not only when the app is really finishing: a Home press and return IS a
    // window teardown. shutdown() answers that by destroying the instance, the
    // device and every buffer, image and descriptor set the game owns — which
    // leaves the game holding handles to objects that no longer exist, and
    // leaves a process that is still running with nothing able to rebuild them
    // (android_main does not run again).
    //
    // So the window's own objects get their own pair of calls. Everything the
    // game allocated outlives the window it was drawn into.
    // True once a window is attached. The host needs to tell "no surface, make
    // one" from "a surface at a new size": the first has to rebuild the
    // VkSurfaceKHR itself, the second only rebuilds the swapchain.
    bool has_surface() const noexcept { return surface != VK_NULL_HANDLE; }

    void release_surface() noexcept {
        if (device == VK_NULL_HANDLE) {
            return;
        }
        if (swapchain != VK_NULL_HANDLE || surface != VK_NULL_HANDLE) {
            vkDeviceWaitIdle(device);
        }
        for (auto v : swap_views) {
            vkDestroyImageView(device, v, nullptr);
        }
        swap_views.clear();
        swap_images.clear();
        if (swapchain != VK_NULL_HANDLE) {
            vkb::destroy_swapchain(vkb_swapchain);
            vkb_swapchain = {};
        }
        swapchain = VK_NULL_HANDLE;
        if (surface != VK_NULL_HANDLE) {
            vkDestroySurfaceKHR(instance, surface, nullptr);
            surface = VK_NULL_HANDLE;
        }
        MM_LOG("release_surface: window objects released, device kept");
    }

    // Rebuilds the surface and swapchain for a NEW window, on the device that
    // is already running. Everything else — pipelines, textures, shaders,
    // descriptors — is untouched and stays valid.
    Expected<void, RHIError> attach_surface(void *window_handle) noexcept {
        if (instance == VK_NULL_HANDLE || device == VK_NULL_HANDLE) {
            return make_unexpected(RHIError::BackendError);
        }
        release_surface();

        int window_w = 0;
        int window_h = 0;
#if defined(VK_USE_PLATFORM_ANDROID_KHR)
        // The window's own dimensions, not the swapchain's: init() reads them
        // the same way, and attach_surface has to pick the extent before the
        // swapchain exists.
        MM_LOG("attach_surface: window_handle=%p", window_handle);
        if (window_handle == nullptr) {
            return make_unexpected(RHIError::InvalidHandle);
        }
        VkAndroidSurfaceCreateInfoKHR sci{};
        sci.sType  = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR;
        sci.window = static_cast<ANativeWindow *>(window_handle);
        if (vkCreateAndroidSurfaceKHR(instance, &sci, nullptr, &surface) != VK_SUCCESS) {
            MM_ERROR("attach_surface: vkCreateAndroidSurfaceKHR failed");
            return make_unexpected(RHIError::BackendError);
        }
        // These return the dimension; they do not take an out-parameter.
        window_w = ANativeWindow_getWidth(static_cast<ANativeWindow *>(window_handle));
        window_h = ANativeWindow_getHeight(static_cast<ANativeWindow *>(window_handle));
    #else
        (void)window_handle;
        (void)window_w;
        (void)window_h;
        return make_unexpected(RHIError::BackendError);
    #endif
        if (window_w <= 0 || window_h <= 0) {
            return make_unexpected(RHIError::InvalidHandle);
        }

        // The swapchain itself is resize()'s job, not a second copy of it: the
        // format, the present mode and the "no framebuffer rebuild needed —
        // dynamic rendering reads swap_views and swap_extent directly" rule all
        // live in one place, and a resume that rendered differently from a
        // rotation would be a very quiet bug.
        MM_LOG("attach_surface: %dx%d", window_w, window_h);
        return resize(SurfaceInfo{window_handle, static_cast<u32>(window_w), static_cast<u32>(window_h), 1.0f});
    }

    Expected<void, RHIError> resize(SurfaceInfo s) noexcept {
        if (!s.native_handle) {
            return make_unexpected(RHIError::InvalidHandle);
        }
        // Android sends 0x0 while backgrounding — ignore, keep old swapchain.
        if (s.width == 0 || s.height == 0) {
            return {};
        }

        // Wait for the in-flight frame, not the whole device.
        //
        // This backend is serialized, not multi-frame-in-flight: there is ONE
        // frame_fence, created SIGNALED, waited+reset in begin_frame() and
        // re-signalled by end_frame()'s vkQueueSubmit. So one wait on that fence
        // IS the "in-flight frames" wait, and it cannot deadlock here — resize()
        // is driven from android_app_pre_exec_cmd (APP_CMD_CONFIG_CHANGED),
        // which runs BEFORE frame() in the same loop iteration, so the fence is
        // always in the signalled state by the time we get here.
        //
        // This used to loop over a MAX_FRAMES_IN_FLIGHT / inFlightFences[] pair
        // that no longer exists — a leftover from before the design was
        // simplified to a single fence — so the file did not compile for Android
        // at all.
        if (frame_fence != VK_NULL_HANDLE) {
            vkWaitForFences(device, 1, &frame_fence, VK_TRUE, UINT64_MAX);
        }

        // Rebuild swapchain, passing the old one so the driver can reuse it.
        vkb::SwapchainBuilder builder(vkb_device, surface);
        auto                  swap_ret = builder.set_desired_format({VK_FORMAT_B8G8R8A8_SRGB, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR})
                            .set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR) // never MAILBOX on Android (battery)
                            .set_desired_extent(s.width, s.height)
                            .set_old_swapchain(vkb_swapchain)
                            .build();

        if (!swap_ret) {
            MM_ERROR("resize: %s", swap_ret.error().message().c_str());
            // Keep rendering on the old swapchain — destroy nothing.
            return make_unexpected(RHIError::BackendError);
        }

        // Destroy old views only after the replacement exists.
        for (auto v : swap_views) {
            vkDestroyImageView(device, v, nullptr);
        }
        swap_views.clear();
        swap_images.clear();
        vkb::destroy_swapchain(vkb_swapchain);

        vkb_swapchain = swap_ret.value();
        swapchain     = vkb_swapchain.swapchain;
        swap_extent   = vkb_swapchain.extent;
        swap_format   = vkb_swapchain.image_format;

        auto img_res  = vkb_swapchain.get_images();
        auto view_res = vkb_swapchain.get_image_views();
        if (!img_res || !view_res) {
            return make_unexpected(RHIError::BackendError);
        }
        swap_images = img_res.value();
        swap_views  = view_res.value();

        // NOTE: no framebuffer/depth rebuild needed — begin_frame() uses
        // dynamic rendering with swap_views[swap_index] + swap_extent directly.
        return {};
    }

    Expected<void, RHIError> end_frame() noexcept {
        vkEndCommandBuffer(cmd_buf);

        VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

        VkSubmitInfo         submit{};
        submit.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit.waitSemaphoreCount   = 1;
        submit.pWaitSemaphores      = &acquire_sem;
        submit.pWaitDstStageMask    = &wait_stage;
        submit.signalSemaphoreCount = 1;
        submit.pSignalSemaphores    = &release_sem;
        submit.commandBufferCount   = 1;
        submit.pCommandBuffers      = &cmd_buf;

        //        MM_LOG("VulkanBackend::end_frame() - submitting graphics queue");
        VkResult submit_res         = vkQueueSubmit(graphics_queue, 1, &submit, frame_fence);
        if (submit_res == VK_ERROR_DEVICE_LOST) {
            MM_ERROR("end_frame: device lost on vkQueueSubmit");
            return make_unexpected(RHIError::DeviceLost);
        }
        if (submit_res != VK_SUCCESS) {
            MM_ERROR("end_frame: vkQueueSubmit failed: %d", (int)submit_res);
            return make_unexpected(RHIError::BackendError);
        }

        VkPresentInfoKHR present{};
        present.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        present.waitSemaphoreCount = 1;
        present.pWaitSemaphores    = &release_sem;
        present.swapchainCount     = 1;
        present.pSwapchains        = &swapchain;
        present.pImageIndices      = &swap_index;

        //        MM_LOG("VulkanBackend::end_frame() - presenting swapchain");
        VkResult present_res       = vkQueuePresentKHR(present_queue, &present);
        if (present_res == VK_ERROR_OUT_OF_DATE_KHR || present_res == VK_SUBOPTIMAL_KHR) {
            //            MM_LOG("VulkanBackend::end_frame() - swapchain out of date or suboptimal");
        } else if (present_res == VK_ERROR_DEVICE_LOST) {
            MM_ERROR("end_frame: device lost on vkQueuePresentKHR");
            return make_unexpected(RHIError::DeviceLost);
        } else if (present_res != VK_SUCCESS) {
            MM_ERROR("end_frame: vkQueuePresentKHR failed: %d", (int)present_res);
        }

        ++timeline_value;
        ++frame_index;
        return {};
    }

    Expected<void, RHIError> begin_pass(const PassDesc &pass) noexcept {
        // Transition swapchain image to COLOR_ATTACHMENT_OPTIMAL
        image_barrier(cmd_buf, swap_images[swap_index], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 0,
                      VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);

        VkRenderingAttachmentInfo color_attach{};
        color_attach.sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        color_attach.imageView   = swap_views[swap_index];
        color_attach.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        color_attach.loadOp      = (pass.color_load == LoadOp::Clear) ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
        color_attach.storeOp     = VK_ATTACHMENT_STORE_OP_STORE;
        memcpy(&color_attach.clearValue.color.float32[0], pass.clear_color, 4 * sizeof(f32));

        VkRenderingInfo render_info{};
        render_info.sType                = VK_STRUCTURE_TYPE_RENDERING_INFO;
        render_info.renderArea           = {{0, 0}, swap_extent};
        render_info.layerCount           = 1;
        render_info.colorAttachmentCount = 1;
        render_info.pColorAttachments    = &color_attach;

        vkCmdBeginRenderingKHR(cmd_buf, &render_info);

        VkViewport viewport{0, 0, static_cast<f32>(swap_extent.width), static_cast<f32>(swap_extent.height), 0, 1};
        VkRect2D   scissor{{0, 0}, swap_extent};
        vkCmdSetViewport(cmd_buf, 0, 1, &viewport);
        vkCmdSetScissor(cmd_buf, 0, 1, &scissor);

        return {};
    }

    Expected<void, RHIError> end_pass() noexcept {
        vkCmdEndRenderingKHR(cmd_buf);

        // Transition swapchain image back to PRESENT_SRC_KHR
        image_barrier(cmd_buf, swap_images[swap_index], VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                      VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, 0, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);

        return {};
    }

    Expected<void, RHIError> bind_pipeline(PipelineHandle h) noexcept {
        auto *pl = pipelines.get(h.handle);
        if (!pl) {
            return make_unexpected(RHIError::InvalidHandle);
        }
        vkCmdBindPipeline(cmd_buf, VK_PIPELINE_BIND_POINT_GRAPHICS, pl->pipeline);
        current_pipeline_handle = h;
        return {};
    }

    void flush_descriptors() noexcept {
        auto *pl = pipelines.get(current_pipeline_handle.handle);
        if (!pl || !pl->desc_set_layout) {
            return;
        }

        // A descriptor pair that is only half known is never a degraded draw, it
        // is a missing one: the combined-image-sampler descriptor cannot be
        // written, and whatever the set already held is undefined per spec
        // (white in practice). Name the pipeline instead of skipping in silence.
        const bool image_complete = (pl->current_view != VK_NULL_HANDLE && pl->current_sampler != VK_NULL_HANDLE);
        if (!image_complete) {
            MM_ERROR("flush_descriptors: pipeline %u has %s%s — its fragment texture descriptor cannot be written (draws white)",
                     current_pipeline_handle.handle.id, pl->current_view ? "" : "no view, ", pl->current_sampler ? "" : "no sampler");
        }

        // ── Push descriptors: the values travel WITH the draw ──
        //
        // vkUpdateDescriptorSets mutates a set immediately, but the draws using
        // that set execute later, at vkQueueSubmit. One set per pipeline therefore
        // cannot describe two textures drawn in the same frame: every draw reads
        // whatever the LAST write left there. That is why cards came out white on
        // Vulkan and fine on Metal — setFragmentTexture: is recorded per draw,
        // while this was recording nothing at all.
        //
        // A push writes the descriptor into the command buffer itself, so each
        // draw carries its own. It replaces the set rather than updating it, so it
        // must cover exactly the bindings the layout declares — hence the layout
        // mirror in VulkanPipeline.
        if (push_descriptors_supported && pfn_push_descriptor_sets) {
            VkWriteDescriptorSet    writes[8]{};
            VkDescriptorImageInfo   img_info{};
            VkDescriptorBufferInfo  ubo_infos[8]{};
            u32                     write_count = 0;

            img_info.sampler     = pl->current_sampler;
            img_info.imageView   = pl->current_view;
            img_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            for (u8 i = 0; i < pl->desc_binding_count && i < 8 && write_count < 8; ++i) {
                const u32 binding = pl->desc_binding_index[i];
                auto    &w       = writes[write_count++];
                w.sType          = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                w.dstSet         = VK_NULL_HANDLE; // a push has no destination set
                w.dstBinding     = binding;
                w.dstArrayElement = 0;
                w.descriptorCount = 1;

                if (pl->desc_binding_type[i] == DescriptorType::CombinedImageSampler) {
                    if (!image_complete) {
                        --write_count; // nothing valid to push for this binding
                        continue;
                    }
                    w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                    w.pImageInfo      = &img_info;
                } else {
                    // An unbound UBO still has to be covered, and a null buffer
                    // would be undefined — point it at the stand-in.
                    VkBuffer bk = pl->current_ubos[binding & 7u];
                    if (!bk) {
                        bk = desc_fallback_ubo;
                    }
                    ubo_infos[i].buffer = bk;
                    ubo_infos[i].offset = 0;
                    ubo_infos[i].range  = VK_WHOLE_SIZE;
                    w.descriptorType    = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                    w.pBufferInfo       = &ubo_infos[i];
                }
            }

            if (write_count > 0) {
                pfn_push_descriptor_sets(cmd_buf, VK_PIPELINE_BIND_POINT_GRAPHICS, pl->layout, 0, write_count, writes);
            }
            return;
        }

        // ── Fallback: a descriptor set per (pipeline, texture) ──
        //
        // Same problem, solved by giving each texture its own set so no two draws
        // in a frame ever share one. Push descriptors remain the better answer and
        // are used whenever the device offers them; this is the path that has to
        // work everywhere else.
        VulkanPipeline::DescSlot *slot = nullptr;
        if (image_complete) {
            for (u32 i = 0; i < pl->desc_slot_count; ++i) {
                auto &c = pl->desc_slots[i];
                if (c.view == pl->current_view && c.sampler == pl->current_sampler) {
                    slot = &c;
                    break;
                }
            }

            // Allocating is only correct when the slot is genuinely NEW. An
            // out-of-slots draw is taking a slot that already owns a set, and
            // allocating again there was a per-draw, per-frame
            // vkAllocateDescriptorSets that also dropped the old handle on the
            // floor (a set leak, since sets are only freed with their pool).
            // The shader lab hit exactly that: 9 distinct (pipeline, texture)
            // pairs against MAX_DESC_SLOTS = 8, so every draw past the 8th
            // allocated forever. On the emulator each allocate is a host
            // round-trip through gfxstream, so the frame never completed — 88%
            // CPU and a black screen, with no error anywhere to point at it.
            bool need_set = false;
            bool fell_back = false;
            if (!slot) {
                if (pl->desc_slot_count < VulkanPipeline::MAX_DESC_SLOTS) {
                    slot = &pl->desc_slots[pl->desc_slot_count++];
                    need_set = true;
                } else {
                    // Out of slots: the draw gets slot 0's texture, which is
                    // wrong but defined. Say so rather than ship a frame that
                    // quietly lies.
                    static bool warned = false;
                    if (!warned) {
                        warned = true;
                        MM_ERROR("pipeline %u exceeded %u texture slots — falling back to slot 0, whose texture the frame will also show",
                                 current_pipeline_handle.handle.id, (u32)VulkanPipeline::MAX_DESC_SLOTS);
                    }
                    slot     = &pl->desc_slots[0];
                    fell_back = true;
                }
            }

            if (need_set) {
                VkDescriptorSetAllocateInfo dsai{};
                dsai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
                dsai.descriptorPool     = desc_pool;
                dsai.descriptorSetCount = 1;
                dsai.pSetLayouts        = &pl->desc_set_layout;
                if (vkAllocateDescriptorSets(device, &dsai, &slot->set) != VK_SUCCESS) {
                    MM_ERROR("flush_descriptors: vkAllocateDescriptorSets failed — this draw will sample an undefined descriptor (white)");
                    slot->set = VK_NULL_HANDLE;
                    return;
                }
                slot->view        = pl->current_view;
                slot->sampler      = pl->current_sampler;
                slot->ubos_written = false;
                for (u32 i = 0; i < 8; ++i) {
                    slot->ubos[i] = VK_NULL_HANDLE;
                }
            } else if (fell_back && slot->set != VK_NULL_HANDLE) {
                // Sharing slot 0 with whatever draw owns it: its image stays
                // (that IS the documented degradation), but the UBOs must be
                // refreshed or this draw samples the other draw's buffers.
                // The rewrite below re-emits slot 0's own image unchanged.
                slot->ubos_written = false;
            }
        }

        if (!slot) {
            // No usable texture descriptor. Binding 0 keeps whatever it held,
            // which is undefined — but drawing with a null set handle would be
            // worse (undefined behaviour in the driver rather than a defined
            // wrong colour), so bind slot 0 if there is one and move on.
            if (pl->desc_slot_count == 0) {
                return;
            }
            slot = &pl->desc_slots[0];
        }

        // The image descriptor is immutable for the life of the slot, so it is written
        // once. The UBOs are NOT: flush_ring draws from a rotating pool of params
        // buffers, so two rings sharing one pipeline and one texture legitimately
        // want different ones. Compare the buffers rather than assuming, which is
        // what a plain "written once" flag would get wrong.
        bool ubos_changed = !slot->ubos_written;
        for (u32 i = 0; i < 8; ++i) {
            if (slot->ubos[i] != pl->current_ubos[i]) {
                ubos_changed = true;
                break;
            }
        }

        if (ubos_changed) {
            u32                     write_count = 0;
            VkWriteDescriptorSet    writes[8]{};
            VkDescriptorImageInfo   img_info{};
            VkDescriptorBufferInfo  ubo_infos[8]{};

            // A binding can only be written with the type its layout entry
            // declares. Writing a UBO into a binding declared as a sampler is
            // not a soft failure — the emulator's lavapipe aborts the whole
            // guest on it (lvp_UpdateDescriptorSets asserts bind_layout->stride
            // == 1), taking the app AND the emulator's system services with it.
            //
            // This is not hypothetical: the renderer binds the camera UBO at
            // index 0 (mm_renderer.hpp, bind_uniform_buffer(camera_ubo, 0))
            // while create_default_pipeline declares binding 0 as the texture
            // and binding 1 as the camera. So the writes have to be filtered by
            // the layout, not trusted to match.
            auto declared_type = [&](u32 binding, DescriptorType &out) -> bool {
                for (u8 d = 0; d < pl->desc_binding_count; ++d) {
                    if (pl->desc_binding_index[d] == binding) {
                        out = pl->desc_binding_type[d];
                        return true;
                    }
                }
                return false;
            };

            if (!slot->ubos_written) {
                img_info.sampler     = slot->sampler;
                img_info.imageView   = slot->view;
                img_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

                // Bind the image where the layout actually declares a sampler,
                // rather than assuming it is 0. Every engine pipeline puts it at
                // 0, so this changes nothing for them and is correct for the
                // ones that do not.
                for (u8 d = 0; d < pl->desc_binding_count; ++d) {
                    if (pl->desc_binding_type[d] != DescriptorType::CombinedImageSampler) {
                        continue;
                    }
                    auto &w           = writes[write_count++];
                    w.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                    w.dstSet          = slot->set;
                    w.dstBinding      = pl->desc_binding_index[d];
                    w.descriptorCount = 1;
                    w.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                    w.pImageInfo      = &img_info;
                    break;
                }
            }

            for (u32 i = 0; i < 8 && write_count < 8; ++i) {
                if (!pl->current_ubos[i]) {
                    continue;
                }
                DescriptorType dt{};
                if (!declared_type(i, dt) || dt != DescriptorType::UniformBuffer) {
                    // Bound to an index the layout does not declare as a UBO.
                    // Dropping it is the only defined option: the alternative is
                    // a type-mismatched write (see above).
                    continue;
                }
                ubo_infos[i].buffer = pl->current_ubos[i];
                ubo_infos[i].offset = 0;
                ubo_infos[i].range  = VK_WHOLE_SIZE;

                auto &w             = writes[write_count++];
                w.sType             = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                w.dstSet            = slot->set;
                w.dstBinding        = i;
                w.descriptorCount   = 1;
                w.descriptorType    = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                w.pBufferInfo       = &ubo_infos[i];
                slot->ubos[i]       = pl->current_ubos[i];
            }

            if (write_count > 0) {
                vkUpdateDescriptorSets(device, write_count, writes, 0, nullptr);
            }
            slot->ubos_written = true;
        }

        vkCmdBindDescriptorSets(cmd_buf, VK_PIPELINE_BIND_POINT_GRAPHICS, pl->layout, 0, 1, &slot->set, 0, nullptr);
    }

    Expected<void, RHIError> bind_vertex_buffers(BufferHandle *handles, u32 count, const u64 *offsets, const u64 *strides,
                                                 const u32 *bindings = nullptr) noexcept {
        VkBuffer     vk_bufs[16];
        VkDeviceSize vk_offsets[16];
        for (u32 i = 0; i < count && i < 16; ++i) {
            auto *buf = this->buffers.get(handles[i].handle);
            if (!buf) {
                return make_unexpected(RHIError::InvalidHandle);
            }
            vk_bufs[i]    = buf->buffer;
            vk_offsets[i] = offsets ? offsets[i] : 0;
        }
        u32 first = bindings ? bindings[0] : 0;
        vkCmdBindVertexBuffers(cmd_buf, first, count, vk_bufs, vk_offsets);
        return {};
    }

    Expected<void, RHIError> bind_index_buffer(BufferHandle h, IndexType type, u64 offset = 0) noexcept {
        auto *buf = buffers.get(h.handle);
        if (!buf) {
            return make_unexpected(RHIError::InvalidHandle);
        }
        vkCmdBindIndexBuffer(cmd_buf, buf->buffer, offset, type == IndexType::Uint32 ? VK_INDEX_TYPE_UINT32 : VK_INDEX_TYPE_UINT16);
        return {};
    }

    // `binding` is the caller's LOGICAL UBO slot, not the shader's physical
    // binding number. Physical binding 0 is always the texture, so both
    // backends shift by +1 on the way in (MetalBackend::bind_uniform_buffer
    // does the same into its buffer index). Logical 0 -> binding 1 (CameraUBO),
    // logical 1 -> binding 2 (effect params). Handing it a `bind::` constant
    // from mm_renderer.hpp double-shifts: the camera descriptor is then never
    // written at all, which leaves the vertex shader reading a descriptor the
    // driver never saw written.
    Expected<void, RHIError> bind_uniform_buffer(BufferHandle handle, u32 binding) noexcept {
        auto *buf = buffers.get(handle.handle);
        if (!buf) {
            return make_unexpected(RHIError::InvalidHandle);
        }
        auto *pl = pipelines.get(current_pipeline_handle.handle);
        if (pl) {
            u32 physical_binding = binding + 1;
            if (physical_binding < 8) {
                pl->current_ubos[physical_binding] = buf->buffer;
            }
        }
        return {};
    }

    Expected<void, RHIError> draw(u32 vertex_count, u32 instance_count, u32 first_vertex, u32 first_instance) noexcept {
        flush_descriptors();
        vkCmdDraw(cmd_buf, vertex_count, instance_count, first_vertex, first_instance);
        return {};
    }

    Expected<void, RHIError> draw_indexed(u32 index_count, u32 instance_count, u32 first_index, i32 vertex_offset = 0) noexcept {
        flush_descriptors();
        vkCmdDrawIndexed(cmd_buf, index_count, instance_count, first_index, vertex_offset, 0);
        return {};
    }

    Expected<void, RHIError> bind_fragment_texture(TextureHandle handle, u32 binding) noexcept {
        auto *tex = textures.get(handle.handle);
        if (!tex) {
            return make_unexpected(RHIError::InvalidHandle);
        }
        auto *pl = pipelines.get(current_pipeline_handle.handle);
        if (pl) {
            pl->current_view     = tex->view;
        }
        return {};
    }

    Expected<void, RHIError> bind_fragment_sampler(SamplerHandle handle, u32 binding) noexcept {
        auto *samp = samplers.get(handle.handle);
        if (!samp) {
            return make_unexpected(RHIError::InvalidHandle);
        }
        auto *pl = pipelines.get(current_pipeline_handle.handle);
        if (pl) {
            pl->current_sampler  = samp->sampler;
        }
        return {};
    }

    Expected<void, RHIError> set_scissor(i16 x, i16 y, u16 w, u16 h) noexcept {
        VkRect2D scissor{{static_cast<i32>(x), static_cast<i32>(y)}, {w, h}};
        vkCmdSetScissor(cmd_buf, 0, 1, &scissor);
        return {};
    }

  private:
    static u32 get_format_size(PixelFormat fmt) noexcept {
        switch (fmt) {
        case PixelFormat::R8_UNORM:
            return 1;
        case PixelFormat::R8G8B8A8_UNORM:
        case PixelFormat::R8G8B8A8_SRGB:
        case PixelFormat::B8G8R8A8_UNORM:
        case PixelFormat::B8G8R8A8_SRGB:
        case PixelFormat::D32_FLOAT:
            return 4;
        case PixelFormat::R32G32_FLOAT:
            return 8;
        default:
            return 4;
        }
    }

    static VkFormat to_vk_format(PixelFormat fmt) noexcept {
        switch (fmt) {
        case PixelFormat::R8_UNORM:
            return VK_FORMAT_R8_UNORM;
        case PixelFormat::R8G8B8A8_UNORM:
            return VK_FORMAT_A8B8G8R8_UNORM_PACK32;
        case PixelFormat::R8G8B8A8_SRGB:
            return VK_FORMAT_R8G8B8A8_SRGB;
        case PixelFormat::B8G8R8A8_UNORM:
            return VK_FORMAT_B8G8R8A8_UNORM;
        case PixelFormat::B8G8R8A8_SRGB:
            return VK_FORMAT_B8G8R8A8_SRGB;
        case PixelFormat::D32_FLOAT:
            return VK_FORMAT_D32_SFLOAT;
        case PixelFormat::ASTC_4x4:
            return VK_FORMAT_ASTC_4x4_SRGB_BLOCK;
        case PixelFormat::ETC2_RGB8:
            return VK_FORMAT_ETC2_R8G8B8_SRGB_BLOCK;
        case PixelFormat::R32G32_FLOAT:
            return VK_FORMAT_R32G32_SFLOAT;
        default:
            return VK_FORMAT_B8G8R8A8_SRGB;
        }
    }

    static VkFormat to_vk_vertex_format(PixelFormat fmt) noexcept {
        switch (fmt) {
        case PixelFormat::R8G8B8A8_UNORM:
            // Engine vertex colors are packed 0xAARRGGBB u32s (NOT raw RGBA
            // bytes) — swizzle to BGRA for Metal parity (Metal maps this to
            // UChar4Normalized_BGRA). Applies to SpriteVertex color +
            // border_color; f32 vertex data never uses this format.
            return VK_FORMAT_B8G8R8A8_UNORM;
        case PixelFormat::R16G16B16A16_FLOAT:
            return VK_FORMAT_R16G16B16A16_SFLOAT;
        case PixelFormat::R32G32B32A32_FLOAT:
            return VK_FORMAT_R32G32B32A32_SFLOAT;
        case PixelFormat::R32G32_FLOAT:
            return VK_FORMAT_R32G32_SFLOAT;
        default:
            break;
        }
        // Default: assume R16G16 (half2) for position/UV
        return VK_FORMAT_R16G16_SFLOAT;
    }

    static VkPrimitiveTopology to_vk_primitive(PrimitiveType prim) noexcept {
        switch (prim) {
        case PrimitiveType::Triangle:
            return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        case PrimitiveType::TriangleStrip:
            return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
        case PrimitiveType::Line:
            return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
        case PrimitiveType::Point:
            return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
        default:
            return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        }
    }

    static VkCullModeFlags to_vk_cull(CullMode mode) noexcept {
        switch (mode) {
        case CullMode::None:
            return VK_CULL_MODE_NONE;
        case CullMode::Front:
            return VK_CULL_MODE_FRONT_BIT;
        case CullMode::Back:
            return VK_CULL_MODE_BACK_BIT;
        default:
            return VK_CULL_MODE_NONE;
        }
    }

    static VkBlendFactor to_vk_blend(BlendFactor factor) noexcept {
        switch (factor) {
        case BlendFactor::Zero:
            return VK_BLEND_FACTOR_ZERO;
        case BlendFactor::One:
            return VK_BLEND_FACTOR_ONE;
        case BlendFactor::SrcAlpha:
            return VK_BLEND_FACTOR_SRC_ALPHA;
        case BlendFactor::OneMinusSrcAlpha:
            return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        case BlendFactor::DstAlpha:
            return VK_BLEND_FACTOR_DST_ALPHA;
        case BlendFactor::OneMinusDstAlpha:
            return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
        case BlendFactor::SrcColor:
            return VK_BLEND_FACTOR_SRC_COLOR;
        case BlendFactor::OneMinusSrcColor:
            return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
        default:
            return VK_BLEND_FACTOR_ONE;
        }
    }

    static VkBlendOp to_vk_blend_op(BlendOp op) noexcept {
        switch (op) {
        case BlendOp::Add:
            return VK_BLEND_OP_ADD;
        case BlendOp::Subtract:
            return VK_BLEND_OP_SUBTRACT;
        case BlendOp::ReverseSubtract:
            return VK_BLEND_OP_REVERSE_SUBTRACT;
        case BlendOp::Min:
            return VK_BLEND_OP_MIN;
        case BlendOp::Max:
            return VK_BLEND_OP_MAX;
        default:
            return VK_BLEND_OP_ADD;
        }
    }

    static VkDescriptorType to_vk_descriptor_type(DescriptorType type) noexcept {
        switch (type) {
        case DescriptorType::UniformBuffer:
            return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        case DescriptorType::CombinedImageSampler:
            return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        case DescriptorType::StorageBuffer:
            return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        default:
            return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        }
    }

    static VkShaderStageFlags to_vk_shader_stage(u32 mask) noexcept {
        VkShaderStageFlags flags = 0;
        if (mask & 1) {
            flags |= VK_SHADER_STAGE_VERTEX_BIT;
        }
        if (mask & 2) {
            flags |= VK_SHADER_STAGE_FRAGMENT_BIT;
        }
        if (mask & 4) {
            flags |= VK_SHADER_STAGE_COMPUTE_BIT;
        }
        return flags;
    }

    static VkSamplerAddressMode to_vk_address(SamplerAddress addr) noexcept {
        switch (addr) {
        case SamplerAddress::Repeat:
            return VK_SAMPLER_ADDRESS_MODE_REPEAT;
        case SamplerAddress::ClampToEdge:
            return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        case SamplerAddress::MirroredRepeat:
            return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
        default:
            return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        }
    }

    static VkCompareOp to_vk_compare(CompareOp op) noexcept {
        switch (op) {
        case CompareOp::Never:
            return VK_COMPARE_OP_NEVER;
        case CompareOp::Less:
            return VK_COMPARE_OP_LESS;
        case CompareOp::Equal:
            return VK_COMPARE_OP_EQUAL;
        case CompareOp::LessEqual:
            return VK_COMPARE_OP_LESS_OR_EQUAL;
        case CompareOp::Greater:
            return VK_COMPARE_OP_GREATER;
        case CompareOp::NotEqual:
            return VK_COMPARE_OP_NOT_EQUAL;
        case CompareOp::GreaterEqual:
            return VK_COMPARE_OP_GREATER_OR_EQUAL;
        case CompareOp::Always:
            return VK_COMPARE_OP_ALWAYS;
        default:
            return VK_COMPARE_OP_ALWAYS;
        }
    }

    static void image_barrier(VkCommandBuffer cmd, VkImage image, VkImageLayout old_layout, VkImageLayout new_layout, VkAccessFlags src_access,
                              VkAccessFlags dst_access, VkPipelineStageFlags src_stage, VkPipelineStageFlags dst_stage) noexcept {
        VkImageMemoryBarrier barrier{};
        barrier.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout                       = old_layout;
        barrier.newLayout                       = new_layout;
        barrier.srcAccessMask                   = src_access;
        barrier.dstAccessMask                   = dst_access;
        barrier.image                           = image;
        barrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel   = 0;
        barrier.subresourceRange.levelCount     = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount     = 1;

        vkCmdPipelineBarrier(cmd, src_stage, dst_stage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    }

};

static_assert(RHI_Backend<VulkanBackend>, "VulkanBackend must satisfy RHI_Backend concept");

#endif // USE_VULKAN_BACKEND
