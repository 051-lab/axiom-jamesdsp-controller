/**
 * JamesDSP Windows - Configuration Implementation
 */

#include "DspConfig.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <vector>

// dr_wav for WAV file loading (already implemented in libjamesdsp)
#include "Effects/eel2/dr_wav.h"

// ============== ConfigFile Implementation ==============

std::string ConfigFile::getDefaultPath() {
    char path[MAX_PATH];
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    std::string exePath(path);
    size_t lastSlash = exePath.find_last_of("\\/");
    return exePath.substr(0, lastSlash + 1) + "config.ini";
}

std::string ConfigFile::trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, last - first + 1);
}

bool ConfigFile::parseBool(const std::string& value) {
    std::string v = trim(value);
    std::transform(v.begin(), v.end(), v.begin(), ::tolower);
    return v == "true" || v == "1" || v == "yes" || v == "on";
}

double ConfigFile::parseDouble(const std::string& value) {
    try { return std::stod(value); }
    catch (...) { return 0.0; }
}

int ConfigFile::parseInt(const std::string& value) {
    try { return std::stoi(value); }
    catch (...) { return 0; }
}

std::array<double, EQ_BANDS> ConfigFile::parseEqGains(const std::string& value) {
    std::array<double, EQ_BANDS> gains = {0};
    std::stringstream ss(value);
    std::string item;
    int i = 0;
    while (std::getline(ss, item, ',') && i < EQ_BANDS) {
        gains[i++] = parseDouble(trim(item));
    }
    return gains;
}

std::array<double, COMP_BANDS> ConfigFile::parseCompGains(const std::string& value) {
    std::array<double, COMP_BANDS> gains = {0};
    std::stringstream ss(value);
    std::string item;
    int i = 0;
    while (std::getline(ss, item, ',') && i < COMP_BANDS) {
        gains[i++] = parseDouble(trim(item));
    }
    return gains;
}

bool ConfigFile::load(const std::string& filename, DspConfig& config) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cout << "[CONFIG] No config file found, using defaults" << std::endl;
        return false;
    }
    config.liveprogParams.clear();
    
    std::string line, currentSection;
    while (std::getline(file, line)) {
        line = trim(line);
        if (line.empty() || line[0] == ';' || line[0] == '#') continue;
        
        // Section header
        if (line[0] == '[' && line.back() == ']') {
            currentSection = line.substr(1, line.size() - 2);
            std::transform(currentSection.begin(), currentSection.end(), 
                          currentSection.begin(), ::tolower);
            continue;
        }
        
        // Key=Value
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        
        std::string key = trim(line.substr(0, eq));
        std::string value = trim(line.substr(eq + 1));
        std::transform(key.begin(), key.end(), key.begin(), ::tolower);
        
        // Parse by section
        if (currentSection == "bassboost") {
            if (key == "enabled") config.bassBoostEnabled = parseBool(value);
            else if (key == "gain") config.bassBoostGain = parseDouble(value);
        }
        else if (currentSection == "stereowide") {
            if (key == "enabled") config.stereoWideEnabled = parseBool(value);
            else if (key == "level") config.stereoWideLevel = parseDouble(value);
        }
        else if (currentSection == "reverb") {
            if (key == "enabled") config.reverbEnabled = parseBool(value);
            else if (key == "preset") config.reverbPreset = parseInt(value);
        }
        else if (currentSection == "tube" || currentSection == "vacuumtube") {
            if (key == "enabled") config.tubeEnabled = parseBool(value);
            else if (key == "gain") config.tubeGain = parseDouble(value);
        }
        else if (currentSection == "compressor") {
            if (key == "enabled") config.compressorEnabled = parseBool(value);
            else if (key == "granularity") config.compressorGranularity = parseDouble(value);
            else if (key == "timeconstant") config.compressorTimeConstant = parseDouble(value);
            else if (key == "resolution") config.compressorTfResolution = parseInt(value);
            else if (key == "gains") config.compressorGains = parseCompGains(value);
        }
        else if (currentSection == "equalizer" || currentSection == "eq") {
            if (key == "enabled") config.eqEnabled = parseBool(value);
            else if (key == "mode") config.eqMode = parseInt(value);
            else if (key == "interpolation") config.eqInterpolation = parseInt(value);
            else if (key == "gains") config.eqGains = parseEqGains(value);
        }
        else if (currentSection == "crossfeed") {
            if (key == "enabled") config.crossfeedEnabled = parseBool(value);
            else if (key == "mode") config.crossfeedMode = parseInt(value);
        }
        else if (currentSection == "ddc") {
            if (key == "enabled") config.ddcEnabled = parseBool(value);
            else if (key == "file") config.ddcFile = value;
        }
        else if (currentSection == "convolver") {
            if (key == "enabled") config.convolverEnabled = parseBool(value);
            else if (key == "file") config.convolverFile = value;
        }
        else if (currentSection == "liveprog") {
            if (key == "enabled") config.liveprogEnabled = parseBool(value);
            else if (key == "file") config.liveprogFile = value;
            else if (key.rfind("param.", 0) == 0 && key.size() > 6) {
                config.liveprogParams[key.substr(6)] = parseDouble(value);
            }
        }
        else if (currentSection == "general") {
            if (key == "postgain") config.postGain = parseDouble(value);
        }
    }
    
    std::cout << "[CONFIG] Loaded from: " << filename << std::endl;
    return true;
}

bool ConfigFile::save(const std::string& filename, const DspConfig& config) {
    std::ofstream file(filename);
    if (!file.is_open()) return false;
    
    file << "; JamesDSP Windows Configuration\n";
    file << "; Generated automatically\n\n";
    
    file << "[General]\n";
    file << "postGain = " << config.postGain << "\n\n";
    
    file << "[BassBoost]\n";
    file << "enabled = " << (config.bassBoostEnabled ? "true" : "false") << "\n";
    file << "gain = " << config.bassBoostGain << "\n\n";
    
    file << "[StereoWide]\n";
    file << "enabled = " << (config.stereoWideEnabled ? "true" : "false") << "\n";
    file << "level = " << config.stereoWideLevel << "\n\n";
    
    file << "[Reverb]\n";
    file << "enabled = " << (config.reverbEnabled ? "true" : "false") << "\n";
    file << "preset = " << config.reverbPreset << "\n\n";
    
    file << "[Tube]\n";
    file << "enabled = " << (config.tubeEnabled ? "true" : "false") << "\n";
    file << "gain = " << config.tubeGain << "\n\n";
    
    file << "[Compressor]\n";
    file << "enabled = " << (config.compressorEnabled ? "true" : "false") << "\n";
    file << "granularity = " << config.compressorGranularity << "\n";
    file << "timeConstant = " << config.compressorTimeConstant << "\n";
    file << "resolution = " << config.compressorTfResolution << "\n";
    file << "gains = ";
    for (int i = 0; i < COMP_BANDS; i++) {
        if (i > 0) file << ",";
        file << config.compressorGains[i];
    }
    file << "\n\n";
    
    file << "[Equalizer]\n";
    file << "enabled = " << (config.eqEnabled ? "true" : "false") << "\n";
    file << "mode = " << config.eqMode << "\n";
    file << "interpolation = " << config.eqInterpolation << "\n";
    file << "gains = ";
    for (int i = 0; i < EQ_BANDS; i++) {
        if (i > 0) file << ",";
        file << config.eqGains[i];
    }
    file << "\n\n";
    
    file << "[Crossfeed]\n";
    file << "enabled = " << (config.crossfeedEnabled ? "true" : "false") << "\n";
    file << "mode = " << config.crossfeedMode << "\n\n";
    
    file << "[DDC]\n";
    file << "enabled = " << (config.ddcEnabled ? "true" : "false") << "\n";
    file << "file = " << config.ddcFile << "\n\n";
    
    file << "[Convolver]\n";
    file << "enabled = " << (config.convolverEnabled ? "true" : "false") << "\n";
    file << "file = " << config.convolverFile << "\n\n";
    
    file << "[LiveProg]\n";
    file << "enabled = " << (config.liveprogEnabled ? "true" : "false") << "\n";
    file << "file = " << config.liveprogFile << "\n";
    for (const auto& param : config.liveprogParams) {
        file << "param." << param.first << " = " << param.second << "\n";
    }
    
    return true;
}

// ============== DspController Implementation ==============

DspController::DspController(JamesDSPLib* dsp, int sampleRate)
    : m_dsp(dsp), m_sampleRate(sampleRate) {
}

DspController::~DspController() {
}

void DspController::applyLimiter(const DspConfig& config) {
    JLimiterSetCoefficients(m_dsp, config.limiterThreshold, config.limiterRelease);
}

void DspController::applyBassBoost(const DspConfig& config) {
    if (config.bassBoostEnabled) {
        BassBoostEnable(m_dsp);
        BassBoostSetParam(m_dsp, static_cast<float>(config.bassBoostGain));
    } else {
        BassBoostDisable(m_dsp);
    }
}

void DspController::applyStereoWide(const DspConfig& config) {
    if (config.stereoWideEnabled) {
        StereoEnhancementEnable(m_dsp);
        StereoEnhancementSetParam(m_dsp, static_cast<float>(config.stereoWideLevel / 100.0));
    } else {
        StereoEnhancementDisable(m_dsp);
    }
}

void DspController::applyReverb(const DspConfig& config) {
    if (config.reverbEnabled) {
        ReverbEnable(m_dsp);
        Reverb_SetParam(m_dsp, config.reverbPreset);
    } else {
        ReverbDisable(m_dsp);
    }
}

void DspController::applyTube(const DspConfig& config) {
    if (config.tubeEnabled) {
        VacuumTubeEnable(m_dsp);
        VacuumTubeSetGain(m_dsp, config.tubeGain);
    } else {
        VacuumTubeDisable(m_dsp);
    }
}

void DspController::applyCompressor(const DspConfig& config) {
    if (config.compressorEnabled) {
        CompressorEnable(m_dsp, 1);
        CompressorSetParam(m_dsp, 
            static_cast<float>(config.compressorTimeConstant),
            static_cast<int>(config.compressorGranularity),
            config.compressorTfResolution,
            0);
        
        // Set frequency/gain points
        double freqs[COMP_BANDS+2];
        double gains[COMP_BANDS+2];
        freqs[0] = 0.0;
        gains[0] = config.compressorGains[0];
        for (int i = 0; i < COMP_BANDS; i++) {
            freqs[i+1] = COMP_FREQUENCIES[i];
            gains[i+1] = config.compressorGains[i];
        }
        freqs[COMP_BANDS+1] = 24000.0;
        gains[COMP_BANDS+1] = config.compressorGains[COMP_BANDS-1];
        
        CompressorSetGain(m_dsp, freqs, gains, 1);
    } else {
        CompressorDisable(m_dsp);
    }
}

void DspController::applyEqualizer(const DspConfig& config) {
    if (config.eqEnabled) {
        MultimodalEqualizerEnable(m_dsp, 1);
        
        // Set frequency/gain points
        double freqs[EQ_BANDS+2];
        double gains[EQ_BANDS+2];
        freqs[0] = 0.0;
        gains[0] = config.eqGains[0];
        for (int i = 0; i < EQ_BANDS; i++) {
            freqs[i+1] = EQ_FREQUENCIES[i];
            gains[i+1] = config.eqGains[i];
        }
        freqs[EQ_BANDS+1] = 24000.0;
        gains[EQ_BANDS+1] = config.eqGains[EQ_BANDS-1];
        
        MultimodalEqualizerAxisInterpolation(m_dsp, 
            config.eqInterpolation, 
            config.eqMode, 
            freqs, gains);
    } else {
        MultimodalEqualizerDisable(m_dsp);
    }
}

void DspController::applyCrossfeed(const DspConfig& config) {
    if (config.crossfeedEnabled) {
        CrossfeedEnable(m_dsp, 1);
        CrossfeedChangeMode(m_dsp, config.crossfeedMode);
    } else {
        CrossfeedDisable(m_dsp);
    }
}

void DspController::applyDdc(const DspConfig& config) {
    if (config.ddcEnabled && !config.ddcFile.empty()) {
        // Only reload if file changed
        if (m_loadedDdcFile != config.ddcFile) {
            std::ifstream file(config.ddcFile);
            if (file.is_open()) {
                std::stringstream buffer;
                buffer << file.rdbuf();
                std::string content = buffer.str();
                
                int result = DDCStringParser(m_dsp, const_cast<char*>(content.c_str()));
                if (result > 0) {
                    DDCEnable(m_dsp, 1);
                    m_loadedDdcFile = config.ddcFile;
                    std::cout << "[DDC] Loaded: " << config.ddcFile << std::endl;
                } else {
                    std::cerr << "[DDC] Failed to parse: " << config.ddcFile << std::endl;
                }
            } else {
                std::cerr << "[DDC] File not found: " << config.ddcFile << std::endl;
            }
        }
    } else {
        DDCDisable(m_dsp);
        m_loadedDdcFile.clear();
    }
}

void DspController::applyConvolver(const DspConfig& config) {
    if (config.convolverEnabled && !config.convolverFile.empty()) {
        // Only reload if file changed
        if (m_loadedConvolverFile != config.convolverFile) {
            std::cout << "[CONVOLVER] Loading: " << config.convolverFile << std::endl;
            
            // Load WAV file using dr_wav
            drwav wav;
            if (!drwav_init_file(&wav, config.convolverFile.c_str(), nullptr)) {
                std::cerr << "[CONVOLVER] Failed to open file" << std::endl;
                return;
            }
            
            std::cout << "[CONVOLVER] Format: " << wav.sampleRate << " Hz, " 
                      << wav.channels << " ch, " << wav.totalPCMFrameCount << " frames" << std::endl;
            
            // Read as float
            size_t totalSamples = wav.totalPCMFrameCount * wav.channels;
            std::vector<float> samples(totalSamples);
            drwav_uint64 samplesRead = drwav_read_pcm_frames_f32(&wav, wav.totalPCMFrameCount, samples.data());
            drwav_uninit(&wav);
            
            if (samplesRead == 0) {
                std::cerr << "[CONVOLVER] Failed to read samples" << std::endl;
                return;
            }
            
            // Pass to convolver (channels, length)
            int result = Convolver1DLoadImpulseResponse(
                m_dsp,
                samples.data(),
                wav.channels,
                static_cast<size_t>(samplesRead),
                0  // updateOld = false
            );
            
            if (result >= 0) {
                Convolver1DEnable(m_dsp);
                m_loadedConvolverFile = config.convolverFile;
                std::cout << "[CONVOLVER] Loaded successfully (" << samplesRead << " frames)" << std::endl;
            } else {
                std::cerr << "[CONVOLVER] Failed to load IR (error " << result << ")" << std::endl;
            }
        }
    } else {
        Convolver1DDisable(m_dsp);
        m_loadedConvolverFile.clear();
    }
}

void DspController::applyLiveprog(const DspConfig& config) {
    if (config.liveprogEnabled && !config.liveprogFile.empty()) {
        // Only reload if file changed
        if (m_loadedLiveprogFile != config.liveprogFile) {
            std::ifstream file(config.liveprogFile);
            if (file.is_open()) {
                std::stringstream buffer;
                buffer << file.rdbuf();
                std::string content = buffer.str();
                
                char errorBuffer[1024] = {0};
                int result = LiveProgStringParser(
                    m_dsp,
                    const_cast<char*>(content.c_str()),
                    errorBuffer,
                    sizeof(errorBuffer));
                if (result > 0) {
                    LiveProgEnable(m_dsp);
                    m_loadedLiveprogFile = config.liveprogFile;
                    std::cout << "[LIVEPROG] Loaded: " << config.liveprogFile
                              << " (" << checkErrorCode(result) << ")" << std::endl;
                } else {
                    std::cerr << "[LIVEPROG] Compilation error (" << result << "): " 
                              << checkErrorCode(result) << std::endl;
                    if (errorBuffer[0] != '\0') {
                        std::cerr << "[LIVEPROG] Compiler detail: " << errorBuffer << std::endl;
                    }
                    std::cerr << "[LIVEPROG] Previous working script remains active." << std::endl;
                }
            } else {
                std::cerr << "[LIVEPROG] File not found: " << config.liveprogFile << std::endl;
            }
        }
        if (m_loadedLiveprogFile == config.liveprogFile) {
            for (const auto& param : config.liveprogParams) {
                if (!LiveProgSetVariable(m_dsp, param.first.c_str(), static_cast<float>(param.second))) {
                    std::cerr << "[LIVEPROG] Rejected variable update: " << param.first << std::endl;
                }
            }
        }
    } else {
        LiveProgDisable(m_dsp);
        m_loadedLiveprogFile.clear();
    }
}

void DspController::applyArbMag(const DspConfig& config) {
    if (config.arbMagEnabled && !config.arbMagResponse.empty()) {
        ArbitraryResponseEqualizerStringParser(m_dsp, 
            const_cast<char*>(config.arbMagResponse.c_str()));
        ArbitraryResponseEqualizerEnable(m_dsp, 1);
    } else {
        ArbitraryResponseEqualizerDisable(m_dsp);
    }
}

void DspController::applyConfig(const DspConfig& config, bool forceRefresh) {
    if (forceRefresh) {
        m_loadedLiveprogFile.clear();
        m_loadedDdcFile.clear();
        m_loadedConvolverFile.clear();
    }

    if (forceRefresh ||
        config.limiterThreshold != m_currentConfig.limiterThreshold ||
        config.limiterRelease != m_currentConfig.limiterRelease) {
        applyLimiter(config);
    }
    if (forceRefresh ||
        config.bassBoostEnabled != m_currentConfig.bassBoostEnabled ||
        config.bassBoostGain != m_currentConfig.bassBoostGain) {
        applyBassBoost(config);
    }
    if (forceRefresh ||
        config.stereoWideEnabled != m_currentConfig.stereoWideEnabled ||
        config.stereoWideLevel != m_currentConfig.stereoWideLevel) {
        applyStereoWide(config);
    }
    if (forceRefresh ||
        config.reverbEnabled != m_currentConfig.reverbEnabled ||
        config.reverbPreset != m_currentConfig.reverbPreset) {
        applyReverb(config);
    }
    if (forceRefresh ||
        config.tubeEnabled != m_currentConfig.tubeEnabled ||
        config.tubeGain != m_currentConfig.tubeGain) {
        applyTube(config);
    }
    if (forceRefresh ||
        config.compressorEnabled != m_currentConfig.compressorEnabled ||
        config.compressorGranularity != m_currentConfig.compressorGranularity ||
        config.compressorTimeConstant != m_currentConfig.compressorTimeConstant ||
        config.compressorTfResolution != m_currentConfig.compressorTfResolution ||
        config.compressorGains != m_currentConfig.compressorGains) {
        applyCompressor(config);
    }
    if (forceRefresh ||
        config.eqEnabled != m_currentConfig.eqEnabled ||
        config.eqMode != m_currentConfig.eqMode ||
        config.eqInterpolation != m_currentConfig.eqInterpolation ||
        config.eqGains != m_currentConfig.eqGains) {
        applyEqualizer(config);
    }
    if (forceRefresh ||
        config.crossfeedEnabled != m_currentConfig.crossfeedEnabled ||
        config.crossfeedMode != m_currentConfig.crossfeedMode) {
        applyCrossfeed(config);
    }
    if (forceRefresh ||
        config.ddcEnabled != m_currentConfig.ddcEnabled ||
        config.ddcFile != m_currentConfig.ddcFile) {
        applyDdc(config);
    }
    if (forceRefresh ||
        config.convolverEnabled != m_currentConfig.convolverEnabled ||
        config.convolverFile != m_currentConfig.convolverFile) {
        applyConvolver(config);
    }
    if (forceRefresh ||
        config.liveprogEnabled != m_currentConfig.liveprogEnabled ||
        config.liveprogFile != m_currentConfig.liveprogFile) {
        applyLiveprog(config);
    } else if (config.liveprogEnabled &&
               config.liveprogParams != m_currentConfig.liveprogParams) {
        for (const auto& param : config.liveprogParams) {
            if (!LiveProgSetVariable(m_dsp, param.first.c_str(), static_cast<float>(param.second))) {
                std::cerr << "[LIVEPROG] Rejected variable update: " << param.first << std::endl;
            }
        }
    }
    if (forceRefresh ||
        config.arbMagEnabled != m_currentConfig.arbMagEnabled ||
        config.arbMagResponse != m_currentConfig.arbMagResponse) {
        applyArbMag(config);
    }
    if (forceRefresh || config.postGain != m_currentConfig.postGain) {
        JamesDSPSetPostGain(m_dsp, config.postGain);
    }
    
    m_currentConfig = config;
}

void DspController::printStatus() const {
    std::cout << "\n=== DSP Status ===" << std::endl;
    std::cout << "Bass Boost:    " << (m_currentConfig.bassBoostEnabled ? "ON" : "OFF") 
              << " (" << m_currentConfig.bassBoostGain << " dB)" << std::endl;
    std::cout << "Stereo Wide:   " << (m_currentConfig.stereoWideEnabled ? "ON" : "OFF")
              << " (" << m_currentConfig.stereoWideLevel << "%)" << std::endl;
    std::cout << "Reverb:        " << (m_currentConfig.reverbEnabled ? "ON" : "OFF")
              << " (preset " << m_currentConfig.reverbPreset << ")" << std::endl;
    std::cout << "Tube:          " << (m_currentConfig.tubeEnabled ? "ON" : "OFF")
              << " (" << m_currentConfig.tubeGain << " dB)" << std::endl;
    std::cout << "Compressor:    " << (m_currentConfig.compressorEnabled ? "ON" : "OFF") << std::endl;
    std::cout << "Equalizer:     " << (m_currentConfig.eqEnabled ? "ON" : "OFF") << std::endl;
    std::cout << "Crossfeed:     " << (m_currentConfig.crossfeedEnabled ? "ON" : "OFF")
              << " (mode " << m_currentConfig.crossfeedMode << ")" << std::endl;
    std::cout << "DDC:           " << (m_currentConfig.ddcEnabled ? "ON" : "OFF") << std::endl;
    std::cout << "Convolver:     " << (m_currentConfig.convolverEnabled ? "ON" : "OFF") << std::endl;
    std::cout << "LiveProg:      " << (m_currentConfig.liveprogEnabled ? "ON" : "OFF") << std::endl;
    std::cout << "Post Gain:     " << m_currentConfig.postGain << " dB" << std::endl;
    std::cout << "==================\n" << std::endl;
}
