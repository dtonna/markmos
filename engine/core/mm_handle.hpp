#pragma once
#include <cstdint>
#include <type_traits>

struct SlotHandle {
    static constexpr uint32_t   INVALID_ID = 0xFFFFFFFFu;

    uint32_t                    id         = INVALID_ID;
    uint16_t                    gen        = 1;
    uint8_t                     pad[2]{};

    static constexpr SlotHandle invalid() noexcept {
        SlotHandle h{};
        h.id  = INVALID_ID;
        h.gen = 1;
        return h;
    }

    constexpr bool        is_valid() const noexcept { return id != INVALID_ID; }

    friend constexpr bool operator==(SlotHandle a, SlotHandle b) noexcept = default;
};

static_assert(sizeof(SlotHandle) == 8);
static_assert(std::is_trivially_copyable_v<SlotHandle>);

template <typename Tag> struct TypedHandle {
    SlotHandle                   handle = SlotHandle::invalid();

    static constexpr TypedHandle invalid() noexcept { return {}; }

    constexpr bool               is_valid() const noexcept { return handle.is_valid(); }

    constexpr explicit           operator bool() const noexcept { return is_valid(); }

    friend constexpr bool        operator==(TypedHandle a, TypedHandle b) noexcept = default;
};

static_assert(sizeof(TypedHandle<void>) == sizeof(SlotHandle));

using BufferHandle   = TypedHandle<class BufferTag>;
using TextureHandle  = TypedHandle<class TextureTag>;
using PipelineHandle = TypedHandle<class PipelineTag>;
using SamplerHandle  = TypedHandle<class SamplerTag>;
using MeshHandle     = TypedHandle<class MeshTag>;
using MaterialHandle = TypedHandle<class MaterialTag>;
using TweenHandle    = TypedHandle<class TweenTag>;
