#pragma once

struct RenderContext
{
    struct Device
    {
        IDXGISwapChain4* pSwapChain;
        uint8_t pad8[0xB0 - 0x8];
    };

    RenderContext() = delete;
    ~RenderContext() = delete;

    static RenderContext* GetInstance() noexcept;

    uint8_t pad0[0xC97F38];
    Device devices[0x30]; // Count unknown, it is at least 0x20
    uint8_t pad[0x13BC4D0 - (0xC97F38 + sizeof(devices))];
    ID3D12CommandQueue* pDirectQueue; // 0x13BC4D0
};

static_assert(sizeof(RenderContext::Device) == 0xB0);
static_assert(offsetof(RenderContext, devices) == 0xC97F38);
static_assert(offsetof(RenderContext, pDirectQueue) == 0x13BC4D0);