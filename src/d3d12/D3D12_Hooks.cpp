#include "CET.h"

#include <stdafx.h>

#include "D3D12.h"
#include "reverse/Addresses.h"
#include "reverse/RenderContext.h"

#include <kiero/kiero.h>

#include <VersionHelpers.h>

#include <chrono>
#include <thread>
#include <openvr.h>
#include <common/HelperMath.h>

vr::TrackedDevicePose_t m_rTrackedDevicePose[vr::k_unMaxTrackedDeviceCount];
Matrix4x4 m_rmat4DevicePose[vr::k_unMaxTrackedDeviceCount];

HRESULT D3D12::PresentDownlevel(
    ID3D12CommandQueueDownlevel* apCommandQueueDownlevel, ID3D12GraphicsCommandList* apOpenCommandList, ID3D12Resource* apSourceTex2D, HWND ahWindow,
    D3D12_DOWNLEVEL_PRESENT_FLAGS aFlags)
{
    if (CET::Get().GetOptions().Patches.DisableWin7Vsync)
        aFlags &= ~D3D12_DOWNLEVEL_PRESENT_FLAG_WAIT_FOR_VBLANK;

    auto& d3d12 = CET::Get().GetD3D12();

    // On Windows 7 there is no swap chain to query the current backbuffer index. Instead do a reverse lookup in the
    // known backbuffer list
    const auto cbegin = d3d12.m_downlevelBackbuffers.size() >= g_numDownlevelBackbuffersRequired ? d3d12.m_downlevelBackbuffers.cend() - g_numDownlevelBackbuffersRequired
                                                                                                 : d3d12.m_downlevelBackbuffers.cbegin();
    auto it = std::find_if(cbegin, d3d12.m_downlevelBackbuffers.cend(), [apSourceTex2D](const auto& downlevelBackbuffer) { return downlevelBackbuffer.Get() == apSourceTex2D; });
    if (it == d3d12.m_downlevelBackbuffers.cend())
    {
        if (d3d12.m_initialized)
        {
            // Already initialized - assume the window was resized and reset state
            d3d12.ResetState();
        }

        // Add the buffer to the list
        d3d12.m_downlevelBackbuffers.emplace_back(apSourceTex2D);
        it = d3d12.m_downlevelBackbuffers.cend() - 1;
    }

    // Limit to at most 3 buffers
    const size_t numBackbuffers = std::min<size_t>(d3d12.m_downlevelBackbuffers.size(), g_numDownlevelBackbuffersRequired);
    const size_t skip = d3d12.m_downlevelBackbuffers.size() - numBackbuffers;
    d3d12.m_downlevelBackbuffers.erase(d3d12.m_downlevelBackbuffers.cbegin(), d3d12.m_downlevelBackbuffers.cbegin() + skip);

    // Determine the current buffer index
    d3d12.m_downlevelBufferIndex = static_cast<uint32_t>(std::distance(d3d12.m_downlevelBackbuffers.cbegin() + skip, it));

    if (d3d12.InitializeDownlevel(d3d12.m_pCommandQueue.Get(), apSourceTex2D, ahWindow))
        d3d12.Update();

    return d3d12.m_realPresentD3D12Downlevel(apCommandQueueDownlevel, apOpenCommandList, apSourceTex2D, ahWindow, aFlags);
}

HRESULT D3D12::CreateCommittedResource(
    ID3D12Device* apDevice, const D3D12_HEAP_PROPERTIES* acpHeapProperties, D3D12_HEAP_FLAGS aHeapFlags, const D3D12_RESOURCE_DESC* acpDesc,
    D3D12_RESOURCE_STATES aInitialResourceState, const D3D12_CLEAR_VALUE* acpOptimizedClearValue, const IID* acpRIID, void** appvResource)
{
    auto& d3d12 = CET::Get().GetD3D12();

    // Check if this is a backbuffer resource being created
    bool isBackBuffer = false;
    if (acpHeapProperties != nullptr && acpHeapProperties->Type == D3D12_HEAP_TYPE_DEFAULT && aHeapFlags == D3D12_HEAP_FLAG_NONE && acpDesc != nullptr &&
        acpDesc->Flags == D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET && aInitialResourceState == D3D12_RESOURCE_STATE_COMMON && acpOptimizedClearValue == nullptr &&
        acpRIID != nullptr && IsEqualGUID(*acpRIID, __uuidof(ID3D12Resource)))
    {
        isBackBuffer = true;
    }

    HRESULT result = d3d12.m_realCreateCommittedResource(apDevice, acpHeapProperties, aHeapFlags, acpDesc, aInitialResourceState, acpOptimizedClearValue, acpRIID, appvResource);

    if (SUCCEEDED(result) && isBackBuffer)
    {
        // Store the returned resource
        d3d12.m_downlevelBackbuffers.emplace_back(static_cast<ID3D12Resource*>(*appvResource));
        spdlog::debug("D3D12::CreateCommittedResourceD3D12() - found valid backbuffer target at {}.", *appvResource);

        if (d3d12.m_initialized)
        {
            // Reset state (a resize may have happened), but don't touch the backbuffer list. The downlevel Present hook
            // will take care of this
            d3d12.ResetState(false);
        }
    }

    return result;
}

void D3D12::ExecuteCommandLists(ID3D12CommandQueue* apCommandQueue, UINT aNumCommandLists, ID3D12CommandList* const* apcpCommandLists)
{
    auto& d3d12 = CET::Get().GetD3D12();
    if (d3d12.m_pCommandQueue == nullptr)
    {
        const auto desc = apCommandQueue->GetDesc();
        if (desc.Type == D3D12_COMMAND_LIST_TYPE_DIRECT)
        {
            auto ret = reinterpret_cast<uintptr_t>(_ReturnAddress()) - reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
            d3d12.m_pCommandQueue = apCommandQueue;
            Log::Info("D3D12::ExecuteCommandListsD3D12() - found valid command queue. {:X}", ret);
        }
        else
            Log::Info("D3D12::ExecuteCommandListsD3D12() - ignoring command queue - unusable command list type");
    }
    d3d12.m_realExecuteCommandLists(apCommandQueue, aNumCommandLists, apcpCommandLists);
}

void* ApplyHook(void** vtable, size_t index, void* target)
{
    DWORD oldProtect;
    VirtualProtect(vtable + index, 8, PAGE_EXECUTE_READWRITE, &oldProtect);
    const auto ret = vtable[index];
    vtable[index] = target;
    VirtualProtect(vtable + index, 8, oldProtect, nullptr);

    return ret;
}

void* D3D12::CRenderNode_Present_InternalPresent(int32_t* apDeviceIndex, uint8_t aSomeSync, UINT aSyncInterval)
{
    static std::once_flag s_kieroOnce;

    auto& d3d12 = CET::Get().GetD3D12();

    const auto* pContext = RenderContext::GetInstance();
    auto* pSwapChain = pContext->devices[*apDeviceIndex - 1].pSwapChain;
    if (d3d12.m_initialized)
        d3d12.Update();
    else
    {
        // NOTE: checking against Windows 8 as Windows 10 requires specific compatibility manifest to be detected by
        // these
        //       DX12 does not work on Windows 8 and 8.1 so we should be safe with this check
        if (IsWindows8OrGreater())
        {
            d3d12.m_pCommandQueue = pContext->pDirectQueue;
            d3d12.m_pdxgiSwapChain = pSwapChain;
            d3d12.Initialize();
        }
        else
        {
            std::call_once(
                s_kieroOnce,
                []
                {
                    if (kiero::init() != kiero::Status::Success)
                        Log::Error("Kiero failed!");
                    else
                    {
                        std::string_view d3d12type = kiero::isDownLevelDevice() ? "D3D12on7" : "D3D12";
                        Log::Info("Kiero initialized for {}", d3d12type);

                        CET::Get().GetD3D12().Hook();
                    }
                });
        }
    }

    if (d3d12.m_pHMD != nullptr)
    {
        const auto bufferIndex = (d3d12.m_pdxgiSwapChain != nullptr) ? (d3d12.m_pdxgiSwapChain->GetCurrentBackBufferIndex()) : (d3d12.m_downlevelBufferIndex);
        auto& frameContext = d3d12.m_frameContexts[bufferIndex];

        vr::VRTextureBounds_t bounds;
        bounds.uMin = 0.0f;
        bounds.uMax = 1.0f;
        bounds.vMin = 0.0f;
        bounds.vMax = 1.0f;

        if (!d3d12.m_vrInfo.m_isRightEye)
        {
            vr::VRCompositor()->WaitGetPoses(m_rTrackedDevicePose, vr::k_unMaxTrackedDeviceCount, NULL, 0);

            d3d12.UpdateHMDMatrixPose();

            vr::D3D12TextureData_t d3d12LeftEyeTexture = {frameContext.BackBuffer.Get(), d3d12.m_pCommandQueue.Get(), 0};
            vr::Texture_t leftEyeTexture = {(void*)&d3d12LeftEyeTexture, vr::TextureType_DirectX12, vr::ColorSpace_Auto};
            vr::EVRCompositorError err = vr::VRCompositor()->Submit(vr::Eye_Left, &leftEyeTexture, &bounds, vr::Submit_Default);

            if (err != vr::EVRCompositorError::VRCompositorError_None)
            {
                std::string s;
                s = std::format("Compositor error: {}", (int)err);
                Log::Error(s);
            }
        }
        else
        {
            vr::D3D12TextureData_t d3d12RightEyeTexture = {frameContext.BackBuffer.Get(), d3d12.m_pCommandQueue.Get(), 0};
            vr::Texture_t rightEyeTexture = {(void*)&d3d12RightEyeTexture, vr::TextureType_DirectX12, vr::ColorSpace_Auto};
            vr::VRCompositor()->Submit(vr::Eye_Right, &rightEyeTexture, &bounds, vr::Submit_Default);
        }

        d3d12.m_vrInfo.m_isRightEye = !d3d12.m_vrInfo.m_isRightEye;
        CET::Get().GetVM().SyncVr(d3d12.m_vrInfo);
    }

    return d3d12.m_realInternalPresent(apDeviceIndex, aSomeSync, aSyncInterval);
}

void* D3D12::CRenderGlobal_Resize(uint32_t aWidth, uint32_t aHeight, uint32_t a3, uint8_t a4, int* apDeviceIndex)
{
    auto& d3d12 = CET::Get().GetD3D12();

    // TODO - ideally find a way to not call this on each minimize/maximize/etc. which causes this to be called
    //        it can get called multiple times even when there was no resolution change or swapchain invalidation
    if (d3d12.m_initialized)
    {
        Log::Info("CRenderGlobal::Resize() called with initialized D3D12, triggering D3D12::ResetState.");
        d3d12.ResetState();
    }

    return d3d12.m_realInternalResize(aWidth, aHeight, a3, a4, apDeviceIndex);
}

// NOTE - this is called 32 times, as it seems to be called for each device object in RendererContext
void* D3D12::CRenderGlobal_Shutdown(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4)
{
    auto& d3d12 = CET::Get().GetD3D12();

    d3d12.ResetState(true, true);

    return d3d12.m_realInternalShutdown(a1, a2, a3, a4);
}

ID3D12Device* D3D12::GetDevice() const
{
    return m_pd3d12Device.Get();
}

std::tuple<D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_GPU_DESCRIPTOR_HANDLE> D3D12::CreateTextureDescriptor()
{
    const UINT handle_increment = m_pd3d12Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    static std::atomic descriptor_index = 1;

    const auto index = descriptor_index++;

    if (index >= 200)
        return {{}, {}};

    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = m_pd3dSrvDescHeap->GetCPUDescriptorHandleForHeapStart();
    cpuHandle.ptr += handle_increment * index;
    D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = m_pd3dSrvDescHeap->GetGPUDescriptorHandleForHeapStart();
    gpuHandle.ptr += handle_increment * index;

    return {cpuHandle, gpuHandle};
}

void D3D12::Hook()
{
    if (kiero::isDownLevelDevice())
    {
        int d3d12FailedHooksCount = 0;
        int d3d12CompleteHooksCount = 0;

        if (kiero::bind(175, reinterpret_cast<void**>(&m_realPresentD3D12Downlevel), reinterpret_cast<void*>(&PresentDownlevel)) != kiero::Status::Success)
        {
            Log::Error("D3D12on7: Downlevel Present hook failed!");
            ++d3d12FailedHooksCount;
        }
        else
        {
            Log::Info("D3D12on7: Downlevel Present hook complete.");
            ++d3d12CompleteHooksCount;
        }

        if (kiero::bind(27, reinterpret_cast<void**>(&m_realCreateCommittedResource), reinterpret_cast<void*>(&CreateCommittedResource)) != kiero::Status::Success)
        {
            Log::Error("D3D12on7: CreateCommittedResource Hook failed!");
            ++d3d12FailedHooksCount;
        }
        else
        {
            Log::Info("D3D12on7: CreateCommittedResource hook complete.");
            ++d3d12CompleteHooksCount;
        }

        if (kiero::bind(54, reinterpret_cast<void**>(&m_realExecuteCommandLists), reinterpret_cast<void*>(&ExecuteCommandLists)) != kiero::Status::Success)
        {
            Log::Error("D3D12on7: ExecuteCommandLists hook failed!");
            ++d3d12FailedHooksCount;
        }
        else
        {
            Log::Info("D3D12on7: ExecuteCommandLists hook complete.");
            ++d3d12CompleteHooksCount;
        }

        if (d3d12FailedHooksCount == 0)
            Log::Info("D3D12on7: hook complete. ({}/{})", d3d12CompleteHooksCount, d3d12CompleteHooksCount + d3d12FailedHooksCount);
        else
            Log::Error("D3D12on7: hook failed! ({}/{})", d3d12CompleteHooksCount, d3d12CompleteHooksCount + d3d12FailedHooksCount);
    }
    else
        Log::Info("Skipping internal d3d12 hook, using game method");
}

void D3D12::HookGame()
{
    const RED4ext::RelocPtr<void> presentInternal(CyberEngineTweaks::Addresses::CRenderNode_Present_DoInternal);
    const RED4ext::RelocPtr<void> resizeInternal(CyberEngineTweaks::Addresses::CRenderGlobal_Resize);
    const RED4ext::RelocPtr<void> shutdownInternal(CyberEngineTweaks::Addresses::CRenderGlobal_Shutdown);

    if (MH_CreateHook(presentInternal.GetAddr(), reinterpret_cast<void*>(&CRenderNode_Present_InternalPresent), reinterpret_cast<void**>(&m_realInternalPresent)) != MH_OK ||
        MH_EnableHook(presentInternal.GetAddr()) != MH_OK)
        Log::Error("Could not hook CRenderNode_Present_InternalPresent function!");
    else
        Log::Info("CRenderNode_Present_InternalPresent function hook complete!");

    if (MH_CreateHook(resizeInternal.GetAddr(), reinterpret_cast<void*>(&CRenderGlobal_Resize), reinterpret_cast<void**>(&m_realInternalResize)) != MH_OK ||
        MH_EnableHook(resizeInternal.GetAddr()) != MH_OK)
        Log::Error("Could not hook CRenderGlobal_Resize function!");
    else
        Log::Info("CRenderGlobal_Resize function hook complete!");

    if (MH_CreateHook(shutdownInternal.GetAddr(), reinterpret_cast<void*>(&CRenderGlobal_Shutdown), reinterpret_cast<void**>(&m_realInternalShutdown)) != MH_OK ||
        MH_EnableHook(shutdownInternal.GetAddr()) != MH_OK)
        Log::Error("Could not hook CRenderGlobal_Shutdown function!");
    else
        Log::Info("CRenderGlobal_Shutdown function hook complete!");
}

void D3D12::InitVr()
{
    // Loading the SteamVR Runtime
    Log::Info("Initializing VR");
    vr::EVRInitError eError = vr::VRInitError_None;
    m_pHMD = vr::VR_Init(&eError, vr::VRApplication_Scene);

    if (eError != vr::VRInitError_None)
    {
        m_pHMD = nullptr;
        Log::Error("Unable to init VR runtime: {}", vr::VR_GetVRInitErrorAsEnglishDescription(eError));
    }

    m_pRenderModels = (vr::IVRRenderModels*)vr::VR_GetGenericInterface(vr::IVRRenderModels_Version, &eError);
    if (!m_pRenderModels)
    {
        m_pHMD = nullptr;
        vr::VR_Shutdown();

        Log::Error("Unable to get render model interface: {}",
                  vr::VR_GetVRInitErrorAsEnglishDescription(eError));
    }

    // init compositor
    vr::EVRInitError peError = vr::VRInitError_None;

	if ( !vr::VRCompositor() )
	{
		Log::Error( "Compositor initialization failed. See log file for details\n" );
	}

}

void D3D12::ShutdownVr()
{
    Log::Info("Shutting down VR");
    vr::VR_Shutdown();
}

Matrix4x4 ConvertSteamVRMatrixToMatrix4( const vr::HmdMatrix34_t &matPose )
{
	Matrix4x4 matrixObj(
		matPose.m[0][0], matPose.m[1][0], matPose.m[2][0], 0.0,
		matPose.m[0][1], matPose.m[1][1], matPose.m[2][1], 0.0,
		matPose.m[0][2], matPose.m[1][2], matPose.m[2][2], 0.0,
		matPose.m[0][3], matPose.m[1][3], matPose.m[2][3], 1.0f
		);

	return matrixObj;
}

Quaternion Matrix4ToQuat(Matrix4x4 mat)
{
    Matrix4x4 te = mat;

    float m11 = te[0], m12 = te[1], m13 = te[2], m21 = te[4], m22 = te[5], m23 = te[6], m31 = te[8], m32 = te[9],
          m33 = te[10];

    float trace = m11 + m22 + m33;

    Quaternion quat;

    if (trace > 0)
    {
        float s = 0.5f / sqrtf(trace + 1.0f);

        quat.r = 0.25f / s;
        quat.i = (m32 - m23) * s;
        quat.j = (m13 - m31) * s;
        quat.k = (m21 - m12) * s;
    }
    else if (m11 > m22 && m11 > m33)
    {
        float s = 2.0f * sqrtf(1.0f + m11 - m22 - m33);

        quat.r = (m32 - m23) / s;
        quat.i = 0.25f * s;
        quat.j = (m12 + m21) / s;
        quat.k = (m13 + m31) / s;
    }
    else if (m22 > m33)
    {
        float s = 2.0f * sqrtf(1.0f + m22 - m11 - m33);

        quat.r = (m13 - m31) / s;
        quat.i = (m12 + m21) / s;
        quat.j = 0.25f * s;
        quat.k = (m23 + m32) / s;
    }
    else
    {
        float s = 2.0f * sqrtf(1.0f + m33 - m11 - m22);

        quat.r = (m21 - m12) / s;
        quat.i = (m13 + m31) / s;
        quat.j = (m23 + m32) / s;
        quat.k = 0.25f * s;
    }

    return quat;
}

void D3D12::UpdateHMDMatrixPose()
{
    if (m_pHMD == nullptr)
    {
        return;
    }

    for (int nDevice = 0; nDevice < vr::k_unMaxTrackedDeviceCount; ++nDevice)
    {
        if (m_rTrackedDevicePose[nDevice].bPoseIsValid)
        {
            m_rmat4DevicePose[nDevice] =
                ConvertSteamVRMatrixToMatrix4(m_rTrackedDevicePose[nDevice].mDeviceToAbsoluteTracking);
            switch (m_pHMD->GetTrackedDeviceClass(nDevice))
            {
            case vr::TrackedDeviceClass_Controller: break;
            case vr::TrackedDeviceClass_HMD:
            {
                vr::ETrackedPropertyError e;
                float ipd = m_pHMD->GetFloatTrackedDeviceProperty(nDevice, vr::Prop_UserIpdMeters_Float, &e);
                if (e == vr::ETrackedPropertyError::TrackedProp_Success)
                {
                    m_vrInfo.m_ipd = ipd;
                }

                float fov = m_pHMD->GetFloatTrackedDeviceProperty(nDevice, vr::Prop_FieldOfViewLeftDegrees_Float, &e);
                if (e == vr::ETrackedPropertyError::TrackedProp_Success)
                {
                    m_vrInfo.m_fov = fov;
                }
                break;
            }
            case vr::TrackedDeviceClass_Invalid: break;
            case vr::TrackedDeviceClass_GenericTracker: break;
            case vr::TrackedDeviceClass_TrackingReference: break;
            default: break;
            }
        }
    }

    if (m_rTrackedDevicePose[vr::k_unTrackedDeviceIndex_Hmd].bPoseIsValid)
    {
        m_hmdDevicePose = m_rmat4DevicePose[vr::k_unTrackedDeviceIndex_Hmd];
        m_hmdDevicePose.Invert();

        m_vrInfo.m_position = Vector3(m_hmdDevicePose[12], m_hmdDevicePose[13], m_hmdDevicePose[14]);
        m_vrInfo.m_rotation = Matrix4ToQuat(m_hmdDevicePose);
    }
}
