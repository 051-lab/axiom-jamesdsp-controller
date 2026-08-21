#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

extern "C" {
#include "jdsp/jdsp_header.h"
}

namespace {

const char* kDefaultScriptPath =
    "C:\\Users\\soloa\\Documents\\EELVault\\dsp\\dragon\\dragon.eel";

int g_failures = 0;

void expect(bool condition, const char* message)
{
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++g_failures;
    }
}

struct Probe {
    double amplitude = 0.01;
    double freqHz = 1000.0;
};

double rmsDb(const std::vector<float>& samples)
{
    double sum = 0.0;
    for (float sample : samples) sum += (double)sample * sample;
    double rms = std::sqrt(sum / samples.size());
    return 20.0 * std::log10(rms + 1e-12);
}

double maxDiff(const std::vector<float>& a, const std::vector<float>& b)
{
    double max = 0.0;
    for (size_t i = 0; i < a.size(); ++i) {
        double d = std::fabs((double)a[i] - b[i]);
        if (d > max) max = d;
    }
    return max;
}

class Engine {
public:
    Engine()
    {
        JamesDSPGlobalMemoryAllocation();
        m_dsp = std::make_unique<JamesDSPLib>();
        std::memset(m_dsp.get(), 0, sizeof(JamesDSPLib));
        JamesDSPInit(m_dsp.get(), 8, 48000.0f);
        JLimiterSetEnabled(m_dsp.get(), 0);
        m_input.resize(kChunk * 2);
        m_output.resize(kChunk * 2);
    }

    ~Engine()
    {
        JamesDSPFree(m_dsp.get());
        JamesDSPGlobalMemoryDeallocation();
    }

    bool loadScript(const std::string& text)
    {
        char error[512] = {};
        int result = LiveProgStringParser(m_dsp.get(), const_cast<char*>(text.c_str()), error, sizeof(error));
        std::printf("LiveProgStringParser result: %d\n", result);
        if (result <= 0) {
            if (error[0] != '\0') std::fprintf(stderr, "Compiler detail: %s\n", error);
            return false;
        }
        LiveProgEnable(m_dsp.get());
        return true;
    }

    bool setParam(const char* name, float value)
    {
        int result = LiveProgSetVariable(m_dsp.get(), name, value);
        if (result != 1) {
            std::printf("  LiveProgSetVariable(%s, %g) rejected\n", name, value);
        }
        return result == 1;
    }

    double steadyStateRmsDb()
    {
        for (size_t c = 0; c < kChunks; ++c) {
            std::fill(m_output.begin(), m_output.end(), 0.0f);
            m_dsp->processFloatMultiplexd(m_dsp.get(), m_input.data(), m_output.data(), kChunk);
        }
        return rmsDb(m_output);
    }

    std::vector<float> steadyStateOutput()
    {
        for (size_t c = 0; c < kChunks; ++c) {
            std::fill(m_output.begin(), m_output.end(), 0.0f);
            m_dsp->processFloatMultiplexd(m_dsp.get(), m_input.data(), m_output.data(), kChunk);
        }
        return m_output;
    }

    void setProbe(const Probe& probe)
    {
        for (size_t i = 0; i < kChunk; ++i) {
            float sample = (float)(probe.amplitude * std::sin(2.0 * 3.14159265358979 * probe.freqHz * i / 48000.0));
            m_input[i * 2] = sample;
            m_input[i * 2 + 1] = sample;
        }
    }

    void setSilence()
    {
        std::fill(m_input.begin(), m_input.end(), 0.0f);
    }

    static constexpr size_t kChunk = 2048;
    static constexpr size_t kChunks = 24;

private:
    std::unique_ptr<JamesDSPLib> m_dsp;
    std::vector<float> m_input;
    std::vector<float> m_output;
};

struct ParamTest {
    const char* name;
    double low;
    double high;
    Probe probe;
    bool expectLowerAtHigh;
    double minimumAbsDeltaDb;
};

void runParamSweep(Engine& engine)
{
    const ParamTest tests[] = {
        {"drive", 0.0, 10.0, {0.02, 1000.0}, false, 3.0},
        {"bias", 0.0, 10.0, {0.30, 1000.0}, true, 0.15},
        {"comp", 0.0, 6.0, {0.40, 1000.0}, true, 1.0},
        {"bump", 0.0, 4.0, {0.05, 50.0}, false, 1.0},
        {"rolloff", 0.0, 9.0, {0.05, 10000.0}, true, 1.0},
    };

    for (const ParamTest& test : tests) {
        if (std::strcmp(test.name, "bias") == 0) continue;
        engine.setProbe(test.probe);
        expect(engine.setParam(test.name, (float)test.low), "controlsable param");
        double lowDb = engine.steadyStateRmsDb();
        expect(engine.setParam(test.name, (float)test.high), "controlsable param high");
        double highDb = engine.steadyStateRmsDb();
        double delta = highDb - lowDb;
        expect(engine.setParam(test.name, test.low), "restores param");
        bool passed = test.expectLowerAtHigh ? delta <= -test.minimumAbsDeltaDb : delta >= test.minimumAbsDeltaDb;
        std::printf("%-8s low %+.2f dB  high %+.2f dB  delta %+.2f dB  -> %s\n",
                    test.name, lowDb, highDb, delta, passed ? "OK" : "NO EFFECT");
        expect(passed, "audible response to parameter");
    }

    engine.setProbe({0.30, 1000.0});
    expect(engine.setParam("bias", 0.0f), "bias low");
    auto biasLow = engine.steadyStateOutput();
    expect(engine.setParam("bias", 10.0f), "bias high");
    auto biasHigh = engine.steadyStateOutput();
    double biasDiff = maxDiff(biasLow, biasHigh);
    bool biasPassed = biasDiff > 1e-3;
    std::printf("%-8s max sample diff %.6f (harmonic/asymmetry) -> %s\n", "bias", biasDiff,
                biasPassed ? "OK" : "NO EFFECT");
    expect(biasPassed, "audible response to bias");
    expect(engine.setParam("bias", 3.0f), "restores bias");

    engine.setProbe({0.1, 1000.0});
    expect(engine.setParam("wflutter", 0.0f), "wflutter low");
    auto wfLow = engine.steadyStateOutput();
    expect(engine.setParam("wflutter", 2.0f), "wflutter high");
    auto wfHigh = engine.steadyStateOutput();
    double wfDiff = maxDiff(wfLow, wfHigh);
    bool wfPassed = wfDiff > 1e-4;
    std::printf("%-8s max sample diff %.6f -> %s\n", "wflutter", wfDiff, wfPassed ? "OK" : "NO EFFECT");
    expect(wfPassed, "audible response to wflutter");
    expect(engine.setParam("wflutter", 1.0f), "restores wflutter");

    engine.setSilence();
    expect(engine.setParam("hiss", -82.0f), "hiss low");
    double hissLowDb = engine.steadyStateRmsDb();
    expect(engine.setParam("hiss", -42.0f), "hiss high");
    double hissHighDb = engine.steadyStateRmsDb();
    double hissDelta = hissHighDb - hissLowDb;
    bool hissPassed = hissDelta >= 15.0;
    std::printf("%-8s -82dB %+.2f dB  -42dB %+.2f dB  delta %+.2f dB -> %s\n",
                "hiss", hissLowDb, hissHighDb, hissDelta, hissPassed ? "OK" : "NO EFFECT");
    expect(hissPassed, "audible response to hiss");
    expect(engine.setParam("hiss", -82.0f), "restores hiss");
}

void runTrimTest(Engine& engine)
{
    engine.setProbe({0.01, 1000.0});
    double beforeDb = engine.steadyStateRmsDb();
    std::printf("%-8s trim=0 %+.2f dB\n", "trim", beforeDb);
    expect(engine.setParam("trim", -12.0f), "trim set");
    double afterDb = engine.steadyStateRmsDb();
    std::printf("%-8s trim=-12 %+.2f dB\n", "trim", afterDb);
    double delta = afterDb - beforeDb;
    bool passes = delta >= -12.75 && delta <= -11.25;
    expect(passes, "trim attenuation in range");
    std::printf("%-8s delta %+.2f dB -> %s\n", "trim", delta, passes ? "OK" : "NO EFFECT");
}

} // namespace

int main(int argc, char* argv[])
{
    const char* scriptPath = argc > 1 ? argv[1] : kDefaultScriptPath;

    std::ifstream file(scriptPath);
    if (!file.is_open()) {
        std::fprintf(stderr, "FAIL: cannot open script: %s\n", scriptPath);
        return 2;
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string script = buffer.str();
    std::printf("Script: %s (%zu bytes)\n", scriptPath, script.size());

    Engine engine;
    if (!engine.loadScript(script)) return 1;

    std::printf("\n--- trim (reference attenuation) ---\n");
    runTrimTest(engine);
    std::printf("\n--- parameter sweep ---\n");
    runParamSweep(engine);

    std::printf("\n");
    if (g_failures) {
        std::fprintf(stderr, "%d assertion(s) failed.\n", g_failures);
        return 1;
    }
    std::printf("Dragon parameter sweep passed: every control is audible.\n");
    return 0;
}