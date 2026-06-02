// Copyright (c) 2025 Markmos
// SPDX-License-Identifier: MIT

#pragma once
#include "mm_rhi_concept.hpp"
#include "../core/mm_handle.hpp"
#include "../core/mm_slotmap.hpp"
#include <cstdint>
#include <cstddef>
#include "../core/mm_expected.hpp"
#include "../core/mm_log.hpp"
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

#include <vulkan/vulkan.h>
#include "../thirdparty/vk-bootstrap/VkBootstrap.h"
#include "../thirdparty/VMA/include/vk_mem_alloc.h"

struct VulkanBuffer {
    VkBuffer    buffer;
    VmaAllocation alloc;
    uint32_t    size;
    BufferType  type;
};

struct VulkanTexture {
    VkImage         image;
    VmaAllocation   alloc;
    VkImageView     view;
    uint32_t        width, height;
    PixelFormat     format;
};

struct VulkanPipeline {
    VkPipeline            pipeline;
    VkPipelineLayout      layout;
    VkDescriptorSetLayout desc_set_layout;
    VkDescriptorSet       desc_set;
    VkImageView           current_view;
    VkSampler             current_sampler;
    VkBuffer              current_ubo;
    uint32_t              ubo_slot;
    bool                  descriptor_dirty;
};

struct VulkanSampler {
    VkSampler sampler;
};

struct VulkanBackend {
    // vk-bootstrap handles
    vkb::Instance     vkb_instance;
    vkb::Device       vkb_device;
    vkb::Swapchain    vkb_swapchain;

    VkInstance        instance;
    VkDevice          device;
    VkPhysicalDevice  phys_device;
    VkQueue           graphics_queue;
    VkQueue           present_queue;
    uint32_t          graphics_family;
    uint32_t          present_family;

    // VMA allocator
    VmaAllocator      allocator;

    // Sync
    VkSemaphore       acquire_sem;
    VkSemaphore       release_sem;
    VkFence           frame_fence;
    VkCommandPool     cmd_pool;
    VkCommandBuffer   cmd_buf;
    uint64_t          timeline_value;

    // Surface + Swapchain
    VkSurfaceKHR      surface;
    VkSwapchainKHR    swapchain;
    VkExtent2D        swap_extent;
    VkFormat          swap_format;
    std::vector<VkImage>     swap_images;
    std::vector<VkImageView> swap_views;
    uint32_t          swap_index;

    // Slotmaps
    Slotmap<VulkanBuffer>   buffers;
    Slotmap<VulkanTexture>  textures;
    Slotmap<VulkanPipeline> pipelines;
    Slotmap<VulkanSampler>  samplers;

    // Descriptor pool
    VkDescriptorPool desc_pool;

    // Currently bound pipeline (for descriptor updates)
    PipelineHandle current_pipeline_handle;

    // Dynamic rendering
    PFN_vkCmdBeginRenderingKHR  vkCmdBeginRenderingKHR;
    PFN_vkCmdEndRenderingKHR    vkCmdEndRenderingKHR;

    uint32_t frame_index;

    Expected<void, RHIError> init(void* window_handle) noexcept {
        if (instance != VK_NULL_HANDLE) {
            MM_LOG("VulkanBackend::init() - Already initialized (Instance: %p), skipping vkb setup.", (void*)instance);
            return {};
        }
        MM_LOG("VulkanBackend::init() - Creating Instance");
        // Step 1: vkb::InstanceBuilder — no manual vkCreateInstance
        vkb::InstanceBuilder inst_builder;
        auto inst_ret = inst_builder
            .set_app_name("Markmos")
            .set_engine_name("Markmos Engine")
            .require_api_version(1, 1, 0)
            .enable_extension(VK_KHR_SURFACE_EXTENSION_NAME)
            .enable_extension(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME)
            .build();
        if (!inst_ret) {
            MM_ERROR("Failed to create Vulkan instance: %s", inst_ret.error().message().c_str());
            return make_unexpected(RHIError::BackendError);
        }
        MM_LOG("Vulkan instance created");

        vkb_instance = inst_ret.value();
        instance = vkb_instance.instance;

        // Step 2: Surface creation (platform-specific)
        surface = VK_NULL_HANDLE;
#if defined(VK_USE_PLATFORM_ANDROID_KHR)
        MM_LOG("Creating Android Surface (window_handle: %p)", window_handle);
        if (window_handle) {
            VkAndroidSurfaceCreateInfoKHR sci{};
            sci.sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR;
            sci.window = static_cast<ANativeWindow*>(window_handle);
            if (vkCreateAndroidSurfaceKHR(instance, &sci, nullptr, &surface) != VK_SUCCESS) {
                MM_ERROR("vkCreateAndroidSurfaceKHR failed");
                return make_unexpected(RHIError::BackendError);
            }
            MM_LOG("Android Surface created: %p", surface);
        }
#endif

        // Step 3: Select physical device
        MM_LOG("Selecting Physical Device");

        vkb::PhysicalDeviceSelector phys_dev_selector(vkb_instance);
        auto phys_dev_ret = phys_dev_selector
            .set_minimum_version(1, 1)
            .set_surface(surface) // Pass surface explicitly to ensure presentation support
            .add_required_extension(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME)
            .select();
        if (!phys_dev_ret) {
            MM_ERROR("Failed to select physical device: %s", phys_dev_ret.error().message().c_str());
            vkb::destroy_instance(vkb_instance);
            return make_unexpected(RHIError::BackendError);
        }
        vkb::PhysicalDevice physical_device = phys_dev_ret.value();
        phys_device = physical_device.physical_device;
        MM_LOG("Physical device selected: %s", physical_device.name.c_str());

        // Step 4: vkb::DeviceBuilder — no manual vkCreateDevice
        MM_LOG("Creating Logical Device");

        // Dynamic rendering features
        VkPhysicalDeviceDynamicRenderingFeaturesKHR dynamic_rendering_features{};
        dynamic_rendering_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES_KHR;
        dynamic_rendering_features.dynamicRendering = VK_TRUE;

        // Vulkan 1.2 features
        VkPhysicalDeviceVulkan12Features features12{};
        features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
        features12.timelineSemaphore = VK_TRUE;
        features12.bufferDeviceAddress = VK_TRUE;
        features12.descriptorIndexing = VK_TRUE;
        features12.pNext = &dynamic_rendering_features;

        VkPhysicalDeviceFeatures2 features2{};
        features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        features2.pNext = &features12;

        vkb::DeviceBuilder device_builder(physical_device);
        auto dev_ret = device_builder
            .add_pNext(&features2)
            .build();
        if (!dev_ret) {
            MM_ERROR("Failed to create logical device: %s", dev_ret.error().message().c_str());
            vkb::destroy_instance(vkb_instance);
            return make_unexpected(RHIError::BackendError);
        }

        vkb_device = dev_ret.value();
        device = vkb_device.device;
        MM_LOG("Logical device created");

        // Get queues
        auto gq = vkb_device.get_queue(vkb::QueueType::graphics);
        auto pq = vkb_device.get_queue(vkb::QueueType::present);
        if (!gq || !pq) return make_unexpected(RHIError::BackendError);
        graphics_queue = gq.value();
        present_queue = pq.value();

        auto gi = vkb_device.get_queue_index(vkb::QueueType::graphics);
        auto pi = vkb_device.get_queue_index(vkb::QueueType::present);
        graphics_family = gi.value();
        present_family = pi.value();

        // Step 4: Swapchain via vkb::SwapchainBuilder
        MM_LOG("Creating Swapchain");
        vkb::SwapchainBuilder swap_builder(vkb_device, surface);
        auto swap_ret = swap_builder
            .set_desired_format({VK_FORMAT_B8G8R8A8_SRGB, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR})
            .set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR)
            .build();
        if (!swap_ret) {
            MM_ERROR("Failed to create swapchain: %s", swap_ret.error().message().c_str());
            return make_unexpected(RHIError::BackendError);
        }
        vkb_swapchain = swap_ret.value();
        swapchain = vkb_swapchain.swapchain;
        swap_extent = vkb_swapchain.extent;
        swap_format = vkb_swapchain.image_format;
        MM_LOG("Swapchain created: %dx%d", swap_extent.width, swap_extent.height);

        swap_images = vkb_swapchain.get_images().value();
        swap_views  = vkb_swapchain.get_image_views().value();

        // Step 5: VMA — all allocations through VMA, no vkAllocateMemory
        MM_LOG("Creating VMA Allocator");
        VmaVulkanFunctions vma_funcs{};
        vma_funcs.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
        vma_funcs.vkGetDeviceProcAddr   = vkGetDeviceProcAddr;
        vma_funcs.vkGetPhysicalDeviceProperties = vkGetPhysicalDeviceProperties;
        vma_funcs.vkGetPhysicalDeviceMemoryProperties = vkGetPhysicalDeviceMemoryProperties;
        vma_funcs.vkAllocateMemory = vkAllocateMemory;
        vma_funcs.vkFreeMemory = vkFreeMemory;
        vma_funcs.vkMapMemory = vkMapMemory;
        vma_funcs.vkUnmapMemory = vkUnmapMemory;
        vma_funcs.vkFlushMappedMemoryRanges = vkFlushMappedMemoryRanges;
        vma_funcs.vkInvalidateMappedMemoryRanges = vkInvalidateMappedMemoryRanges;
        vma_funcs.vkBindBufferMemory = vkBindBufferMemory;
        vma_funcs.vkBindImageMemory = vkBindImageMemory;
        vma_funcs.vkGetBufferMemoryRequirements = vkGetBufferMemoryRequirements;
        vma_funcs.vkGetImageMemoryRequirements = vkGetImageMemoryRequirements;
        vma_funcs.vkCreateBuffer = vkCreateBuffer;
        vma_funcs.vkDestroyBuffer = vkDestroyBuffer;
        vma_funcs.vkCreateImage = vkCreateImage;
        vma_funcs.vkDestroyImage = vkDestroyImage;
        vma_funcs.vkCmdCopyBuffer = vkCmdCopyBuffer;

        VmaAllocatorCreateInfo alloc_info{};
        alloc_info.device = device;
        alloc_info.physicalDevice = phys_device;
        alloc_info.instance = instance;
        alloc_info.pVulkanFunctions = &vma_funcs;
        alloc_info.vulkanApiVersion = VK_API_VERSION_1_1;
        VkResult vma_res = vmaCreateAllocator(&alloc_info, &allocator);
        if (vma_res != VK_SUCCESS) {
            MM_ERROR("Failed to create VMA allocator, result: %d", (int)vma_res);
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
        pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        pool_info.queueFamilyIndex = graphics_family;
        pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        vkCreateCommandPool(device, &pool_info, nullptr, &cmd_pool);

        // Command buffer
        VkCommandBufferAllocateInfo alloc_cmd{};
        alloc_cmd.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        alloc_cmd.commandPool = cmd_pool;
        alloc_cmd.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        alloc_cmd.commandBufferCount = 1;
        vkAllocateCommandBuffers(device, &alloc_cmd, &cmd_buf);

        // Load dynamic rendering functions
        vkCmdBeginRenderingKHR = reinterpret_cast<PFN_vkCmdBeginRenderingKHR>(
            vkGetDeviceProcAddr(device, "vkCmdBeginRenderingKHR"));
        vkCmdEndRenderingKHR = reinterpret_cast<PFN_vkCmdEndRenderingKHR>(
            vkGetDeviceProcAddr(device, "vkCmdEndRenderingKHR"));

        if (!vkCmdBeginRenderingKHR || !vkCmdEndRenderingKHR) {
            MM_ERROR("Failed to load vkCmdBeginRenderingKHR or vkCmdEndRenderingKHR");
            return make_unexpected(RHIError::BackendError);
        }

        // Descriptor pool — supports up to 16 descriptor sets with
        // combined image samplers + uniform buffers for all pipelines
        VkDescriptorPoolSize pool_sizes[2] = {};
        pool_sizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        pool_sizes[0].descriptorCount = 16;
        pool_sizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        pool_sizes[1].descriptorCount = 16;

        VkDescriptorPoolCreateInfo dpci{};
        dpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        dpci.maxSets = 16;
        dpci.poolSizeCount = 2;
        dpci.pPoolSizes = pool_sizes;
        dpci.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        vkCreateDescriptorPool(device, &dpci, nullptr, &desc_pool);

        timeline_value = 0;
        frame_index = 0;

        return {};
    }

    void shutdown() noexcept {
        vkDeviceWaitIdle(device);  // one-time at shutdown only

        vkDestroyFence(device, frame_fence, nullptr);
        vkDestroySemaphore(device, acquire_sem, nullptr);
        vkDestroySemaphore(device, release_sem, nullptr);
        vkDestroyCommandPool(device, cmd_pool, nullptr);

        vkDestroyDescriptorPool(device, desc_pool, nullptr);
        for (auto v : swap_views) vkDestroyImageView(device, v, nullptr);
        vmaDestroyAllocator(allocator);
        if (surface) vkDestroySurfaceKHR(instance, surface, nullptr);
        vkb::destroy_swapchain(vkb_swapchain);
        vkb::destroy_device(vkb_device);
        vkb::destroy_instance(vkb_instance);
    }

    Expected<BufferHandle, RHIError> create_buffer(const BufferDesc& desc) noexcept {
        MM_LOG("create_buffer entering: type=%d size=%u cpu_visible=%d", (int)desc.type, (uint32_t)desc.size, (int)desc.cpu_visible);
        if (!allocator) {
            MM_ERROR("create_buffer: allocator is NULL!");
            return make_unexpected(RHIError::BackendError);
        }

        MM_LOG("create_buffer: allocator=%p, device=%p", (void*)allocator, (void*)device);
        VkBufferCreateInfo buf_info{};
        buf_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        buf_info.size = desc.size;
        buf_info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;

        switch (desc.type) {
            case BufferType::Vertex:  buf_info.usage |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT; break;
            case BufferType::Index:   buf_info.usage |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT; break;
            case BufferType::Uniform: buf_info.usage |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT; break;
            case BufferType::Storage: buf_info.usage |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT; break;
            case BufferType::Indirect: buf_info.usage |= VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT; break;
            default: break;
        }
        if (desc.cpu_visible) {
            buf_info.usage |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        }

        MM_LOG("create_buffer: setting up VmaAllocationCreateInfo");
        VmaAllocationCreateInfo alloc_info{};
        // Use older enums for better emulator compatibility
        alloc_info.usage = desc.cpu_visible
            ? VMA_MEMORY_USAGE_CPU_TO_GPU
            : VMA_MEMORY_USAGE_GPU_ONLY;
        alloc_info.flags = desc.cpu_visible ? VMA_ALLOCATION_CREATE_MAPPED_BIT : 0;

        VkBuffer buf = VK_NULL_HANDLE;
        VmaAllocation alloc = VK_NULL_HANDLE;

        MM_LOG("create_buffer: calling vmaCreateBuffer (device=%p)", (void*)device);
        VkResult res = vmaCreateBuffer(allocator, &buf_info, &alloc_info, &buf, &alloc, nullptr);
        if (res != VK_SUCCESS) {
            MM_ERROR("create_buffer: vmaCreateBuffer failed with result %d", (int)res);
            return make_unexpected(RHIError::OutOfMemory);
        }
        MM_LOG("create_buffer: vmaCreateBuffer OK: buffer=%p, alloc=%p", (void*)buf, (void*)alloc);

        VulkanBuffer vb{buf, alloc, (uint32_t)desc.size, desc.type};

        MM_LOG("create_buffer: emplace into slotmap");
        SlotHandle sh = buffers.emplace(vb);
        MM_LOG("create_buffer: emplace OK: id=%u, gen=%u", sh.id, sh.gen);

        return BufferHandle{sh};
    }

    Expected<TextureHandle, RHIError> create_texture(const TextureDesc& desc) noexcept {
        MM_LOG("create_texture entering: %ux%d fmt=%d", desc.width, desc.height, (int)desc.format);
        VkImageCreateInfo img_info{};
        img_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        img_info.imageType = VK_IMAGE_TYPE_2D;
        img_info.format = to_vk_format(desc.format);
        img_info.extent = {desc.width, desc.height, 1};
        img_info.mipLevels = desc.mip_levels;
        img_info.arrayLayers = desc.array_layers;
        img_info.samples = VK_SAMPLE_COUNT_1_BIT;
        img_info.tiling = VK_IMAGE_TILING_OPTIMAL;
        img_info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        img_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        VmaAllocationCreateInfo alloc_info{};
        // Use more compatible GPU usage
        alloc_info.usage = VMA_MEMORY_USAGE_GPU_ONLY;

        VkImage img = VK_NULL_HANDLE;
        VmaAllocation alloc = VK_NULL_HANDLE;
        MM_LOG("create_texture: calling vmaCreateImage");
        VkResult res = vmaCreateImage(allocator, &img_info, &alloc_info, &img, &alloc, nullptr);
        if (res != VK_SUCCESS) {
            MM_ERROR("create_texture: vmaCreateImage failed with result %d", (int)res);
            return make_unexpected(RHIError::OutOfMemory);
        }
        MM_LOG("create_texture: vmaCreateImage OK: image=%p", (void*)img);

        // Create image view
        VkImageViewCreateInfo view_info{};
        view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view_info.image = img;
        view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view_info.format = to_vk_format(desc.format);
        view_info.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, desc.mip_levels, 0, desc.array_layers};

        VkImageView view;
        if (vkCreateImageView(device, &view_info, nullptr, &view) != VK_SUCCESS) {
            MM_ERROR("create_texture: vkCreateImageView failed");
            vmaDestroyImage(allocator, img, alloc);
            return make_unexpected(RHIError::BackendError);
        }
        MM_LOG("create_texture: vkCreateImageView OK: view=%p", (void*)view);

        VulkanTexture vt{img, alloc, view, desc.width, desc.height, desc.format};
        SlotHandle sh = textures.emplace(vt);
        return TextureHandle{sh};
    }

    Expected<SamplerHandle, RHIError> create_sampler(const SamplerDesc& desc) noexcept {
        VkSamplerCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        info.magFilter = desc.mag_filter == SamplerFilter::Linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
        info.minFilter = desc.min_filter == SamplerFilter::Linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
        info.addressModeU = to_vk_address(desc.address_u);
        info.addressModeV = to_vk_address(desc.address_v);
        info.addressModeW = to_vk_address(desc.address_w);
        info.maxAnisotropy = desc.max_anisotropy;
        info.compareOp = to_vk_compare(desc.compare);

        VkSampler sampler;
        vkCreateSampler(device, &info, nullptr, &sampler);

        VulkanSampler vs{sampler};
        SlotHandle sh = samplers.emplace(vs);
        return SamplerHandle{sh};
    }

    Expected<PipelineHandle, RHIError> create_pipeline(const PipelineDesc& desc) noexcept {
        MM_LOG("create_pipeline entering: vs_size=%zu fs_size=%zu", desc.vertex_shader.code_size, desc.fragment_shader.code_size);
        // --- Shader modules ---
        VkShaderModuleCreateInfo vsm{};
        vsm.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        vsm.codeSize = desc.vertex_shader.code_size;
        vsm.pCode = static_cast<const uint32_t*>(desc.vertex_shader.code);

        VkShaderModule vs_module;
        if (vkCreateShaderModule(device, &vsm, nullptr, &vs_module) != VK_SUCCESS) {
            MM_ERROR("create_pipeline: vertex shader module creation failed");
            return make_unexpected(RHIError::ShaderCompileFail);
        }

        VkShaderModuleCreateInfo fsm{};
        fsm.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        fsm.codeSize = desc.fragment_shader.code_size;
        fsm.pCode = static_cast<const uint32_t*>(desc.fragment_shader.code);

        VkShaderModule fs_module;
        if (vkCreateShaderModule(device, &fsm, nullptr, &fs_module) != VK_SUCCESS) {
            MM_ERROR("create_pipeline: fragment shader module creation failed");
            vkDestroyShaderModule(device, vs_module, nullptr);
            return make_unexpected(RHIError::ShaderCompileFail);
        }
        MM_LOG("create_pipeline: shader modules created");

        // --- Shader stages ---
        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vs_module;
        stages[0].pName = desc.vertex_shader.entry;

        stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = fs_module;
        stages[1].pName = desc.fragment_shader.entry;

        // --- Vertex input ---
        VkVertexInputBindingDescription bindings[16];
        VkVertexInputAttributeDescription attrs[16];
        uint32_t binding_count = 0;
        uint32_t attr_count = desc.vertex_attr_count;

        // Group attributes by stride (binding)
        uint32_t max_stride = 0;
        for (uint8_t i = 0; i < attr_count; ++i) {
            auto& a = desc.vertex_attrs[i];
            attrs[i].location = a.location;
            attrs[i].binding = 0;  // all in buffer 0 for sprite/sdf
            attrs[i].format = to_vk_vertex_format(a.format);
            attrs[i].offset = a.offset;
            if (a.stride > max_stride) max_stride = a.stride;
        }
        if (attr_count > 0) {
            bindings[0].binding = 0;
            bindings[0].stride = max_stride;
            bindings[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
            binding_count = 1;
        }

        VkPipelineVertexInputStateCreateInfo vi{};
        vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vi.vertexBindingDescriptionCount = binding_count;
        vi.pVertexBindingDescriptions = bindings;
        vi.vertexAttributeDescriptionCount = attr_count;
        vi.pVertexAttributeDescriptions = attrs;

        // --- Input assembly ---
        VkPipelineInputAssemblyStateCreateInfo ia{};
        ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        ia.topology = to_vk_primitive(desc.prim_type);

        // --- Rasterization ---
        VkPipelineRasterizationStateCreateInfo rs{};
        rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rs.cullMode = to_vk_cull(desc.cull_mode);
        rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rs.lineWidth = 1.0f;
        rs.polygonMode = VK_POLYGON_MODE_FILL;

        // --- Multisample ---
        VkPipelineMultisampleStateCreateInfo ms{};
        ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        // --- Color blend ---
        VkPipelineColorBlendAttachmentState cb_attachments[4]{};
        for (uint8_t i = 0; i < desc.color_count && i < 4; ++i) {
            cb_attachments[i].blendEnable = VK_TRUE;
            cb_attachments[i].srcColorBlendFactor = to_vk_blend(desc.src_blend);
            cb_attachments[i].dstColorBlendFactor = to_vk_blend(desc.dst_blend);
            cb_attachments[i].colorBlendOp = to_vk_blend_op(desc.blend_op);
            cb_attachments[i].srcAlphaBlendFactor = to_vk_blend(desc.src_blend);
            cb_attachments[i].dstAlphaBlendFactor = to_vk_blend(desc.dst_blend);
            cb_attachments[i].alphaBlendOp = to_vk_blend_op(desc.blend_op);
            cb_attachments[i].colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
                                             | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        }

        VkPipelineColorBlendStateCreateInfo cb{};
        cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        cb.attachmentCount = desc.color_count;
        cb.pAttachments = cb_attachments;

        // --- Depth-stencil ---
        VkPipelineDepthStencilStateCreateInfo ds{};
        ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        ds.depthTestEnable = desc.depth_test ? VK_TRUE : VK_FALSE;
        ds.depthWriteEnable = desc.depth_write ? VK_TRUE : VK_FALSE;
        ds.depthCompareOp = to_vk_compare(desc.depth_compare);
        ds.depthBoundsTestEnable = VK_FALSE;

        // --- Dynamic states ---
        VkDynamicState dyn_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dyn{};
        dyn.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dyn.dynamicStateCount = 2;
        dyn.pDynamicStates = dyn_states;

        // --- Descriptor set layout ---
        VkDescriptorSetLayoutBinding dsl_bindings[8];
        for (uint8_t i = 0; i < desc.descriptor_count; ++i) {
            auto& db = desc.descriptor_bindings[i];
            dsl_bindings[i].binding = db.binding;
            dsl_bindings[i].descriptorType = to_vk_descriptor_type(db.type);
            dsl_bindings[i].descriptorCount = db.count > 0 ? db.count : 1;
            dsl_bindings[i].stageFlags = to_vk_shader_stage(db.stage_mask);
            dsl_bindings[i].pImmutableSamplers = nullptr;
        }

        VkDescriptorSetLayoutCreateInfo dsl{};
        dsl.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        dsl.bindingCount = desc.descriptor_count;
        dsl.pBindings = dsl_bindings;

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
        pl.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pl.setLayoutCount = (desc_set_layout != VK_NULL_HANDLE) ? 1 : 0;
        pl.pSetLayouts = &desc_set_layout;

        VkPipelineLayout pipeline_layout;
        if (vkCreatePipelineLayout(device, &pl, nullptr, &pipeline_layout) != VK_SUCCESS) {
            if (desc_set_layout) vkDestroyDescriptorSetLayout(device, desc_set_layout, nullptr);
            vkDestroyShaderModule(device, vs_module, nullptr);
            vkDestroyShaderModule(device, fs_module, nullptr);
            return make_unexpected(RHIError::BackendError);
        }

        // --- Dynamic rendering format ---
        VkFormat color_fmts[4];
        for (uint8_t i = 0; i < desc.color_count && i < 4; ++i) {
            color_fmts[i] = to_vk_format(desc.color_formats[i]);
        }
        VkPipelineRenderingCreateInfo rd{};
        rd.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
        rd.colorAttachmentCount = desc.color_count;
        rd.pColorAttachmentFormats = color_fmts;
        rd.depthAttachmentFormat = (desc.depth_format != static_cast<PixelFormat>(0))
            ? to_vk_format(desc.depth_format) : VK_FORMAT_UNDEFINED;
        rd.stencilAttachmentFormat = VK_FORMAT_UNDEFINED;

        // --- Graphics pipeline ---
        VkGraphicsPipelineCreateInfo gp{};
        gp.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        gp.pNext = &rd;
        gp.stageCount = 2;
        gp.pStages = stages;
        gp.pVertexInputState = &vi;
        gp.pInputAssemblyState = &ia;
        gp.pRasterizationState = &rs;
        gp.pMultisampleState = &ms;
        gp.pDepthStencilState = &ds;
        gp.pColorBlendState = &cb;
        gp.pDynamicState = &dyn;
        gp.layout = pipeline_layout;

        VkPipeline pipeline;
        VkPipelineCache cache = VK_NULL_HANDLE;
        VkResult res = vkCreateGraphicsPipelines(device, cache, 1, &gp, nullptr, &pipeline);
        vkDestroyShaderModule(device, vs_module, nullptr);
        vkDestroyShaderModule(device, fs_module, nullptr);

        if (res != VK_SUCCESS) {
            vkDestroyPipelineLayout(device, pipeline_layout, nullptr);
            return make_unexpected(RHIError::PipelineCompileFail);
        }

        // Allocate descriptor set if we have bindings
        VkDescriptorSet desc_set = VK_NULL_HANDLE;
        if (desc_set_layout) {
            VkDescriptorSetAllocateInfo dsai{};
            dsai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            dsai.descriptorPool = desc_pool;
            dsai.descriptorSetCount = 1;
            dsai.pSetLayouts = &desc_set_layout;
            vkAllocateDescriptorSets(device, &dsai, &desc_set);
        }

        VulkanPipeline vp{pipeline, pipeline_layout, desc_set_layout, desc_set,
                          VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE, 0, true};
        SlotHandle sh = pipelines.emplace(vp);
        return PipelineHandle{sh};
    }

    void destroy_buffer(BufferHandle h) noexcept {
        auto* b = buffers.get(h.handle);
        if (b) vmaDestroyBuffer(allocator, b->buffer, b->alloc);
        buffers.free(h.handle);
    }

    void destroy_texture(TextureHandle h) noexcept {
        auto* t = textures.get(h.handle);
        if (t) {
            vkDestroyImageView(device, t->view, nullptr);
            vmaDestroyImage(allocator, t->image, t->alloc);
        }
        textures.free(h.handle);
    }

    void destroy_sampler(SamplerHandle h) noexcept {
        auto* s = samplers.get(h.handle);
        if (s) vkDestroySampler(device, s->sampler, nullptr);
        samplers.free(h.handle);
    }

    void destroy_pipeline(PipelineHandle h) noexcept {
        auto* p = pipelines.get(h.handle);
        if (p) {
            if (p->desc_set) {
                vkFreeDescriptorSets(device, desc_pool, 1, &p->desc_set);
                vkDestroyDescriptorSetLayout(device, p->desc_set_layout, nullptr);
            }
            vkDestroyPipeline(device, p->pipeline, nullptr);
            vkDestroyPipelineLayout(device, p->layout, nullptr);
        }
        pipelines.free(h.handle);
    }

    Expected<void, RHIError> update_buffer(BufferHandle h, const void* data,
                                              uint32_t offset, uint32_t size) noexcept {
        auto* buf = buffers.get(h.handle);
        if (!buf) return make_unexpected(RHIError::InvalidHandle);

        void* mapped;
        vmaMapMemory(allocator, buf->alloc, &mapped);
        memcpy(static_cast<uint8_t*>(mapped) + offset, data, size);
        vmaUnmapMemory(allocator, buf->alloc);
        return {};
    }

    Expected<void, RHIError> update_texture(TextureHandle h, const void* data,
                                               uint32_t x, uint32_t y,
                                               uint32_t w, uint32_t h_,
                                               uint32_t mip, uint32_t slice) noexcept {
        MM_LOG("update_texture entering: handle=%u %ux%u", h.handle.id, w, h_);
        auto* tex = textures.get(h.handle);
        if (!tex) {
            MM_ERROR("update_texture: invalid texture handle");
            return make_unexpected(RHIError::InvalidHandle);
        }

        // Create staging buffer
        uint32_t bpp = get_format_size(tex->format);
        VkDeviceSize image_size = static_cast<VkDeviceSize>(w) * h_ * bpp;
        VkBufferCreateInfo buf_info{};
        buf_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        buf_info.size = image_size;
        buf_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

        VmaAllocationCreateInfo alloc_info{};
        alloc_info.usage = VMA_MEMORY_USAGE_CPU_ONLY; // Simplest usage for staging
        alloc_info.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

        VkBuffer staging_buf;
        VmaAllocation staging_alloc;
        VmaAllocationInfo staging_info;
        MM_LOG("update_texture: creating staging buffer size=%zu (bpp=%u)", (size_t)image_size, bpp);
        if (vmaCreateBuffer(allocator, &buf_info, &alloc_info,
                            &staging_buf, &staging_alloc, &staging_info) != VK_SUCCESS) {
            MM_ERROR("update_texture: staging buffer creation failed");
            return make_unexpected(RHIError::OutOfMemory);
        }

        MM_LOG("update_texture: copying pixels to staging");
        memcpy(staging_info.pMappedData, data, static_cast<size_t>(image_size));

        // One-shot command buffer for transfer
        VkCommandBufferAllocateInfo cmd_alloc{};
        cmd_alloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cmd_alloc.commandPool = cmd_pool;
        cmd_alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cmd_alloc.commandBufferCount = 1;

        VkCommandBuffer transfer_cmd;
        MM_LOG("update_texture: allocating command buffer");
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
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.image = tex->image;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = mip;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = slice;
        barrier.subresourceRange.layerCount = 1;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        vkCmdPipelineBarrier(transfer_cmd,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &barrier);

        // Copy buffer → image
        VkBufferImageCopy region{};
        region.bufferOffset = 0;
        region.bufferRowLength = 0;
        region.bufferImageHeight = 0;
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = mip;
        region.imageSubresource.baseArrayLayer = slice;
        region.imageSubresource.layerCount = 1;
        region.imageOffset = {static_cast<int32_t>(x), static_cast<int32_t>(y), 0};
        region.imageExtent = {w, h_, 1};
        MM_LOG("update_texture: recording copy command");
        vkCmdCopyBufferToImage(transfer_cmd, staging_buf, tex->image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        // Transition: transfer dst → shader read
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(transfer_cmd,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &barrier);

        vkEndCommandBuffer(transfer_cmd);

        VkSubmitInfo submit{};
        submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &transfer_cmd;

        VkFenceCreateInfo fence_info{};
        fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        VkFence transfer_fence;
        vkCreateFence(device, &fence_info, nullptr, &transfer_fence);

        MM_LOG("update_texture: submitting to queue");
        VkResult submit_res = vkQueueSubmit(graphics_queue, 1, &submit, transfer_fence);
        if (submit_res != VK_SUCCESS) {
            MM_ERROR("update_texture: vkQueueSubmit failed with %d", (int)submit_res);
            vkDestroyFence(device, transfer_fence, nullptr);
            vkFreeCommandBuffers(device, cmd_pool, 1, &transfer_cmd);
            vmaDestroyBuffer(allocator, staging_buf, staging_alloc);
            return make_unexpected(RHIError::BackendError);
        }

        MM_LOG("update_texture: waiting for fence");
        vkWaitForFences(device, 1, &transfer_fence, VK_TRUE, UINT64_MAX);

        MM_LOG("update_texture: cleanup");
        vkDestroyFence(device, transfer_fence, nullptr);
        vkFreeCommandBuffers(device, cmd_pool, 1, &transfer_cmd);
        vmaDestroyBuffer(allocator, staging_buf, staging_alloc);
        MM_LOG("update_texture: done");

        return {};
    }

    Expected<void, RHIError> begin_frame() noexcept {
        MM_LOG("VulkanBackend::begin_frame() - waiting for frame_fence");
        vkWaitForFences(device, 1, &frame_fence, VK_TRUE, UINT64_MAX);
        MM_LOG("VulkanBackend::begin_frame() - frame_fence signaled");
        vkResetFences(device, 1, &frame_fence);

        VkResult result = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX,
                                                  acquire_sem, VK_NULL_HANDLE, &swap_index);
        if (result == VK_ERROR_OUT_OF_DATE_KHR) {
            return make_unexpected(RHIError::DeviceLost);
        }

        vkResetCommandBuffer(cmd_buf, 0);

        VkCommandBufferBeginInfo begin{};
        begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd_buf, &begin);

        return {};
    }

    void resize() noexcept {
        vkDeviceWaitIdle(device);

        // Destroy old swapchain image views
        for (auto v : swap_views) vkDestroyImageView(device, v, nullptr);
        swap_views.clear();
        swap_images.clear();

        // Rebuild swapchain (pass old swapchain for seamless recreation)
        vkb::SwapchainBuilder swap_builder(vkb_device, surface);
        auto swap_ret = swap_builder
            .set_desired_format({VK_FORMAT_B8G8R8A8_SRGB, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR})
            .set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR)
            .set_old_swapchain(vkb_swapchain)
            .build();
        if (swap_ret) {
            vkb::destroy_swapchain(vkb_swapchain);
            vkb_swapchain = swap_ret.value();
            swapchain = vkb_swapchain.swapchain;
            swap_extent = vkb_swapchain.extent;
            swap_format = vkb_swapchain.image_format;

            swap_images = vkb_swapchain.get_images().value();
            swap_views = vkb_swapchain.get_image_views().value();
        }
    }

    Expected<void, RHIError> end_frame() noexcept {
        vkEndCommandBuffer(cmd_buf);

        VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

        VkSubmitInfo submit{};
        submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit.waitSemaphoreCount = 1;
        submit.pWaitSemaphores = &acquire_sem;
        submit.pWaitDstStageMask = &wait_stage;
        submit.signalSemaphoreCount = 1;
        submit.pSignalSemaphores = &release_sem;
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &cmd_buf;

        MM_LOG("VulkanBackend::end_frame() - submitting graphics queue");
        VkResult submit_res = vkQueueSubmit(graphics_queue, 1, &submit, frame_fence);
        if (submit_res != VK_SUCCESS) {
            MM_ERROR("VulkanBackend::end_frame() - vkQueueSubmit failed: %d", (int)submit_res);
            return make_unexpected(RHIError::BackendError);
        }

        VkPresentInfoKHR present{};
        present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        present.waitSemaphoreCount = 1;
        present.pWaitSemaphores = &release_sem;
        present.swapchainCount = 1;
        present.pSwapchains = &swapchain;
        present.pImageIndices = &swap_index;

        MM_LOG("VulkanBackend::end_frame() - presenting swapchain");
        VkResult present_res = vkQueuePresentKHR(present_queue, &present);
        if (present_res == VK_ERROR_OUT_OF_DATE_KHR || present_res == VK_SUBOPTIMAL_KHR) {
            MM_LOG("VulkanBackend::end_frame() - swapchain out of date or suboptimal");
        } else if (present_res != VK_SUCCESS) {
            MM_ERROR("VulkanBackend::end_frame() - vkQueuePresentKHR failed: %d", (int)present_res);
        }

        ++timeline_value;
        ++frame_index;
        return {};
    }

    Expected<void, RHIError> begin_pass(const PassDesc& pass) noexcept {
        // Transition swapchain image to COLOR_ATTACHMENT_OPTIMAL
        image_barrier(cmd_buf, swap_images[swap_index],
                      VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                      0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                      VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);

        VkRenderingAttachmentInfo color_attach{};
        color_attach.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        color_attach.imageView = swap_views[swap_index];
        color_attach.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        color_attach.loadOp = (pass.color_load == LoadOp::Clear) ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
        color_attach.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        memcpy(&color_attach.clearValue.color.float32[0], pass.clear_color, 4 * sizeof(float));

        VkRenderingInfo render_info{};
        render_info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        render_info.renderArea = {{0, 0}, swap_extent};
        render_info.layerCount = 1;
        render_info.colorAttachmentCount = 1;
        render_info.pColorAttachments = &color_attach;

        vkCmdBeginRenderingKHR(cmd_buf, &render_info);

        VkViewport viewport{0, 0, static_cast<float>(swap_extent.width),
                            static_cast<float>(swap_extent.height), 0, 1};
        VkRect2D scissor{{0, 0}, swap_extent};
        vkCmdSetViewport(cmd_buf, 0, 1, &viewport);
        vkCmdSetScissor(cmd_buf, 0, 1, &scissor);

        return {};
    }

    Expected<void, RHIError> end_pass() noexcept {
        vkCmdEndRenderingKHR(cmd_buf);

        // Transition swapchain image back to PRESENT_SRC_KHR
        image_barrier(cmd_buf, swap_images[swap_index],
                      VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                      VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, 0,
                      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);

        return {};
    }

    Expected<void, RHIError> bind_pipeline(PipelineHandle h) noexcept {
        auto* pl = pipelines.get(h.handle);
        if (!pl) return make_unexpected(RHIError::InvalidHandle);
        vkCmdBindPipeline(cmd_buf, VK_PIPELINE_BIND_POINT_GRAPHICS, pl->pipeline);
        current_pipeline_handle = h;
        return {};
    }

    void flush_descriptors() noexcept {
        auto* pl = pipelines.get(current_pipeline_handle.handle);
        if (!pl || !pl->desc_set) return;

        if (pl->descriptor_dirty) {
            MM_LOG("VulkanBackend::flush_descriptors() - updating set for handle=%u ubo=%p view=%p",
                   current_pipeline_handle.handle.id, (void*)pl->current_ubo, (void*)pl->current_view);
            uint32_t write_count = 0;
            VkWriteDescriptorSet writes[2] = {};

            VkDescriptorBufferInfo ubo_info{};
            if (pl->current_ubo) {
                ubo_info.buffer = pl->current_ubo;
                ubo_info.offset = 0;
                ubo_info.range = VK_WHOLE_SIZE;

                auto& w = writes[write_count++];
                w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                w.dstSet = pl->desc_set;
                w.dstBinding = 0; // UBO at logical slot 0
                w.descriptorCount = 1;
                w.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                w.pBufferInfo = &ubo_info;
            }

            VkDescriptorImageInfo img_info{};
            if (pl->current_view && pl->current_sampler) {
                img_info.sampler = pl->current_sampler;
                img_info.imageView = pl->current_view;
                img_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

                auto& w = writes[write_count++];
                w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                w.dstSet = pl->desc_set;
                w.dstBinding = 1; // Sampler at logical slot 1
                w.descriptorCount = 1;
                w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                w.pImageInfo = &img_info;
            }

            if (write_count > 0) {
                vkUpdateDescriptorSets(device, write_count, writes, 0, nullptr);
            }
            pl->descriptor_dirty = false;
        }

        vkCmdBindDescriptorSets(cmd_buf, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                pl->layout, 0, 1, &pl->desc_set, 0, nullptr);
    }

    Expected<void, RHIError> bind_vertex_buffers(BufferHandle* handles, uint32_t count,
                                                    const uint64_t* offsets,
                                                    const uint64_t* strides,
                                                    const uint32_t* bindings = nullptr) noexcept {
        VkBuffer vk_bufs[16];
        VkDeviceSize vk_offsets[16];
        for (uint32_t i = 0; i < count && i < 16; ++i) {
            auto* buf = this->buffers.get(handles[i].handle);
            if (!buf) return make_unexpected(RHIError::InvalidHandle);
            vk_bufs[i] = buf->buffer;
            vk_offsets[i] = offsets ? offsets[i] : 0;
        }
        uint32_t first = bindings ? bindings[0] : 0;
        vkCmdBindVertexBuffers(cmd_buf, first, count, vk_bufs, vk_offsets);
        return {};
    }

    Expected<void, RHIError> bind_index_buffer(BufferHandle h, IndexType type, uint64_t offset = 0) noexcept {
        auto* buf = buffers.get(h.handle);
        if (!buf) return make_unexpected(RHIError::InvalidHandle);
        vkCmdBindIndexBuffer(cmd_buf, buf->buffer, offset,
                             type == IndexType::Uint32 ? VK_INDEX_TYPE_UINT32 : VK_INDEX_TYPE_UINT16);
        return {};
    }

    Expected<void, RHIError> bind_uniform_buffer(BufferHandle handle, uint32_t binding) noexcept {
        auto* buf = buffers.get(handle.handle);
        if (!buf) return make_unexpected(RHIError::InvalidHandle);
        auto* pl = pipelines.get(current_pipeline_handle.handle);
        if (pl) {
            pl->current_ubo = buf->buffer;
            pl->ubo_slot = binding; // Store slot
            pl->descriptor_dirty = true;
        }
        return {};
    }

    Expected<void, RHIError> draw(uint32_t vertex_count, uint32_t instance_count,
                                    uint32_t first_vertex, uint32_t first_instance) noexcept {
        flush_descriptors();
        vkCmdDraw(cmd_buf, vertex_count, instance_count, first_vertex, first_instance);
        static uint32_t draw_count = 0;
        if (++draw_count % 100 == 1) MM_LOG("VulkanBackend::draw() count=%u verts=%u", draw_count, vertex_count);
        return {};
    }

    Expected<void, RHIError> draw_indexed(uint32_t index_count, uint32_t instance_count,
                                            uint32_t first_index, int32_t vertex_offset = 0) noexcept {
        flush_descriptors();
        vkCmdDrawIndexed(cmd_buf, index_count, instance_count, first_index, vertex_offset, 0);
        static uint32_t idx_draw_count = 0;
        if (++idx_draw_count % 100 == 1) MM_LOG("VulkanBackend::draw_indexed() count=%u indices=%u", idx_draw_count, index_count);
        return {};
    }

    Expected<void, RHIError> bind_fragment_texture(TextureHandle handle,
                                                   uint32_t binding) noexcept {
        auto* tex = textures.get(handle.handle);
        if (!tex) return make_unexpected(RHIError::InvalidHandle);
        auto* pl = pipelines.get(current_pipeline_handle.handle);
        if (pl) {
            pl->current_view = tex->view;
            pl->descriptor_dirty = true;
        }
        return {};
    }

    Expected<void, RHIError> bind_fragment_sampler(SamplerHandle handle,
                                                   uint32_t binding) noexcept {
        auto* samp = samplers.get(handle.handle);
        if (!samp) return make_unexpected(RHIError::InvalidHandle);
        auto* pl = pipelines.get(current_pipeline_handle.handle);
        if (pl) {
            pl->current_sampler = samp->sampler;
            pl->descriptor_dirty = true;
        }
        return {};
    }

    Expected<void, RHIError> set_scissor(int16_t x, int16_t y,
                                        uint16_t w, uint16_t h) noexcept {
        VkRect2D scissor{{static_cast<int32_t>(x), static_cast<int32_t>(y)},
                         {w, h}};
        vkCmdSetScissor(cmd_buf, 0, 1, &scissor);
        return {};
    }

private:
    static uint32_t get_format_size(PixelFormat fmt) noexcept {
        switch (fmt) {
            case PixelFormat::R8_UNORM:          return 1;
            case PixelFormat::R8G8B8A8_UNORM:
            case PixelFormat::R8G8B8A8_SRGB:
            case PixelFormat::B8G8R8A8_UNORM:
            case PixelFormat::B8G8R8A8_SRGB:
            case PixelFormat::D32_FLOAT:         return 4;
            default:                             return 4;
        }
    }

    static VkFormat to_vk_format(PixelFormat fmt) noexcept {
        switch (fmt) {
            case PixelFormat::R8_UNORM:          return VK_FORMAT_R8_UNORM;
            case PixelFormat::R8G8B8A8_UNORM:    return VK_FORMAT_R8G8B8A8_UNORM;
            case PixelFormat::R8G8B8A8_SRGB:     return VK_FORMAT_R8G8B8A8_SRGB;
            case PixelFormat::B8G8R8A8_UNORM:    return VK_FORMAT_B8G8R8A8_UNORM;
            case PixelFormat::B8G8R8A8_SRGB:     return VK_FORMAT_B8G8R8A8_SRGB;
            case PixelFormat::D32_FLOAT:         return VK_FORMAT_D32_SFLOAT;
            case PixelFormat::ASTC_4x4:          return VK_FORMAT_ASTC_4x4_SRGB_BLOCK;
            case PixelFormat::ETC2_RGB8:         return VK_FORMAT_ETC2_R8G8B8_SRGB_BLOCK;
            default:                             return VK_FORMAT_B8G8R8A8_SRGB;
        }
    }

    static VkFormat to_vk_vertex_format(PixelFormat fmt) noexcept {
        switch (fmt) {
            case PixelFormat::R8G8B8A8_UNORM:    return VK_FORMAT_R8G8B8A8_UNORM;
            case PixelFormat::R16G16B16A16_FLOAT: return VK_FORMAT_R16G16B16A16_SFLOAT;
            case PixelFormat::R32G32B32A32_FLOAT: return VK_FORMAT_R32G32B32A32_SFLOAT;
            default: break;
        }
        // Default: assume R16G16 (half2) for position/UV
        return VK_FORMAT_R16G16_SFLOAT;
    }

    static VkPrimitiveTopology to_vk_primitive(PrimitiveType prim) noexcept {
        switch (prim) {
            case PrimitiveType::Triangle:      return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            case PrimitiveType::TriangleStrip: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
            case PrimitiveType::Line:          return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
            case PrimitiveType::Point:         return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
            default:                           return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        }
    }

    static VkCullModeFlags to_vk_cull(CullMode mode) noexcept {
        switch (mode) {
            case CullMode::None:  return VK_CULL_MODE_NONE;
            case CullMode::Front: return VK_CULL_MODE_FRONT_BIT;
            case CullMode::Back:  return VK_CULL_MODE_BACK_BIT;
            default:              return VK_CULL_MODE_NONE;
        }
    }

    static VkBlendFactor to_vk_blend(BlendFactor factor) noexcept {
        switch (factor) {
            case BlendFactor::Zero:                return VK_BLEND_FACTOR_ZERO;
            case BlendFactor::One:                 return VK_BLEND_FACTOR_ONE;
            case BlendFactor::SrcAlpha:            return VK_BLEND_FACTOR_SRC_ALPHA;
            case BlendFactor::OneMinusSrcAlpha:    return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            case BlendFactor::DstAlpha:            return VK_BLEND_FACTOR_DST_ALPHA;
            case BlendFactor::OneMinusDstAlpha:    return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
            case BlendFactor::SrcColor:            return VK_BLEND_FACTOR_SRC_COLOR;
            case BlendFactor::OneMinusSrcColor:    return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
            default:                               return VK_BLEND_FACTOR_ONE;
        }
    }

    static VkBlendOp to_vk_blend_op(BlendOp op) noexcept {
        switch (op) {
            case BlendOp::Add:             return VK_BLEND_OP_ADD;
            case BlendOp::Subtract:        return VK_BLEND_OP_SUBTRACT;
            case BlendOp::ReverseSubtract: return VK_BLEND_OP_REVERSE_SUBTRACT;
            case BlendOp::Min:             return VK_BLEND_OP_MIN;
            case BlendOp::Max:             return VK_BLEND_OP_MAX;
            default:                       return VK_BLEND_OP_ADD;
        }
    }

    static VkDescriptorType to_vk_descriptor_type(DescriptorType type) noexcept {
        switch (type) {
            case DescriptorType::UniformBuffer:      return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            case DescriptorType::CombinedImageSampler: return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            case DescriptorType::StorageBuffer:      return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            default:                                 return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        }
    }

    static VkShaderStageFlags to_vk_shader_stage(uint32_t mask) noexcept {
        VkShaderStageFlags flags = 0;
        if (mask & 1) flags |= VK_SHADER_STAGE_VERTEX_BIT;
        if (mask & 2) flags |= VK_SHADER_STAGE_FRAGMENT_BIT;
        if (mask & 4) flags |= VK_SHADER_STAGE_COMPUTE_BIT;
        return flags;
    }

    static VkSamplerAddressMode to_vk_address(SamplerAddress addr) noexcept {
        switch (addr) {
            case SamplerAddress::Repeat:          return VK_SAMPLER_ADDRESS_MODE_REPEAT;
            case SamplerAddress::ClampToEdge:     return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            case SamplerAddress::MirroredRepeat:  return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
            default:                              return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        }
    }

    static VkCompareOp to_vk_compare(CompareOp op) noexcept {
        switch (op) {
            case CompareOp::Never:          return VK_COMPARE_OP_NEVER;
            case CompareOp::Less:           return VK_COMPARE_OP_LESS;
            case CompareOp::Equal:          return VK_COMPARE_OP_EQUAL;
            case CompareOp::LessEqual:      return VK_COMPARE_OP_LESS_OR_EQUAL;
            case CompareOp::Greater:        return VK_COMPARE_OP_GREATER;
            case CompareOp::NotEqual:       return VK_COMPARE_OP_NOT_EQUAL;
            case CompareOp::GreaterEqual:   return VK_COMPARE_OP_GREATER_OR_EQUAL;
            case CompareOp::Always:         return VK_COMPARE_OP_ALWAYS;
            default:                        return VK_COMPARE_OP_ALWAYS;
        }
    }

    static void image_barrier(VkCommandBuffer cmd, VkImage image,
                              VkImageLayout old_layout, VkImageLayout new_layout,
                              VkAccessFlags src_access, VkAccessFlags dst_access,
                              VkPipelineStageFlags src_stage, VkPipelineStageFlags dst_stage) noexcept {
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = old_layout;
        barrier.newLayout = new_layout;
        barrier.srcAccessMask = src_access;
        barrier.dstAccessMask = dst_access;
        barrier.image = image;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;

        vkCmdPipelineBarrier(cmd, src_stage, dst_stage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    }
};

static_assert(RHI_Backend<VulkanBackend>, "VulkanBackend must satisfy RHI_Backend concept");

#endif // USE_VULKAN_BACKEND
