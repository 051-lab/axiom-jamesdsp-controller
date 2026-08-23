#include "DspController.h"
#include <QDebug>
#include <QFileInfo>

// SEH-safe wrapper for Convolver loading (no C++ objects allowed in SEH functions)
static bool safeConvolverLoad(JamesDSPLib* jdsp, float* impulse, int channels, int frames, int mode) {
    __try {
        Convolver1DLoadImpulseResponse(jdsp, impulse, channels, frames, mode);
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return false; // Crashed
    }
}

// SEH-safe wrapper for JamesDSP initialization
static bool safeJamesDSPInit(JamesDSPLib* jdsp, int blockSize, float sampleRate) {
    __try {
        JamesDSPInit(jdsp, blockSize, sampleRate);
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return false; // Crashed in init
    }
}

DspManager::DspManager() {
    // Zero-initialize the DSP struct
    memset(&m_jdsp, 0, sizeof(JamesDSPLib));
}

DspManager::~DspManager() {
    if(m_initialized) {
        JamesDSPFree(&m_jdsp);
    }
}

void DspManager::init(int sampleRate, int blockSize) {
    QMutexLocker locker(&m_mutex);
    
    qDebug() << "[DspManager] init called. SR:" << sampleRate << " BlockSize:" << blockSize;
    
    if(m_initialized) {
        qDebug() << "[DspManager] Already initialized, freeing first...";
        JamesDSPFree(&m_jdsp);
        m_initialized = false;
    }
    
    // Zero-init before JamesDSPInit
    memset(&m_jdsp, 0, sizeof(JamesDSPLib));
    // Call global memory allocation ONCE per application lifetime
    static bool globalAllocDone = false;
    if(!globalAllocDone) {
        qDebug() << "[DspManager] Calling JamesDSPGlobalMemoryAllocation...";
        JamesDSPGlobalMemoryAllocation();
        globalAllocDone = true;
        qDebug() << "[DspManager] Global memory allocated.";
    }
    
    qDebug() << "[DspManager] Struct zeroed. Calling JamesDSPInit...";
    
    if(!safeJamesDSPInit(&m_jdsp, blockSize, (float)sampleRate)) {
        qCritical() << "[DspManager] JamesDSPInit CRASHED! SEH caught exception.";
        return;
    }
    
    m_initialized = true;
    qDebug() << "[DspManager] JamesDSPInit SUCCESS. Setting defaults...";
    
    // Set basic defaults
    JamesDSPSetPostGain(&m_jdsp, 0.0);
    qDebug() << "[DspManager] Init complete.";
}

void DspManager::process(float* buffer, int frames) {
    QMutexLocker locker(&m_mutex);
    if(!m_initialized) return;

    // TODO: Deinterleave logic if using processFloatMultiplexd which expects parallel arrays?
    // JNI wrapper processFloatMultiplexd takes interleaved float* and deinterleaves internally?
    // Checking jamesdsp.c: processFloatMultiplexd(struct dspsys*, float*, float*, size_t);
    // It takes input, output, frameCount.
    // If we process in-place, we might need a temp buffer or just pass buffer as both in and out.
    // However, JamesDSP often expects separate channels.
    
    // For now, let's assume we call the multiplexd function which handles interleaved input.
    // NOTE: We need to verify if we can pass same buffer for in/out.
    // Reading jamesdsp.c: processFloatMultiplexd implementation splits channels then calls processInternal.
    // So separate buffers might be safer or just rely on robust implementation.
    
    // Process in-place
    if(m_jdsp.processFloatMultiplexd) {
        m_jdsp.processFloatMultiplexd(&m_jdsp, buffer, buffer, frames);
    }
    float maxAmp = 0.0f;
    for (size_t i = 0; i < frames; ++i) {
        float valL = fabsf(buffer[i * 2]);
        float valR = fabsf(buffer[i * 2 + 1]);
        if (valL > maxAmp) maxAmp = valL;
        if (valR > maxAmp) maxAmp = valR;
    }

    static int logCounter = 0;
    logCounter++;
    if (maxAmp > 1.0f || maxAmp == 0.0f || logCounter % 100 == 0) {
         QFile f("C:/Users/gmaym/.gemini/antigravity/playground/blazing-comet/JamesDSP-Windows/jamesdsp_debug.log");
         if(f.open(QIODevice::Append | QIODevice::Text)) {
             QTextStream out(&f);
             out << "[DSP] MaxAmp: " << maxAmp << " Frames: " << frames << "\n";
         }
    }
}

void DspManager::setBassBoost(bool enabled, int maxGain) {
    QMutexLocker locker(&m_mutex);
    if(!m_initialized) return;
    
    if(enabled) {
        BassBoostEnable(&m_jdsp);
        BassBoostEnable(&m_jdsp);
        // FIX: maxGain is int (e.g. 150 for 15dB), but API expects float dB.
        BassBoostSetParam(&m_jdsp, (float)maxGain / 10.0f);
    } else {
        BassBoostDisable(&m_jdsp);
    }
}

void DspManager::setToneControl(bool enabled, float drive) {
    QMutexLocker locker(&m_mutex);
    if(!m_initialized) return;

    if(enabled) {
        VacuumTubeEnable(&m_jdsp);
        VacuumTubeSetGain(&m_jdsp, (double)drive); // drive is usually dbGain
    } else {
        VacuumTubeDisable(&m_jdsp);
    }
}

void DspManager::setReverb(bool enabled, int preset) {
    QMutexLocker locker(&m_mutex);
    if(!m_initialized) return;

    if(enabled) {
        ReverbEnable(&m_jdsp);
        Reverb_SetParam(&m_jdsp, preset);
    } else {
        ReverbDisable(&m_jdsp);
    }
}

void DspManager::setLimiter(double threshold, double release, double postGain) {
    QMutexLocker locker(&m_mutex);
    if(!m_initialized) return;

    JLimiterSetCoefficients(&m_jdsp, threshold, release);
    JamesDSPSetPostGain(&m_jdsp, postGain);
}

#include <QElapsedTimer>
#include <vector>
#include <fstream>
#include <iostream>
#include <sstream>

// Standard ISO 15-band frequencies
static const double EQ_FREQUENCIES[15] = {
    25.0, 40.0, 63.0, 100.0, 160.0, 250.0, 400.0, 630.0, 
    1000.0, 1600.0, 2500.0, 4000.0, 6300.0, 10000.0, 16000.0
};

// ... (previous methods)

// Helper for file logging
static void logToFile(const std::string& msg) {
    std::ofstream outfile("C:\\Users\\gmaym\\.gemini\\antigravity\\playground\\blazing-comet\\JamesDSP-Windows\\build-final\\debug_log.txt", std::ios_base::app);
    if (outfile.is_open()) {
        outfile << msg << std::endl;
    }
}

// SEH-safe wrapper for Multimodal EQ Compute (No Lock - Double Buffering)
// SEH-safe wrapper for Multimodal EQ Compute (No Lock - Double Buffering)
static void* safeEqualizerCompute(JamesDSPLib* jdsp, int interp, int mode, double* freqs, double* gains, unsigned int targetBlockSize) {
    __try {
        return MultimodalEqualizerCompute(jdsp, interp, mode, freqs, gains, targetBlockSize);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

// SEH-safe wrapper for Multimodal EQ Apply (Requires Lock - Pointer Swap)
static void* safeEqualizerApply(JamesDSPLib* jdsp, int mode, void* newConv) {
    __try {
        return MultimodalEqualizerApply(jdsp, mode, newConv);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

static void safeEqualizerFree(void* conv) {
    __try {
        if(conv) MultimodalEqualizerFreeConv(conv);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

void DspManager::setEqualizer(bool enabled, int mode, int interp, const std::vector<double>& gains) {
    // DIAGNOSTIC: Log entry
    {
        QFile f("C:/Users/gmaym/.gemini/antigravity/playground/blazing-comet/JamesDSP-Windows/jamesdsp_debug.log");
        if(f.open(QIODevice::Append | QIODevice::Text)) {
            QTextStream out(&f);
            out << "[setEQ] ENTRY: enabled=" << enabled << " mode=" << mode << " interp=" << interp << " gains.size=" << gains.size() << "\n";
        }
    }
    if(!m_initialized) return;

    float* eqFil = nullptr; 
    void* shadowConv = nullptr;
    std::vector<double> freqs;
    std::vector<double> gainsArr;

    if(enabled) {
        int nBands = 15;
        freqs.resize(nBands + 2);
        gainsArr.resize(nBands + 2);
        
        freqs[0] = 0.0;
        gainsArr[0] = gains.size() > 0 ? gains[0] : 0.0;
        
        for(int i=0; i < nBands; ++i) {
            freqs[i+1] = EQ_FREQUENCIES[i];
            if(i < gains.size()) gainsArr[i+1] = gains[i];
            else gainsArr[i+1] = 0.0;
        }
        freqs[nBands+1] = 24000.0;
        gainsArr[nBands+1] = gains.size() > nBands-1 ? gains[nBands-1] : 0.0;
        
        // SAFE READ of Active Block Size (Prevents Mismatch)
        unsigned int targetBlockSize = 0;
        {
            QMutexLocker locker(&m_mutex);
            if(m_initialized && m_jdsp.mEQ.conv._blockSize > 0) {
                 targetBlockSize = m_jdsp.mEQ.conv._blockSize;
            } else if (m_initialized) {
                 targetBlockSize = (unsigned int)m_jdsp.blockSize;
            }
        }
        if(targetBlockSize == 0) targetBlockSize = 4096; // Fallback
        
        // DIAGNOSTIC: Log before compute
        {
            QFile f("C:/Users/gmaym/.gemini/antigravity/playground/blazing-comet/JamesDSP-Windows/jamesdsp_debug.log");
            if(f.open(QIODevice::Append | QIODevice::Text)) {
                QTextStream out(&f);
                out << "[setEQ] COMPUTE: targetBS=" << targetBlockSize << " interp=" << interp << " mode=" << mode << "\n";
            }
        }
        
        // Phase 1: Heavy computation (Lock-Free)
        shadowConv = safeEqualizerCompute(&m_jdsp, interp, mode, freqs.data(), gainsArr.data(), targetBlockSize);
        
        // DIAGNOSTIC: Log compute result
        {
            QFile f("C:/Users/gmaym/.gemini/antigravity/playground/blazing-comet/JamesDSP-Windows/jamesdsp_debug.log");
            if(f.open(QIODevice::Append | QIODevice::Text)) {
                QTextStream out(&f);
                out << "[setEQ] COMPUTE DONE: shadowConv=" << (shadowConv ? "valid" : "NULL") << "\n";
            }
        }
        
        if (mode == 0 && !shadowConv) {
            // DIAGNOSTIC: Log early return
            {
                QFile f("C:/Users/gmaym/.gemini/antigravity/playground/blazing-comet/JamesDSP-Windows/jamesdsp_debug.log");
                if(f.open(QIODevice::Append | QIODevice::Text)) {
                    QTextStream out(&f);
                    out << "[setEQ] EARLY RETURN: FIR mode with NULL shadowConv!\n";
                }
            }
            return;
        }
    }

    void* oldConv = nullptr;

    // Phase 2: Application (Locked)
    {
        QMutexLocker locker(&m_mutex);
        
        if(!m_initialized) {
            if(shadowConv) safeEqualizerFree(shadowConv);
            return;
        }

        if(enabled) {
            oldConv = safeEqualizerApply(&m_jdsp, mode, shadowConv);
            
            if(m_jdsp.equalizerEnabled == 0) {
                 MultimodalEqualizerEnable(&m_jdsp, 1);
            }
        } else {
            MultimodalEqualizerDisable(&m_jdsp);
            if(shadowConv) safeEqualizerFree(shadowConv);
        }
        
        // Verify Block Size via Debug
        if(enabled && m_jdsp.equalizerEnabled) {
            // Log if needed
        }
    }
    
    // Phase 3: Cleanup (Unlocked)
    if (oldConv) {
        safeEqualizerFree(oldConv);
    }
}

void DspManager::setCrossfeed(bool enabled, int mode) {
    QMutexLocker locker(&m_mutex);
    if(!m_initialized) return;
    
    if(enabled) {
        CrossfeedEnable(&m_jdsp, 1);
        CrossfeedChangeMode(&m_jdsp, mode);
    } else {
        CrossfeedDisable(&m_jdsp);
    }
}

void DspManager::setStereoWide(bool enabled, int level) {
    QMutexLocker locker(&m_mutex);
    if(!m_initialized) return;
    
    if(enabled) {
        StereoEnhancementEnable(&m_jdsp);
        StereoEnhancementSetParam(&m_jdsp, (float)level / 100.0f); // 0.0 to 1.0? Or more?
        // Linux GUI uses 0-100 slider mapping to what? 
        // Checked logic: It maps to 0-1 range usually for width.
    } else {
        StereoEnhancementDisable(&m_jdsp);
    }
}

void DspManager::setDdc(bool enabled, const QString& vdcFile) {
    QMutexLocker locker(&m_mutex);
    if(!m_initialized) return;
    
    if(enabled && !vdcFile.isEmpty()) {
        std::ifstream file(vdcFile.toStdString());
        if(file.is_open()) {
            std::stringstream buffer;
            buffer << file.rdbuf();
            std::string content = buffer.str();
            
            // Check parsing result
            int result = DDCStringParser(&m_jdsp, const_cast<char*>(content.c_str()));
            if(result == 1) { // Success? Need to verify return code meaning
                 DDCEnable(&m_jdsp, 1);
                 qDebug() << "[DSP] DDC Loaded:" << vdcFile;
            } else {
                 qDebug() << "[DSP] DDC Parse Failure";
            }
        }
    } else {
        DDCDisable(&m_jdsp);
    }
}

// Helper to calculate dB
static float linearToDb(float lin) {
    if (lin <= 1e-9f) return -180.0f; // Silence floor
    return 20.0f * log10f(lin);
}

// Helper to calculate Linear
static float dbToLinear(float db) {
    return powf(10.0f, db / 20.0f);
}

// Android "Advanced Waveform Editor" Replication
// Params: StartTrim(dB); EndTrim(dB); LeftSrcIdx; RightSrcIdx; LeftPhase; RightPhase
// Default: "-80;-100;0;0;0;0"
static void processImpulseResponse(std::vector<float>& impulse, int& channels, const std::string& advParams) {
    if (advParams.empty() || impulse.empty()) return;

    qDebug() << "[DSP] processAdvIR Entry. Frames:" << (impulse.size()/channels) << " Ch:" << channels << " Params:" << QString::fromStdString(advParams);

    // Parse loop
    std::vector<float> params; 
    std::stringstream ss(advParams);
    std::string item;
    while (std::getline(ss, item, ';')) {
        try {
            params.push_back(std::stof(item));
        } catch (...) {
            params.push_back(0.0f);
        }
    }

    // Ensure we have 6 params, fill defaults if missing
    if (params.size() < 1) params.push_back(-80.0f);
    if (params.size() < 2) params.push_back(-100.0f);
    if (params.size() < 3) params.push_back(0.0f); // L Src
    if (params.size() < 4) params.push_back(1.0f); // R Src (Default to 1 for Stereo)
    if (params.size() < 5) params.push_back(0.0f); // L Phase
    if (params.size() < 6) params.push_back(0.0f); // R Phase

    float startThreshDb = params[0];
    float endThreshDb = params[1];
    int lSrcIdx = (int)params[2];
    int rSrcIdx = (int)params[3];
    int lPhase = (int)params[4];
    int rPhase = (int)params[5];
    
    qDebug() << "[DSP] Parsed: L_Src=" << lSrcIdx << " R_Src=" << rSrcIdx;

    // AUTO-FIX: If file is Mono (1 ch), requests for Ch1 (Right) are impossible.
    if (channels == 1) {
        if (lSrcIdx == 1) lSrcIdx = 0;
        if (rSrcIdx == 1) rSrcIdx = 0;
        qDebug() << "[DSP] Mono Input Detected: Remapping Ch1 requests to Ch0.";
    }

    // AUTO-DETECT FAKE STEREO
    // DISABLED: This was causing issues where explicit user routing (e.g. routing Right Ch to Left) 
    // was being overridden because the DSP thought a channel was "silent".
    /* 
    if (channels == 2 && !impulse.empty()) {
        float maxL = 0.0f;
        float maxR = 0.0f;
        int framesCheck = impulse.size() / 2;
        for (int i = 0; i < framesCheck; i+=10) { 
             float l = fabsf(impulse[i*2]);
             float r = fabsf(impulse[i*2+1]);
             if(l > maxL) maxL = l;
             if(r > maxR) maxR = r;
        }
        
        bool leftActive = (maxL > 0.001f);
        bool rightActive = (maxR > 0.001f);
        
        if (leftActive && !rightActive) {
            qDebug() << "[DSP] Smart Mono: Ch0 Active, Ch1 Silent. Remapping Ch1 requests to Ch0.";
            if (lSrcIdx == 1) lSrcIdx = 0;
            if (rSrcIdx == 1) rSrcIdx = 0;
        } else if (!leftActive && rightActive) {
            qDebug() << "[DSP] Smart Mono: Ch0 Silent, Ch1 Active. Remapping Ch0 requests to Ch1.";
            if (lSrcIdx == 0) lSrcIdx = 1;
            if (rSrcIdx == 0) rSrcIdx = 1;
        }
    }
    */
    
    // 1. Channel Routing & Phase (Create new buffer)
    int oldChannels = channels;
    int frames = impulse.size() / oldChannels;
    
    // FIX: Detect "True Stereo" (4ch) or Multi-channel with Default Routing
    // If user selects "Default" (L->0, R->1, Normal Phase), we should NOT downmix to 2 channels
    // if the input is already >2 (e.g. 4-channel True Stereo).
    bool isIdentityRouting = (lSrcIdx == 0 && rSrcIdx == 1 && lPhase == 0 && rPhase == 0);
    
    if (oldChannels > 2 && isIdentityRouting) {
        qDebug() << "[DSP] Multi-channel (" << oldChannels << "ch) detected with Default Routing. Preserving original layout (True Stereo).";
        // Skip downmix loop - leave 'impulse' and 'channels' as is.
    } 
    else {
        int newChannels = 2; 
        std::vector<float> processed(frames * newChannels);

        for (int i = 0; i < frames; ++i) {
            // Left Output
            float lSample = 0.0f;
            if (lSrcIdx < oldChannels) lSample = impulse[i * oldChannels + lSrcIdx];
            if (lPhase == 1) lSample *= -1.0f;
            
            // Right Output
            float rSample = 0.0f;
            if (rSrcIdx < oldChannels) rSample = impulse[i * oldChannels + rSrcIdx];
            if (rPhase == 1) rSample *= -1.0f;

            processed[i * 2] = lSample;
            processed[i * 2 + 1] = rSample;
        }

        // Update impulse to new layout
        impulse = processed;
        channels = newChannels;
    }
    
    // 2. Trim Start
    int trimStartIdx = 0;
    float startThreshLin = dbToLinear(startThreshDb);
    
    if (startThreshDb > -150.0f) { 
        for (int i = 0; i < frames; ++i) {
            // GENERIC TRIM: Check energy of ALL channels to determine silence
            bool frameSilent = true;
            for(int c=0; c < channels; ++c) {
                if(fabsf(impulse[i * channels + c]) > startThreshLin) {
                    frameSilent = false;
                    break;
                }
            }
            
            if (!frameSilent) {
                trimStartIdx = i;
                break;
            }
        }
    }

    // 3. Trim End
    int trimEndIdx = frames;
    float endThreshLin = dbToLinear(endThreshDb);
    
    if (endThreshDb > -150.0f) {
       for (int i = frames - 1; i >= trimStartIdx; --i) {
            bool frameSilent = true;
            for(int c=0; c < channels; ++c) {
                if(fabsf(impulse[i * channels + c]) > endThreshLin) {
                    frameSilent = false;
                    break;
                }
            }
            
            if (!frameSilent) {
                trimEndIdx = i + 1; 
                break;
            }
       }
    }
    
    // Apply Trim
    if (trimStartIdx > 0 || trimEndIdx < frames) {
        int newFrameCount = trimEndIdx - trimStartIdx;
        if (newFrameCount <= 0) {
           newFrameCount = 0;
           impulse.clear();
        } else {
            std::vector<float> trimmed(newFrameCount * channels);
            size_t startOffset = trimStartIdx * channels;
            size_t endOffset = trimEndIdx * channels;
            
            if (endOffset <= impulse.size()) {
                 std::copy(impulse.begin() + startOffset, 
                           impulse.begin() + endOffset, 
                           trimmed.begin());
                 impulse = trimmed;
            }
        }
    }
    
    // DIAGNOSTIC: Check Energy
    float eL = 0.0f, eR = 0.0f;
    for(size_t i=0; i<impulse.size(); i+=2) {
        eL += fabsf(impulse[i]);
        eR += fabsf(impulse[i+1]);
    }
    
    {
        QFile f("C:/Users/gmaym/.gemini/antigravity/playground/blazing-comet/JamesDSP-Windows/jamesdsp_debug.log");
        if(f.open(QIODevice::Append | QIODevice::Text)) {
            QTextStream out(&f);
            out << "[DSP] AdvImp Result. Trim:" << trimStartIdx << "-" << trimEndIdx 
                << " Energy L:" << eL << " R:" << eR 
                << " NewFrames:" << (impulse.size()/channels) << "\n";
             out << "[DSP] Params Used: Start=" << startThreshDb << " End=" << endThreshDb
                 << " L_Src=" << lSrcIdx << " R_Src=" << rSrcIdx 
                 << " L_Phase=" << lPhase << " R_Phase=" << rPhase << "\n";
        }
    }
}

struct WavHeader {
    char riff[4];
    uint32_t overall_size;
    char wave[4];
    char fmt[4];
    uint32_t fmt_chunk_size;
    uint16_t format_type;
    uint16_t channels;
    uint32_t sample_rate;
    uint32_t byte_rate;
    uint16_t block_align;
    uint16_t bits_per_sample;
    char data[4];
    uint32_t data_size;
};

// Note: Update logic to accept adv string
void DspManager::setConvolver(bool enabled, const QString& irFile, int optimization, const QString& advParams) {
    QMutexLocker locker(&m_mutex);
    if(!m_initialized) return;

    if(enabled && !irFile.isEmpty()) {
         std::ifstream file(irFile.toStdString(), std::ios::binary);
         if(file.is_open()) {
             // Robust WAV parsing using chunk iteration
             char riff[4];
             file.read(riff, 4);
             if(memcmp(riff, "RIFF", 4) != 0) return;
             
             file.seekg(4, std::ios::cur); // Skip file size
             
             char wave[4];
             file.read(wave, 4);
             if(memcmp(wave, "WAVE", 4) != 0) return;
             
             uint16_t channels = 0;
             uint32_t sampleRate = 0;
             uint16_t bitsPerSample = 0;
             uint16_t formatType = 0;
             std::vector<float> impulse;
             bool fmtFound = false;
             bool dataFound = false;
             
             char chunkId[4];
             uint32_t chunkSize;
             
             while(file.read(chunkId, 4) && file.read((char*)&chunkSize, 4)) {
                 if(memcmp(chunkId, "fmt ", 4) == 0) {
                     // Parse fmt
                     uint64_t chunkStart = file.tellg();
                     if(chunkSize >= 16) {
                         file.read((char*)&formatType, 2);
                         file.read((char*)&channels, 2);
                         file.read((char*)&sampleRate, 4);
                         file.seekg(6, std::ios::cur); // Skip byteRate (4) + blockAlign (2)
                         file.read((char*)&bitsPerSample, 2);
                         fmtFound = true;
                     }
                     // Skip remaining chunk bytes (if e.g. extensible)
                     file.seekg(chunkStart + chunkSize, std::ios::beg);
                 }
                 else if(memcmp(chunkId, "data", 4) == 0) {
                     if(!fmtFound) {
                         qDebug() << "[DSP] Data chunk found before fmt chunk";
                         break;
                     }
                     
                     uint32_t numSamples = 0;
                     if(bitsPerSample > 0) numSamples = chunkSize / (bitsPerSample / 8);
                     else { /* invalid bps */ break; }
                     
                     impulse.resize(numSamples);
                     
                     if(bitsPerSample == 32 && formatType == 3) {
                         // Float
                         file.read((char*)impulse.data(), chunkSize);
                         dataFound = true;
                     }
                     else if(bitsPerSample == 16) {
                         // PCM 16-bit
                         std::vector<int16_t> temp(numSamples);
                         file.read((char*)temp.data(), chunkSize);
                         // Convert to float
                         for(size_t i=0; i<numSamples; ++i) impulse[i] = temp[i] / 32768.0f;
                         dataFound = true;
                     }
                     else if(bitsPerSample == 24) {
                         // PCM 24-bit
                         // 3 bytes per sample
                         struct int24 { int8_t b[3]; };
                         std::vector<int24> temp(numSamples);
                         file.read((char*)temp.data(), numSamples * 3);
                         for(size_t i=0; i<numSamples; ++i) {
                             int32_t val = (temp[i].b[2] << 16) | (temp[i].b[1] << 8) | (temp[i].b[0] & 0xFF);
                             impulse[i] = val / 8388608.0f;
                         }
                         dataFound = true;
                     }
                     else {
                         qDebug() << "[DSP] Unsupported bits per sample:" << bitsPerSample;
                     }
                     // Stop after data
                 }
                 else {
                     // Skip unknown chunks
                     file.seekg(chunkSize, std::ios::cur);
                 }
                 
                 // Alignment pad
                 if(chunkSize & 1) file.seekg(1, std::ios::cur);
             }
             
             // Load Convolver
             if(dataFound && impulse.size() > 0) {
                 // Validate Impulse Data
                 for(float s : impulse) {
                     if(std::isnan(s) || std::isinf(s)) {
                         qWarning() << "[DSP] Invalid Impulse Response (NaN/Inf). Aborting.";
                         return;
                     }
                 }
                 
                 qDebug() << "[DSP] Convolver Pre-Load Check. Impulse Size:" << impulse.size() << " Channels:" << channels;
                 
                  // *** ANDROID FEATURE REPLICATION ***
                 // Apply processing (Trim/Routing)
                 // If advParams is empty, use default "80;-100;0;0;0;0" ?? 
                 // Actually, if empty, maybe user didn't want it. 
                 // But for now, let's respect the argument.
                 if(!advParams.isEmpty()) {
                      int chInt = (int)channels;
                      processImpulseResponse(impulse, chInt, advParams.toStdString());
                      channels = (uint16_t)chInt;
                 }
                 
                 // Cache for visualization (FIX - Cache the PROCESSED IR so user sees what they hear)
                 m_cachedIrData = impulse;
                 m_cachedIrChannels = channels;

                 // Check for Silence or Empty to prevent Division-by-Zero in Library Normalization
                 bool isSilent = true;
                 if(impulse.empty()) isSilent = true;
                 else {
                     for(float s : impulse) {
                         if(fabsf(s) > 1e-6f) { isSilent = false; break; }
                     }
                 }

                 if(isSilent) {
                     qDebug() << "[DSP] Processed IR is Silent. Generating ROBUST silence buffer (16384 samples).";
                     // FIX: Use a larger buffer (16384) to ensure it's larger than any reasonable playback block size (e.g. 4096).
                     // Small buffers (256) might be rejected by the partitioner or cause the engine to bypass.
                     channels = 2; // Force stereo silence
                     impulse.assign(16384, 0.0f); // Zeroed buffer
                     // Add a tiny epsilon at the end to ensure it's not "null" if the library checks
                     impulse[16383] = 1e-9f; 
                 } else {
                     qDebug() << "[DSP] Processed IR is Active. Size:" << impulse.size();
                 }

                 // Use SEH-safe helper
                 bool loadOk = safeConvolverLoad(&m_jdsp, impulse.data(), channels, impulse.size() / channels, optimization);
                 if(loadOk) {
                     Convolver1DEnable(&m_jdsp);
                     qDebug() << "[DSP] Convolver Loaded:" << irFile;
                 } else {
                     qCritical() << "[DSP] SEH Exception in Convolver! File may be corrupt or unsupported.";
                     Convolver1DDisable(&m_jdsp);
                 }
                 return; // Done
            }
         }
         
         // Fallback
         Convolver1DDisable(getLib());
    } else {
        Convolver1DDisable(getLib());
    }
}

void DspManager::setCompander(bool enabled, bool modeA, double timeConst, int granularity, int tfresolution) {
     QMutexLocker locker(&m_mutex);
     if(!m_initialized) return;

     if(enabled) {
         CompressorEnable(&m_jdsp, 1); // 1 = force refresh? or enabled?
         // timeConst: e.g. 0.22 from slider/100
         CompressorSetParam(&m_jdsp, (float)timeConst, granularity, tfresolution, 1);
     } else {
         CompressorDisable(&m_jdsp);
     }
}

void DspManager::setLiveProgContent(bool enabled, const QString& content) {
    QMutexLocker locker(&m_mutex);
    if(!m_initialized) return;

    if(enabled && !content.isEmpty()) {
        // Check for null content
        if (content.trimmed().isEmpty()) {
            qWarning() << "[DSP] LiveProg script content is empty.";
            return;
        }

        int result = LiveProgStringParser(&m_jdsp, const_cast<char*>(content.toStdString().c_str()), nullptr, 0);
        
        if (result == 1) {
            LiveProgEnable(&m_jdsp);
            qDebug() << "[DSP] LiveProg Content Loaded SUCCESS.";
        } else {
            qCritical() << "[DSP] LiveProg Compilation FAILED. Error Code:" << result;
            // Don't enable if compilation failed
            // LiveProgDisable(&m_jdsp); // Optional: disable on failure?
        }
    } else {
        LiveProgDisable(&m_jdsp);
        qDebug() << "[DSP] LiveProg Disabled";
    }
}

void DspManager::setLiveprog(bool enabled, const QString& eelScript) {
    if(enabled && !eelScript.isEmpty()) {
        std::ifstream file(eelScript.toStdString());
        if(file.is_open()) {
            std::stringstream buffer;
            buffer << file.rdbuf();
            std::string content = buffer.str();
            
            // Unlock mutex here because setLiveProgContent will lock it
            // Actually, we are not holding it yet.
            setLiveProgContent(true, QString::fromStdString(content));
            qDebug() << "[DSP] LiveProg File Loaded:" << eelScript;
        } else {
            qWarning() << "[DSP] Failed to open LiveProg file:" << eelScript;
        }
    } else {
        setLiveProgContent(false, "");
    }
}
void DspManager::setArbitraryEq(bool enabled, const QString& curveString) {
    QMutexLocker locker(&m_mutex);
    if(!m_initialized) return;

    if(enabled) {
        // String format: "freq:gain:freq:gain..."
        // If the string starts with "GraphicEQ:", strip it.
        QString clean = curveString;
        if(clean.startsWith("GraphicEQ:", Qt::CaseInsensitive)) {
            // Remove prefix and potential leading space
            clean = clean.mid(10).trimmed();
            // Replace semicolons with colons if needed, or check format
            // The parser likely expects space or colon separated?
            // ArbFIRGen.c uses strtod which skips whitespace. 
            // It splits by ':' if present. 
            // "GraphicEQ: 20 0; 50 0;" -> "20 0; 50 0;"
            // If parser handles it, great.
            // Let's ensure it's in a format the C parser likes.
            // C parser: strchr(str, ':'). If not found, assumes numbers separated by space?
            // "20 -5: 100 0" -> 2 nodes.
            // My GraphicEqPage outputs "GraphicEQ: 20 0; 24000 0;"
            // I should replace ';' with ' ' just in case, or check parser logic.
            // Parser: `char *symSt = strchr(frArbitraryEqString, ':');` -> If there IS a colon, it starts parsing from there?
            // "GraphicEQ: 20 0" -> colon is at 9. 
            // If passed "20 0; 50 0", no colon.
            // The parser seems to handle "GraphicEQ:" style if passed the whole string.
            // Let's pass the raw string if possible, or stripped.
            // Wait, ArbFIRGen.c: `char *symSt = strchr(frArbitraryEqString, ':');`
            // If passed "GraphicEQ: ...", symSt points to ':'. p starts there.
            // Then it parses floats.
            
            // However, our string uses ';' as separator between nodes.
            // `get_floatArb` uses `strtof`.
            // We should probably replace ';' with ' ' to be safe.
            clean = clean.replace(";", " ");
        }
        
        QByteArray ba = clean.toLatin1();
        char* str = ba.data();
        
        ArbitraryResponseEqualizerEnable(&m_jdsp, 1);
        ArbitraryResponseEqualizerStringParser(&m_jdsp, str);
    } else {
        ArbitraryResponseEqualizerDisable(&m_jdsp);
    }
}

void DspManager::setLiveProgParam(const QString& name, double value) {
    QMutexLocker locker(&m_mutex);
    if(!m_initialized) return;
    
    if (!LiveProgSetVariable(&m_jdsp, name.toStdString().c_str(), static_cast<float>(value)))
        qWarning() << "[DSP] Rejected LiveProg variable update:" << name;
}

// Helper to get IR for visualization
bool DspManager::getImpulseResponse(std::vector<float>& out, int& channels) {
    QMutexLocker locker(&m_mutex);
    if (m_cachedIrData.empty()) return false;
    out = m_cachedIrData;
    channels = m_cachedIrChannels;
    return true;
}

void DspManager::setCompanderPoints(const std::vector<double>& gains) {
    QMutexLocker locker(&m_mutex);
    if(!m_initialized) return;

    if(gains.size() < 7) return;

    // Fixed frequencies matching both UI and Default Core
    double freqs[7] = { 95.0, 200.0, 400.0, 800.0, 1600.0, 3400.0, 7500.0 };
    double gainArr[7];
    
    for(int i=0; i<7; ++i) {
        gainArr[i] = gains[i];
    }

    // Call Core
    CompressorSetGain(&m_jdsp, freqs, gainArr, 1);
}
