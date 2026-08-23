/**
 * JamesDSP Windows - Configuration System
 * 
 * Complete configuration structure matching Linux JamesDSP functionality.
 * Loads/saves settings from INI file.
 */

#pragma once

#include <string>
#include <array>
#include <fstream>
#include <sstream>
#include <map>
#include <algorithm>
#include <vector>
#include <windows.h>

extern "C" {
#include "jdsp/jdsp_header.h"
}

// EQ bands (15-band like Linux version)
constexpr int EQ_BANDS = 15;
constexpr double EQ_FREQUENCIES[EQ_BANDS] = {
    25.0, 40.0, 63.0, 100.0, 160.0, 250.0, 400.0, 630.0,
    1000.0, 1600.0, 2500.0, 4000.0, 6300.0, 10000.0, 16000.0
};

// Compressor bands (7 points)
constexpr int COMP_BANDS = 7;
constexpr double COMP_FREQUENCIES[COMP_BANDS] = {
    95.0, 200.0, 400.0, 800.0, 1600.0, 3400.0, 7500.0
};

// Reverb presets
enum ReverbPreset {
    REVERB_DEFAULT = 0,
    REVERB_SMALLHALL1, REVERB_SMALLHALL2,
    REVERB_MEDIUMHALL1, REVERB_MEDIUMHALL2,
    REVERB_LARGEHALL1, REVERB_LARGEHALL2,
    REVERB_SMALLROOM1, REVERB_SMALLROOM2,
    REVERB_MEDIUMROOM1, REVERB_MEDIUMROOM2,
    REVERB_LARGEROOM1, REVERB_LARGEROOM2
};

// Crossfeed modes
enum CrossfeedMode {
    CROSSFEED_BS2B_LEVEL1 = 0,
    CROSSFEED_BS2B_LEVEL2 = 1,
    CROSSFEED_HRTF_CROSSFEED = 2,
    CROSSFEED_HRTF_SURROUND1 = 3,
    CROSSFEED_HRTF_SURROUND2 = 4,
    CROSSFEED_HRTF_SURROUND3 = 5
};

// Complete DSP Configuration
struct DspConfig {
    // === Output Limiter (always on) ===
    double limiterThreshold = -0.1;  // dB
    double limiterRelease = 60.0;    // ms
    
    // === Bass Boost ===
    bool bassBoostEnabled = true;
    double bassBoostGain = 6.0;  // dB (0-15)
    
    // === Stereo Enhancement ===
    bool stereoWideEnabled = true;
    double stereoWideLevel = 40.0;  // percent (0-100)
    
    // === Reverb ===
    bool reverbEnabled = false;
    int reverbPreset = REVERB_DEFAULT;
    
    // === Vacuum Tube ===
    bool tubeEnabled = false;
    double tubeGain = 6.0;  // dB
    
    // === Dynamic Range Compressor ===
    bool compressorEnabled = false;
    double compressorGranularity = 1;      // 1-3
    double compressorTimeConstant = 0.56;  // 0.0-1.0
    int compressorTfResolution = 0;        // 0=low, 1=medium, 2=high
    std::array<double, COMP_BANDS> compressorGains = {0,0,0,0,0,0,0};  // dB per band
    
    // === Graphic EQ (15-band) ===
    bool eqEnabled = false;
    int eqMode = 0;  // 0=FIR (linear phase), 1=IIR (minimum phase)
    int eqInterpolation = 0;  // 0=linear, 1=cubic spline
    std::array<double, EQ_BANDS> eqGains = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};  // dB per band
    
    // === Crossfeed (BS2B / HRTF) ===
    bool crossfeedEnabled = false;
    int crossfeedMode = CROSSFEED_BS2B_LEVEL1;
    
    // === DDC (ViPER Driver Compatibility) ===
    bool ddcEnabled = false;
    std::string ddcFile;  // Path to .vdc file
    
    // === Convolver ===
    bool convolverEnabled = false;
    std::string convolverFile;  // Path to IR file (.wav, .flac)
    
    // === LiveProg (EEL Scripting) ===
    bool liveprogEnabled = false;
    std::string liveprogFile;  // Path to .eel script
    std::map<std::string, double> liveprogParams;

    // === Darwin filter package output ===
    bool darwinEnabled = false;
    std::string darwinImpulseFile;
    double darwinHarmonic = 0.0;
    bool darwinAutoHeadroom = true;
    
    // === Arbitrary Magnitude EQ ===
    bool arbMagEnabled = false;
    std::string arbMagResponse;  // Frequency response string
    
    // === Post Gain ===
    double postGain = 0.0;  // dB
};

// Simple INI parser/writer
class ConfigFile {
public:
    static bool load(const std::string& filename, DspConfig& config);
    static bool save(const std::string& filename, const DspConfig& config);
    static std::string getDefaultPath();
    
private:
    static std::string trim(const std::string& str);
    static bool parseBool(const std::string& value);
    static double parseDouble(const std::string& value);
    static int parseInt(const std::string& value);
    static std::array<double, EQ_BANDS> parseEqGains(const std::string& value);
    static std::array<double, COMP_BANDS> parseCompGains(const std::string& value);
};

// Apply configuration to JamesDSPLib
class DspController {
public:
    DspController(JamesDSPLib* dsp, int sampleRate, int blockSize);
    ~DspController();
    
    void applyConfig(const DspConfig& config, bool forceRefresh = false);
    void process(float* interleaved, size_t frames);
    void printStatus() const;
    
private:
    JamesDSPLib* m_dsp;
    int m_sampleRate;
    int m_blockSize;
    DspConfig m_currentConfig;
    JamesDSPLib* m_darwinDsp = nullptr;
    JamesDSPLib* m_retiringDarwinDsp = nullptr;
    size_t m_darwinCrossfadeFrames = 0;
    size_t m_darwinCrossfadePosition = 0;
    bool m_darwinOwnsOutput = false;
    bool m_darwinUpdatePending = false;
    std::vector<float> m_darwinInput;
    std::vector<float> m_darwinActiveOutput;
    std::vector<float> m_darwinRetiringOutput;
    
    // Cache for file-based effects
    std::string m_loadedDdcFile;
    std::string m_loadedConvolverFile;
    std::string m_loadedLiveprogFile;
    
    void applyLimiter(const DspConfig& config);
    void applyBassBoost(const DspConfig& config);
    void applyStereoWide(const DspConfig& config);
    void applyReverb(const DspConfig& config);
    void applyTube(const DspConfig& config);
    void applyCompressor(const DspConfig& config);
    void applyEqualizer(const DspConfig& config);
    void applyCrossfeed(const DspConfig& config);
    void applyDdc(const DspConfig& config);
    void applyConvolver(const DspConfig& config);
    void applyLiveprog(const DspConfig& config);
    bool applyDarwin(const DspConfig& config);
    void applyArbMag(const DspConfig& config);
    void restorePrimaryOutputStages();
    JamesDSPLib* buildDarwinEngine(const DspConfig& config);
    static void destroyEngine(JamesDSPLib*& dsp);
};
