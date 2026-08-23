/**
 * JamesDSP for Windows
 * 
 * Full-featured audio DSP processor using WASAPI loopback.
 * Replicates Linux JamesDSP functionality.
 * 
 * Usage:
 *   JamesDSPConsole.exe [-i <capture_device_index>] [-o <device_index>] [-c <config_file>]
 * 
 * Keys:
 *   R - Reload configuration from file
 *   S - Show current DSP status
 *   Q - Quit (or Ctrl+C)
 */

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <avrt.h>
#include <functiondiscoverykeys_devpkey.h>
#include <iostream>
#include <string>
#include <atomic>
#include <thread>
#include <conio.h>
#include <sstream>
#include <vector>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <mmreg.h>
#include <ksmedia.h>

#include "DspConfig.h"

struct DeviceShareMode;

class DECLSPEC_UUID("870af99c-171d-4f9e-af0d-e63df40c2bc9") CPolicyConfigClient;

MIDL_INTERFACE("f8679f50-850a-41cf-9c72-430f290290c8")
IPolicyConfig : public IUnknown {
public:
    virtual HRESULT GetMixFormat(PCWSTR, WAVEFORMATEX**) = 0;
    virtual HRESULT GetDeviceFormat(PCWSTR, INT, WAVEFORMATEX**) = 0;
    virtual HRESULT ResetDeviceFormat(PCWSTR) = 0;
    virtual HRESULT SetDeviceFormat(PCWSTR, WAVEFORMATEX*, WAVEFORMATEX*) = 0;
    virtual HRESULT GetProcessingPeriod(PCWSTR, INT, PINT64, PINT64) = 0;
    virtual HRESULT SetProcessingPeriod(PCWSTR, PINT64) = 0;
    virtual HRESULT GetShareMode(PCWSTR, DeviceShareMode*) = 0;
    virtual HRESULT SetShareMode(PCWSTR, DeviceShareMode*) = 0;
    virtual HRESULT GetPropertyValue(PCWSTR, const PROPERTYKEY&, PROPVARIANT*) = 0;
    virtual HRESULT SetPropertyValue(PCWSTR, const PROPERTYKEY&, PROPVARIANT*) = 0;
    virtual HRESULT SetDefaultEndpoint(PCWSTR, ERole) = 0;
    virtual HRESULT SetEndpointVisibility(PCWSTR, INT) = 0;
};

// Reference time units
#define REFTIMES_PER_SEC  10000000
#define REFTIMES_PER_MILLISEC  10000

// Global state
static std::atomic<bool> g_running{true};
static std::atomic<bool> g_reloadConfig{false};
static std::atomic<bool> g_showStatus{false};
static JamesDSPLib* g_dsp = nullptr;
static DspController* g_controller = nullptr;
static DspConfig g_config;
static std::string g_configFile;
static bool g_watchConfig = false;
static int g_bufferMs = 100;

static void PrintAudioHealth(
    uint64_t processedFrames,
    uint64_t droppedFrames,
    uint64_t silentFrames,
    uint64_t packets,
    uint64_t conversionErrors,
    uint64_t captureDiscontinuities,
    uint64_t renderStarvations,
    uint64_t renderErrors,
    uint64_t dspCalls,
    uint64_t dspTotalMicros,
    uint64_t dspMaxMicros,
    uint64_t dspDeadlineMisses,
    uint64_t dspCriticalStalls,
    UINT32 minRenderPadding,
    UINT32 maxRenderPadding,
    UINT32 renderBufferFrames) {
    uint64_t dspAvgMicros = dspCalls > 0 ? dspTotalMicros / dspCalls : 0;
    UINT32 reportedMinPadding = minRenderPadding == UINT32_MAX ? 0 : minRenderPadding;
    std::cout << "[AUDIO] frames=" << processedFrames
              << " dropped=" << droppedFrames
              << " silent=" << silentFrames
              << " packets=" << packets
              << " conversionErrors=" << conversionErrors
              << " discontinuities=" << captureDiscontinuities
              << " renderStarvations=" << renderStarvations
              << " renderErrors=" << renderErrors
              << " dspAvgUs=" << dspAvgMicros
              << " dspMaxUs=" << dspMaxMicros
              << " dspCalls=" << dspCalls
              << " dspDeadlineMisses=" << dspDeadlineMisses
              << " dspCriticalStalls=" << dspCriticalStalls
              << " paddingMin=" << reportedMinPadding
              << " paddingMax=" << maxRenderPadding
              << " bufferFrames=" << renderBufferFrames
              << std::endl;
}

static bool IsFloatFormat(const WAVEFORMATEX* format) {
    if (!format) return false;
    if (format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) return true;
    if (format->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
        auto ext = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(format);
        return ext->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
    }
    return false;
}

static std::string FormatDescription(const WAVEFORMATEX* format) {
    if (!format) return "unknown";
    std::ostringstream out;
    out << format->nSamplesPerSec << " Hz, "
        << format->nChannels << " ch, "
        << format->wBitsPerSample << " bits, tag 0x"
        << std::hex << format->wFormatTag << std::dec
        << (IsFloatFormat(format) ? " float" : " pcm/int");
    if (format->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
        auto ext = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(format);
        out << ", valid bits " << ext->Samples.wValidBitsPerSample
            << ", mask 0x" << std::hex << ext->dwChannelMask << std::dec;
    }
    return out.str();
}

static float ClampFloatSample(float value) {
    if (value > 1.0f) return 1.0f;
    if (value < -1.0f) return -1.0f;
    return value;
}

static int32_t ReadInt24(const uint8_t* ptr) {
    int32_t value = (static_cast<int32_t>(ptr[2]) << 16)
        | (static_cast<int32_t>(ptr[1]) << 8)
        | static_cast<int32_t>(ptr[0]);
    if (value & 0x800000) value |= ~0xFFFFFF;
    return value;
}

static void WriteInt24(uint8_t* ptr, int32_t value) {
    value = std::clamp(value, -8388608, 8388607);
    ptr[0] = static_cast<uint8_t>(value & 0xFF);
    ptr[1] = static_cast<uint8_t>((value >> 8) & 0xFF);
    ptr[2] = static_cast<uint8_t>((value >> 16) & 0xFF);
}

static bool ConvertToFloat(const BYTE* input, UINT32 frames, const WAVEFORMATEX* format, std::vector<float>& output) {
    if (!input || !format) return false;
    const UINT32 channels = format->nChannels;
    const size_t sampleCount = static_cast<size_t>(frames) * channels;
    output.assign(sampleCount, 0.0f);

    if (IsFloatFormat(format) && format->wBitsPerSample == 32) {
        memcpy(output.data(), input, sampleCount * sizeof(float));
        return true;
    }

    if (format->wBitsPerSample == 16) {
        auto samples = reinterpret_cast<const int16_t*>(input);
        for (size_t i = 0; i < sampleCount; ++i) output[i] = samples[i] / 32768.0f;
        return true;
    }

    if (format->wBitsPerSample == 24) {
        auto ptr = reinterpret_cast<const uint8_t*>(input);
        for (size_t i = 0; i < sampleCount; ++i) {
            output[i] = ReadInt24(ptr) / 8388608.0f;
            ptr += 3;
        }
        return true;
    }

    if (format->wBitsPerSample == 32) {
        auto samples = reinterpret_cast<const int32_t*>(input);
        for (size_t i = 0; i < sampleCount; ++i) output[i] = samples[i] / 2147483648.0f;
        return true;
    }

    return false;
}

static void AdaptChannels(const std::vector<float>& input, UINT32 frames, UINT32 inputChannels, UINT32 outputChannels, std::vector<float>& output) {
    output.assign(static_cast<size_t>(frames) * outputChannels, 0.0f);
    if (inputChannels == 0 || outputChannels == 0) return;

    const UINT32 shared = inputChannels < outputChannels ? inputChannels : outputChannels;
    for (UINT32 frame = 0; frame < frames; ++frame) {
        const size_t inBase = static_cast<size_t>(frame) * inputChannels;
        const size_t outBase = static_cast<size_t>(frame) * outputChannels;
        for (UINT32 ch = 0; ch < shared; ++ch) output[outBase + ch] = input[inBase + ch];
        if (inputChannels == 1 && outputChannels >= 2) output[outBase + 1] = input[inBase];
    }
}

static void ResampleInterleaved(
    const std::vector<float>& input,
    UINT32 inputFrames,
    UINT32 outputFrames,
    UINT32 channels,
    std::vector<float>& output) {
    output.assign(static_cast<size_t>(outputFrames) * channels, 0.0f);
    if (inputFrames == 0 || outputFrames == 0 || channels == 0) return;
    if (inputFrames == outputFrames) {
        output = input;
        return;
    }
    const double scale = outputFrames > 1
        ? static_cast<double>(inputFrames - 1) / static_cast<double>(outputFrames - 1)
        : 0.0;
    for (UINT32 outFrame = 0; outFrame < outputFrames; ++outFrame) {
        const double position = outFrame * scale;
        const UINT32 first = static_cast<UINT32>(position);
        const UINT32 second = min(first + 1, inputFrames - 1);
        const float fraction = static_cast<float>(position - first);
        for (UINT32 channel = 0; channel < channels; ++channel) {
            const float a = input[static_cast<size_t>(first) * channels + channel];
            const float b = input[static_cast<size_t>(second) * channels + channel];
            output[static_cast<size_t>(outFrame) * channels + channel] = a + (b - a) * fraction;
        }
    }
}

static bool ConvertFromFloat(const std::vector<float>& input, UINT32 frames, const WAVEFORMATEX* format, BYTE* output) {
    if (!format || !output) return false;
    const UINT32 channels = format->nChannels;
    const size_t sampleCount = static_cast<size_t>(frames) * channels;

    if (IsFloatFormat(format) && format->wBitsPerSample == 32) {
        memcpy(output, input.data(), sampleCount * sizeof(float));
        return true;
    }

    if (format->wBitsPerSample == 16) {
        auto samples = reinterpret_cast<int16_t*>(output);
        for (size_t i = 0; i < sampleCount; ++i) {
            samples[i] = static_cast<int16_t>(std::lrintf(ClampFloatSample(input[i]) * 32767.0f));
        }
        return true;
    }

    if (format->wBitsPerSample == 24) {
        auto ptr = reinterpret_cast<uint8_t*>(output);
        for (size_t i = 0; i < sampleCount; ++i) {
            WriteInt24(ptr, static_cast<int32_t>(std::lrintf(ClampFloatSample(input[i]) * 8388607.0f)));
            ptr += 3;
        }
        return true;
    }

    if (format->wBitsPerSample == 32) {
        auto samples = reinterpret_cast<int32_t*>(output);
        for (size_t i = 0; i < sampleCount; ++i) {
            samples[i] = static_cast<int32_t>(std::llrintf(ClampFloatSample(input[i]) * 2147483647.0f));
        }
        return true;
    }

    return false;
}

// Console handler for Ctrl+C
BOOL WINAPI ConsoleHandler(DWORD signal) {
    if (signal == CTRL_C_EVENT || signal == CTRL_BREAK_EVENT) {
        std::cout << "\n[INFO] Stopping..." << std::endl;
        g_running = false;
        return TRUE;
    }
    return FALSE;
}

// Keyboard input thread
void keyboardThread() {
    while (g_running) {
        if (_kbhit()) {
            int ch = _getch();
            if (ch == 'r' || ch == 'R') {
                g_reloadConfig = true;
            } else if (ch == 's' || ch == 'S') {
                g_showStatus = true;
            } else if (ch == 'q' || ch == 'Q') {
                g_running = false;
            }
        }
        Sleep(50);
    }
}

// Initialize JamesDSP library
bool initDsp(int sampleRate, int blockSize) {
    g_dsp = new JamesDSPLib();
    if (!g_dsp) {
        std::cerr << "[ERROR] Failed to allocate JamesDSPLib" << std::endl;
        return false;
    }
    
    memset(g_dsp, 0, sizeof(JamesDSPLib));
    JamesDSPGlobalMemoryAllocation();
    JamesDSPInit(g_dsp, blockSize, static_cast<float>(sampleRate));
    
    g_controller = new DspController(g_dsp, sampleRate, blockSize);
    
    std::cout << "[INFO] JamesDSP initialized @ " << sampleRate << " Hz, block size: " << blockSize << std::endl;
    return true;
}

// Cleanup DSP
void cleanupDsp() {
    if (g_controller) {
        delete g_controller;
        g_controller = nullptr;
    }
    if (g_dsp) {
        JamesDSPFree(g_dsp);
        JamesDSPGlobalMemoryDeallocation();
        delete g_dsp;
        g_dsp = nullptr;
    }
}

// Get default audio endpoint device
IMMDevice* GetDefaultDevice(EDataFlow flow, ERole role) {
    IMMDeviceEnumerator* enumerator = nullptr;
    IMMDevice* device = nullptr;
    
    HRESULT hr = CoCreateInstance(
        __uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
        __uuidof(IMMDeviceEnumerator), (void**)&enumerator
    );
    
    if (FAILED(hr)) {
        std::cerr << "[ERROR] Failed to create device enumerator" << std::endl;
        return nullptr;
    }
    
    hr = enumerator->GetDefaultAudioEndpoint(flow, role, &device);
    enumerator->Release();
    
    if (FAILED(hr)) {
        std::cerr << "[ERROR] Failed to get default audio endpoint" << std::endl;
        return nullptr;
    }
    
    return device;
}

IMMDevice* GetDeviceByIndex(EDataFlow flow, int index) {
    if (index < 0) return nullptr;

    IMMDeviceEnumerator* enumerator = nullptr;
    IMMDeviceCollection* collection = nullptr;
    IMMDevice* device = nullptr;

    HRESULT hr = CoCreateInstance(
        __uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
        __uuidof(IMMDeviceEnumerator), (void**)&enumerator
    );

    if (FAILED(hr) || !enumerator) return nullptr;

    hr = enumerator->EnumAudioEndpoints(flow, DEVICE_STATE_ACTIVE, &collection);
    if (SUCCEEDED(hr) && collection) {
        UINT count = 0;
        collection->GetCount(&count);
        if ((UINT)index < count) collection->Item(index, &device);
        collection->Release();
    }

    enumerator->Release();
    return device;
}

// List audio devices
void ListDevices(EDataFlow flow) {
    IMMDeviceEnumerator* enumerator = nullptr;
    IMMDeviceCollection* collection = nullptr;
    
    CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
        __uuidof(IMMDeviceEnumerator), (void**)&enumerator);
    
    if (!enumerator) return;
    
    enumerator->EnumAudioEndpoints(flow, DEVICE_STATE_ACTIVE, &collection);
    
    if (!collection) {
        enumerator->Release();
        return;
    }
    
    UINT count;
    collection->GetCount(&count);
    
    std::wcout << L"\n" << (flow == eRender ? L"OUTPUT" : L"INPUT") << L" DEVICES:" << std::endl;
    std::wcout << L"----------------------------------------" << std::endl;
    
    for (UINT i = 0; i < count; i++) {
        IMMDevice* device = nullptr;
        collection->Item(i, &device);
        
        if (device) {
            IPropertyStore* props = nullptr;
            device->OpenPropertyStore(STGM_READ, &props);
            
            if (props) {
                PROPVARIANT name;
                PropVariantInit(&name);
                props->GetValue(PKEY_Device_FriendlyName, &name);
                
                std::wcout << L"[" << i << L"] " << name.pwszVal << std::endl;
                
                PropVariantClear(&name);
                props->Release();
            }
            
            device->Release();
        }
    }
    
    collection->Release();
    enumerator->Release();
}

std::string JsonEscape(const std::wstring& value) {
    if (value.empty()) return "";
    int size = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (size <= 0) return "";
    std::string utf8(size - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, utf8.data(), size, nullptr, nullptr);
    std::ostringstream out;
    for (char ch : utf8) {
        switch (ch) {
            case '\\': out << "\\\\"; break;
            case '"': out << "\\\""; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default:
                if (static_cast<unsigned char>(ch) < 0x20) out << " ";
                else out << ch;
        }
    }
    return out.str();
}

int ListDevicesJson(EDataFlow flow) {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    bool comInitialized = SUCCEEDED(hr);
    IMMDeviceEnumerator* enumerator = nullptr;
    IMMDeviceCollection* collection = nullptr;

    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
        __uuidof(IMMDeviceEnumerator), (void**)&enumerator);
    if (FAILED(hr) || !enumerator) {
        std::cout << "{\"devices\":[]}" << std::endl;
        if (comInitialized) CoUninitialize();
        return 1;
    }

    enumerator->EnumAudioEndpoints(flow, DEVICE_STATE_ACTIVE, &collection);
    std::cout << "{\"devices\":[";
    if (collection) {
        UINT count = 0;
        collection->GetCount(&count);
        for (UINT i = 0; i < count; i++) {
            IMMDevice* device = nullptr;
            collection->Item(i, &device);
            if (!device) continue;
            IPropertyStore* props = nullptr;
            LPWSTR id = nullptr;
            std::wstring nameValue;
            std::wstring idValue;
            device->GetId(&id);
            if (id) {
                idValue = id;
                CoTaskMemFree(id);
            }
            if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, &props)) && props) {
                PROPVARIANT name;
                PropVariantInit(&name);
                props->GetValue(PKEY_Device_FriendlyName, &name);
                if (name.vt == VT_LPWSTR && name.pwszVal) nameValue = name.pwszVal;
                PropVariantClear(&name);
                props->Release();
            }
            if (i > 0) std::cout << ",";
            std::cout << "{\"index\":" << i
                      << ",\"name\":\"" << JsonEscape(nameValue)
                      << "\",\"id\":\"" << JsonEscape(idValue) << "\"}";
            device->Release();
        }
        collection->Release();
    }
    std::cout << "]}" << std::endl;
    enumerator->Release();
    if (comInitialized) CoUninitialize();
    return 0;
}

int GetDefaultDeviceJson(EDataFlow flow) {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    bool comInitialized = SUCCEEDED(hr);
    IMMDeviceEnumerator* enumerator = nullptr;
    IMMDeviceCollection* collection = nullptr;
    IMMDevice* defaultDevice = nullptr;

    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
        __uuidof(IMMDeviceEnumerator), (void**)&enumerator);
    if (FAILED(hr) || !enumerator) {
        std::cout << "{\"index\":-1,\"name\":\"\",\"id\":\"\"}" << std::endl;
        if (comInitialized) CoUninitialize();
        return 1;
    }

    hr = enumerator->GetDefaultAudioEndpoint(flow, eMultimedia, &defaultDevice);
    if (FAILED(hr) || !defaultDevice) {
        std::cout << "{\"index\":-1,\"name\":\"\",\"id\":\"\"}" << std::endl;
        enumerator->Release();
        if (comInitialized) CoUninitialize();
        return 1;
    }

    LPWSTR defaultId = nullptr;
    std::wstring idValue;
    std::wstring nameValue;
    int defaultIndex = -1;
    defaultDevice->GetId(&defaultId);
    if (defaultId) idValue = defaultId;

    IPropertyStore* props = nullptr;
    if (SUCCEEDED(defaultDevice->OpenPropertyStore(STGM_READ, &props)) && props) {
        PROPVARIANT name;
        PropVariantInit(&name);
        props->GetValue(PKEY_Device_FriendlyName, &name);
        if (name.vt == VT_LPWSTR && name.pwszVal) nameValue = name.pwszVal;
        PropVariantClear(&name);
        props->Release();
    }

    if (SUCCEEDED(enumerator->EnumAudioEndpoints(flow, DEVICE_STATE_ACTIVE, &collection)) && collection) {
        UINT count = 0;
        collection->GetCount(&count);
        for (UINT i = 0; i < count; i++) {
            IMMDevice* device = nullptr;
            LPWSTR id = nullptr;
            collection->Item(i, &device);
            if (device) {
                device->GetId(&id);
                if (id && defaultId && wcscmp(id, defaultId) == 0) defaultIndex = static_cast<int>(i);
                if (id) CoTaskMemFree(id);
                device->Release();
            }
            if (defaultIndex >= 0) break;
        }
        collection->Release();
    }

    std::cout << "{\"index\":" << defaultIndex
              << ",\"name\":\"" << JsonEscape(nameValue)
              << "\",\"id\":\"" << JsonEscape(idValue) << "\"}" << std::endl;

    if (defaultId) CoTaskMemFree(defaultId);
    defaultDevice->Release();
    enumerator->Release();
    if (comInitialized) CoUninitialize();
    return 0;
}

int SetDefaultRenderDevice(int index) {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    bool comInitialized = SUCCEEDED(hr);

    IMMDevice* device = GetDeviceByIndex(eRender, index);
    if (!device) {
        std::cerr << "[ERROR] No render device at index " << index << std::endl;
        if (comInitialized) CoUninitialize();
        return 1;
    }

    LPWSTR id = nullptr;
    hr = device->GetId(&id);
    if (FAILED(hr) || !id) {
        std::cerr << "[ERROR] Failed to read render device ID" << std::endl;
        device->Release();
        if (comInitialized) CoUninitialize();
        return 1;
    }

    IPolicyConfig* policy = nullptr;
    hr = CoCreateInstance(__uuidof(CPolicyConfigClient), nullptr, CLSCTX_ALL,
        __uuidof(IPolicyConfig), (void**)&policy);
    if (FAILED(hr) || !policy) {
        std::cerr << "[ERROR] Failed to create policy config: 0x" << std::hex << hr << std::endl;
        CoTaskMemFree(id);
        device->Release();
        if (comInitialized) CoUninitialize();
        return 1;
    }

    HRESULT consoleHr = policy->SetDefaultEndpoint(id, eConsole);
    HRESULT multimediaHr = policy->SetDefaultEndpoint(id, eMultimedia);
    HRESULT communicationsHr = policy->SetDefaultEndpoint(id, eCommunications);

    IPropertyStore* props = nullptr;
    std::wstring nameValue;
    if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, &props)) && props) {
        PROPVARIANT name;
        PropVariantInit(&name);
        props->GetValue(PKEY_Device_FriendlyName, &name);
        if (name.vt == VT_LPWSTR && name.pwszVal) nameValue = name.pwszVal;
        PropVariantClear(&name);
        props->Release();
    }

    policy->Release();
    CoTaskMemFree(id);
    device->Release();
    if (comInitialized) CoUninitialize();

    if (FAILED(consoleHr) || FAILED(multimediaHr) || FAILED(communicationsHr)) {
        std::cerr << "[ERROR] Failed to set one or more default endpoint roles" << std::endl;
        return 1;
    }

    std::wcout << L"[INFO] Windows default playback set to [" << index << L"] " << nameValue << std::endl;
    return 0;
}

ULONGLONG GetFileWriteStamp(const std::string& path) {
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (!GetFileAttributesExA(path.c_str(), GetFileExInfoStandard, &data)) return 0;
    ULARGE_INTEGER value;
    value.LowPart = data.ftLastWriteTime.dwLowDateTime;
    value.HighPart = data.ftLastWriteTime.dwHighDateTime;
    return value.QuadPart;
}

// Print usage
void PrintUsage() {
    std::cout << "\n========================================" << std::endl;
    std::cout << "   JamesDSP for Windows" << std::endl;
    std::cout << "========================================\n" << std::endl;
    std::cout << "Usage: JamesDSPConsole.exe [-i <capture_device>] [-o <device>] [-c <config>]\n" << std::endl;
    std::cout << "Setup:" << std::endl;
    std::cout << "  1. Route the player/browser to the capture/source endpoint" << std::endl;
    std::cout << "  2. Run this program with -o to select the real output" << std::endl;
    std::cout << "\nKeys:" << std::endl;
    std::cout << "  R - Reload config.ini" << std::endl;
    std::cout << "  S - Show DSP status" << std::endl;
    std::cout << "  Q - Quit" << std::endl;
    std::cout << "\nOptions:" << std::endl;
    std::cout << "  -i, --input <idx>    Capture/source render endpoint index" << std::endl;
    std::cout << "  --watch-config       Reload config automatically when it changes" << std::endl;
    std::cout << "  --buffer-ms <ms>     Shared WASAPI buffer target, 20-200 ms" << std::endl;
    std::cout << "  --list-devices-json  Print active render devices as JSON and exit" << std::endl;
    std::cout << "  --set-default <idx>  Set Windows default playback endpoint and exit" << std::endl;
}

// Main audio processing loop
void AudioProcessingLoop(IMMDevice* captureDevice, IMMDevice* renderDevice) {
    IAudioClient* captureClient = nullptr;
    IAudioClient* renderClient = nullptr;
    IAudioCaptureClient* captureService = nullptr;
    IAudioRenderClient* renderService = nullptr;
    WAVEFORMATEX* captureFormat = nullptr;
    WAVEFORMATEX* renderFormat = nullptr;
    HANDLE captureEvent = nullptr;
    HANDLE mmcssHandle = nullptr;
    DWORD mmcssTaskIndex = 0;
    std::thread* kbThread = nullptr;
    std::vector<float> captureFloat;
    std::vector<float> dspFloat;
    std::vector<float> resampledDspFloat;
    std::vector<float> renderFloat;
    
    HRESULT hr;
    int bufferMs = g_bufferMs;
    if (bufferMs < 20) bufferMs = 20;
    if (bufferMs > 200) bufferMs = 200;
    REFERENCE_TIME bufferDuration = static_cast<REFERENCE_TIME>(bufferMs) * REFTIMES_PER_MILLISEC;

    if (SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS)) {
        std::cout << "[INFO] Process priority enabled: High" << std::endl;
    } else {
        std::cerr << "[WARN] Unable to enable High process priority: " << GetLastError() << std::endl;
    }

    PROCESS_POWER_THROTTLING_STATE powerThrottling{};
    powerThrottling.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
    powerThrottling.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
    powerThrottling.StateMask = 0;
    if (!SetProcessInformation(
            GetCurrentProcess(),
            ProcessPowerThrottling,
            &powerThrottling,
            sizeof(powerThrottling))) {
        std::cerr << "[WARN] Unable to disable process execution throttling: "
                  << GetLastError() << std::endl;
    }

    mmcssHandle = AvSetMmThreadCharacteristicsW(L"Pro Audio", &mmcssTaskIndex);
    if (mmcssHandle) {
        AvSetMmThreadPriority(mmcssHandle, AVRT_PRIORITY_CRITICAL);
        std::cout << "[INFO] MMCSS audio scheduling enabled: Pro Audio / Critical" << std::endl;
    } else {
        std::cerr << "[WARN] Unable to enable MMCSS audio scheduling: " << GetLastError() << std::endl;
    }
    
    // Create event for capture notifications
    captureEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (!captureEvent) {
        std::cerr << "[ERROR] Failed to create capture event" << std::endl;
        goto cleanup;
    }
    
    // Initialize capture
    hr = captureDevice->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&captureClient);
    if (FAILED(hr)) {
        std::cerr << "[ERROR] Failed to activate capture device" << std::endl;
        goto cleanup;
    }
    
    hr = captureClient->GetMixFormat(&captureFormat);
    if (FAILED(hr)) {
        std::cerr << "[ERROR] Failed to get capture format" << std::endl;
        goto cleanup;
    }
    
    std::cout << "[INFO] Capture format: " << FormatDescription(captureFormat) << std::endl;
    std::cout << "[INFO] Buffer target: " << bufferMs << " ms" << std::endl;
    
    {
        hr = captureClient->Initialize(
            AUDCLNT_SHAREMODE_SHARED,
            AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
            bufferDuration, 0, captureFormat, nullptr
        );
        
        if (FAILED(hr)) {
            std::cerr << "[ERROR] Capture init failed: 0x" << std::hex << hr << std::endl;
            goto cleanup;
        }
        
        captureClient->SetEventHandle(captureEvent);
    }
    
    hr = captureClient->GetService(__uuidof(IAudioCaptureClient), (void**)&captureService);
    if (FAILED(hr)) goto cleanup;
    
    // Initialize render
    hr = renderDevice->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&renderClient);
    if (FAILED(hr)) goto cleanup;
    
    hr = renderClient->GetMixFormat(&renderFormat);
    if (FAILED(hr)) goto cleanup;
    
    std::cout << "[INFO] Render format: " << FormatDescription(renderFormat) << std::endl;
    
    {
        hr = renderClient->Initialize(AUDCLNT_SHAREMODE_SHARED, 0,
            bufferDuration, 0, renderFormat, nullptr);
        if (FAILED(hr)) goto cleanup;
    }
    
    hr = renderClient->GetService(__uuidof(IAudioRenderClient), (void**)&renderService);
    if (FAILED(hr)) goto cleanup;
    
    // Initialize DSP
    {
        int dspBlockSize = 960;
        if (!initDsp(captureFormat->nSamplesPerSec, dspBlockSize)) goto cleanup;
        
        // Load and apply config
        ConfigFile::load(g_configFile, g_config);
        g_controller->applyConfig(g_config, true);
        g_controller->printStatus();
    }
    
    // Pre-fill render buffer
    {
        UINT32 bufferFrames;
        renderClient->GetBufferSize(&bufferFrames);
        const UINT32 prefillFrames = std::max<UINT32>(1, bufferFrames / 5);
        BYTE* data;
        renderService->GetBuffer(prefillFrames, &data);
        if (data) {
            memset(data, 0, prefillFrames * renderFormat->nBlockAlign);
            renderService->ReleaseBuffer(prefillFrames, AUDCLNT_BUFFERFLAGS_SILENT);
        }
    }
    
    // Start keyboard thread
    kbThread = new std::thread(keyboardThread);
    
    // Start streams
    captureClient->Start();
    renderClient->Start();
    
    std::cout << "\n[INFO] Processing started. Press R=reload, S=status, Q=quit\n" << std::endl;
    
    ULONGLONG configStamp = GetFileWriteStamp(g_configFile);
    uint64_t processedFrames = 0;
    uint64_t droppedFrames = 0;
    uint64_t silentFrames = 0;
    uint64_t packets = 0;
    uint64_t conversionErrors = 0;
    uint64_t captureDiscontinuities = 0;
    uint64_t renderStarvations = 0;
    uint64_t renderErrors = 0;
    uint64_t dspCalls = 0;
    uint64_t dspTotalMicros = 0;
    uint64_t dspMaxMicros = 0;
    uint64_t dspDeadlineMisses = 0;
    uint64_t dspCriticalStalls = 0;
    UINT32 minRenderPadding = UINT32_MAX;
    UINT32 maxRenderPadding = 0;
    UINT32 renderBufferFrames = 0;
    renderClient->GetBufferSize(&renderBufferFrames);
    double driftCorrectionAccumulator = 0.0;
    ULONGLONG lastHealthLog = GetTickCount64();

    // Main loop
    while (g_running) {
        if (g_watchConfig) {
            ULONGLONG nextStamp = GetFileWriteStamp(g_configFile);
            if (nextStamp != 0 && nextStamp != configStamp) {
                configStamp = nextStamp;
                g_reloadConfig = true;
            }
        }

        // Check for config reload request
        if (g_reloadConfig.exchange(false)) {
            std::cout << "\n[INFO] Reloading config..." << std::endl;
            ConfigFile::load(g_configFile, g_config);
            g_controller->applyConfig(g_config, false);
            g_controller->printStatus();
        }
        
        // Check for status request
        if (g_showStatus.exchange(false)) {
            g_controller->printStatus();
            PrintAudioHealth(
                processedFrames, droppedFrames, silentFrames, packets, conversionErrors,
                captureDiscontinuities, renderStarvations, renderErrors,
                dspCalls, dspTotalMicros, dspMaxMicros,
                dspDeadlineMisses, dspCriticalStalls,
                minRenderPadding, maxRenderPadding, renderBufferFrames);
        }

        ULONGLONG now = GetTickCount64();
        if (now - lastHealthLog >= 5000) {
            lastHealthLog = now;
            PrintAudioHealth(
                processedFrames, droppedFrames, silentFrames, packets, conversionErrors,
                captureDiscontinuities, renderStarvations, renderErrors,
                dspCalls, dspTotalMicros, dspMaxMicros,
                dspDeadlineMisses, dspCriticalStalls,
                minRenderPadding, maxRenderPadding, renderBufferFrames);
        }
        
        // Wait for capture event
        DWORD waitResult = WaitForSingleObject(captureEvent, 100);
        if (waitResult == WAIT_TIMEOUT) continue;
        if (waitResult != WAIT_OBJECT_0) break;
        
        // Process packets
        UINT32 packetLength = 0;
        hr = captureService->GetNextPacketSize(&packetLength);
        
        while (SUCCEEDED(hr) && packetLength > 0 && g_running) {
            BYTE* captureData = nullptr;
            UINT32 numFrames = 0;
            DWORD flags = 0;
            
            hr = captureService->GetBuffer(&captureData, &numFrames, &flags, nullptr, nullptr);
            if (FAILED(hr)) break;

            if ((flags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY) != 0) {
                captureDiscontinuities += 1;
            }
            
            if (numFrames > 0) {
                UINT32 padding = 0;
                UINT32 renderBufSize = 0;
                HRESULT bufferSizeHr = renderClient->GetBufferSize(&renderBufSize);
                HRESULT paddingHr = renderClient->GetCurrentPadding(&padding);
                if (FAILED(bufferSizeHr) || FAILED(paddingHr)) {
                    renderErrors += 1;
                    captureService->ReleaseBuffer(numFrames);
                    break;
                }

                minRenderPadding = min(minRenderPadding, padding);
                maxRenderPadding = max(maxRenderPadding, padding);
                
                UINT32 available = renderBufSize - padding;
                UINT32 toWrite = numFrames;
                const double targetPadding = renderBufSize / 5.0;
                const double normalizedError = (static_cast<double>(padding) - targetPadding) / renderBufSize;
                driftCorrectionAccumulator += normalizedError * 0.5;
                if (driftCorrectionAccumulator >= 1.0 && toWrite > 1) {
                    const UINT32 correction = std::min<UINT32>(
                        static_cast<UINT32>(driftCorrectionAccumulator),
                        std::min<UINT32>(8, toWrite - 1));
                    toWrite -= correction;
                    driftCorrectionAccumulator -= correction;
                } else if (driftCorrectionAccumulator <= -1.0 && available > toWrite) {
                    toWrite += 1;
                    driftCorrectionAccumulator += 1.0;
                }

                const UINT32 highPaddingThreshold = renderBufSize * 3 / 5;
                if (padding > highPaddingThreshold && toWrite > 1) {
                    const UINT32 recoveryStep = std::max<UINT32>(1, renderBufSize / 32);
                    const UINT32 emergencyCorrection = std::min<UINT32>(
                        8,
                        1 + (padding - highPaddingThreshold) / recoveryStep);
                    toWrite -= std::min<UINT32>(emergencyCorrection, toWrite - 1);
                }
                if (toWrite > available) {
                    if (available == 0) renderStarvations += 1;
                    droppedFrames += toWrite - available;
                    toWrite = available;
                }
                
                if (toWrite > 0) {
                    BYTE* renderData = nullptr;
                    hr = renderService->GetBuffer(toWrite, &renderData);
                    
                    if (SUCCEEDED(hr) && renderData) {
                        bool silent = (flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0;
                        
                        if (silent) {
                            memset(renderData, 0, toWrite * renderFormat->nBlockAlign);
                            silentFrames += toWrite;
                        } else {
                            bool converted = ConvertToFloat(captureData, numFrames, captureFormat, captureFloat);
                            if (!converted) {
                                conversionErrors += 1;
                                memset(renderData, 0, toWrite * renderFormat->nBlockAlign);
                            } else {
                                AdaptChannels(captureFloat, numFrames, captureFormat->nChannels, 2, dspFloat);
                                if (g_controller) {
                                    auto dspStart = std::chrono::steady_clock::now();
                                    g_controller->process(dspFloat.data(), numFrames);
                                    auto dspEnd = std::chrono::steady_clock::now();
                                    uint64_t dspMicros = static_cast<uint64_t>(
                                        std::chrono::duration_cast<std::chrono::microseconds>(dspEnd - dspStart).count());
                                    dspCalls += 1;
                                    dspTotalMicros += dspMicros;
                                    dspMaxMicros = max(dspMaxMicros, dspMicros);
                                    uint64_t packetDeadlineMicros =
                                        static_cast<uint64_t>(toWrite) * 1000000ULL /
                                        static_cast<uint64_t>(captureFormat->nSamplesPerSec);
                                    if (dspMicros > packetDeadlineMicros) dspDeadlineMisses += 1;
                                    if (dspMicros > static_cast<uint64_t>(bufferMs) * 1000ULL) {
                                        dspCriticalStalls += 1;
                                    }
                                }
                                ResampleInterleaved(dspFloat, numFrames, toWrite, 2, resampledDspFloat);
                                AdaptChannels(resampledDspFloat, toWrite, 2, renderFormat->nChannels, renderFloat);
                                if (!ConvertFromFloat(renderFloat, toWrite, renderFormat, renderData)) {
                                    conversionErrors += 1;
                                    memset(renderData, 0, toWrite * renderFormat->nBlockAlign);
                                }
                                processedFrames += toWrite;
                                packets += 1;
                            }
                        }

                        if (!silent) {
                            renderService->ReleaseBuffer(toWrite, 0);
                        } else {
                            renderService->ReleaseBuffer(toWrite, AUDCLNT_BUFFERFLAGS_SILENT);
                        }
                    } else {
                        renderErrors += 1;
                    }
                }
            }
            
            captureService->ReleaseBuffer(numFrames);
            hr = captureService->GetNextPacketSize(&packetLength);
        }
    }
    
    std::cout << "\n[INFO] Stopping..." << std::endl;
    
    captureClient->Stop();
    renderClient->Stop();
    
    // Wait for keyboard thread
    if (kbThread && kbThread->joinable()) kbThread->join();
    delete kbThread;
    
cleanup:
    cleanupDsp();
    if (mmcssHandle) AvRevertMmThreadCharacteristics(mmcssHandle);
    if (captureEvent) CloseHandle(captureEvent);
    if (renderService) renderService->Release();
    if (captureService) captureService->Release();
    if (renderFormat) CoTaskMemFree(renderFormat);
    if (captureFormat) CoTaskMemFree(captureFormat);
    if (renderClient) renderClient->Release();
    if (captureClient) captureClient->Release();
}

int main(int argc, char* argv[]) {
    SetConsoleCtrlHandler(ConsoleHandler, TRUE);
    
    // Parse args
    int selectedCapture = -1;
    int selectedOutput = -1;
    g_configFile = ConfigFile::getDefaultPath();
    
    for (int i = 1; i < argc; i++) {
        if ((strcmp(argv[i], "-i") == 0 || strcmp(argv[i], "--input") == 0 || strcmp(argv[i], "--capture") == 0) && i + 1 < argc) {
            selectedCapture = atoi(argv[++i]);
        }
        else if ((strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--output") == 0) && i + 1 < argc) {
            selectedOutput = atoi(argv[++i]);
        }
        else if ((strcmp(argv[i], "-c") == 0 || strcmp(argv[i], "--config") == 0) && i + 1 < argc) {
            g_configFile = argv[++i];
        }
        else if (strcmp(argv[i], "--watch-config") == 0) {
            g_watchConfig = true;
        }
        else if (strcmp(argv[i], "--buffer-ms") == 0 && i + 1 < argc) {
            g_bufferMs = atoi(argv[++i]);
            if (g_bufferMs < 20) g_bufferMs = 20;
            if (g_bufferMs > 200) g_bufferMs = 200;
        }
        else if (strcmp(argv[i], "--list-devices-json") == 0) {
            return ListDevicesJson(eRender);
        }
        else if (strcmp(argv[i], "--get-default-json") == 0) {
            return GetDefaultDeviceJson(eRender);
        }
        else if (strcmp(argv[i], "--set-default") == 0 && i + 1 < argc) {
            return SetDefaultRenderDevice(atoi(argv[++i]));
        }
        else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            std::cout << "JamesDSP for Windows\n" << std::endl;
            std::cout << "Usage: JamesDSPConsole.exe [options]\n" << std::endl;
            std::cout << "Options:" << std::endl;
            std::cout << "  -i, --input <idx>    Capture/source render endpoint index" << std::endl;
            std::cout << "  -o, --output <idx>   Processed output device index" << std::endl;
            std::cout << "  -c, --config <file>  Config file path" << std::endl;
            std::cout << "  --watch-config       Reload config automatically when it changes" << std::endl;
            std::cout << "  --buffer-ms <ms>     Shared WASAPI buffer target, 20-200 ms" << std::endl;
            std::cout << "  --list-devices-json  Print active render devices as JSON and exit" << std::endl;
            std::cout << "  --get-default-json   Print the current default render device as JSON and exit" << std::endl;
            std::cout << "  --set-default <idx>  Set Windows default playback endpoint and exit" << std::endl;
            std::cout << "  -h, --help           Show this help" << std::endl;
            return 0;
        }
    }
    
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr)) {
        std::cerr << "[ERROR] COM init failed" << std::endl;
        return 1;
    }
    
    PrintUsage();
    ListDevices(eRender);
    
    // Get capture/source render endpoint.
    IMMDevice* captureDevice = selectedCapture >= 0
        ? GetDeviceByIndex(eRender, selectedCapture)
        : GetDefaultDevice(eRender, eConsole);
    if (!captureDevice) {
        std::cerr << "[ERROR] No capture device" << std::endl;
        CoUninitialize();
        return 1;
    }
    
    // Print capture device name
    {
        IPropertyStore* props;
        captureDevice->OpenPropertyStore(STGM_READ, &props);
        PROPVARIANT name;
        PropVariantInit(&name);
        props->GetValue(PKEY_Device_FriendlyName, &name);
        std::wcout << L"\n[INFO] Capture: " << name.pwszVal << std::endl;
        PropVariantClear(&name);
        props->Release();
    }
    
    // Get output device
    IMMDevice* renderDevice = nullptr;
    IMMDeviceEnumerator* enumerator = nullptr;
    IMMDeviceCollection* collection = nullptr;
    
    CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
        __uuidof(IMMDeviceEnumerator), (void**)&enumerator);
    
    if (enumerator) {
        enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &collection);
        
        if (collection) {
            UINT count;
            collection->GetCount(&count);
            
            LPWSTR captureId;
            captureDevice->GetId(&captureId);
            
            if (selectedOutput >= 0 && (UINT)selectedOutput < count) {
                collection->Item(selectedOutput, &renderDevice);
                
                LPWSTR id;
                renderDevice->GetId(&id);
                if (wcscmp(id, captureId) == 0) {
                    std::cerr << "[ERROR] Output same as capture" << std::endl;
                    renderDevice->Release();
                    renderDevice = nullptr;
                }
                CoTaskMemFree(id);
            } else {
                // Auto-select first non-capture device
                for (UINT i = 0; i < count; i++) {
                    IMMDevice* dev;
                    collection->Item(i, &dev);
                    LPWSTR id;
                    dev->GetId(&id);
                    if (wcscmp(id, captureId) != 0) {
                        renderDevice = dev;
                        CoTaskMemFree(id);
                        break;
                    }
                    CoTaskMemFree(id);
                    dev->Release();
                }
            }
            
            CoTaskMemFree(captureId);
            collection->Release();
        }
        enumerator->Release();
    }
    
    if (!renderDevice) {
        std::cerr << "[ERROR] No output device. Use -o <index>" << std::endl;
        captureDevice->Release();
        CoUninitialize();
        return 1;
    }
    
    // Print output device name
    {
        IPropertyStore* props;
        renderDevice->OpenPropertyStore(STGM_READ, &props);
        PROPVARIANT name;
        PropVariantInit(&name);
        props->GetValue(PKEY_Device_FriendlyName, &name);
        std::wcout << L"[INFO] Output: " << name.pwszVal << std::endl;
        PropVariantClear(&name);
        props->Release();
    }
    
    std::cout << "[INFO] Config: " << g_configFile << std::endl;
    
    // Run
    AudioProcessingLoop(captureDevice, renderDevice);
    
    captureDevice->Release();
    renderDevice->Release();
    CoUninitialize();
    
    std::cout << "[INFO] Done." << std::endl;
    return 0;
}
