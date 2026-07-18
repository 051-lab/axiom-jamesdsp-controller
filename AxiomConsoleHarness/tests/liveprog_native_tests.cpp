#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

extern "C" {
#include "jdsp/jdsp_header.h"
}

namespace {

int failures = 0;

void expect(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

void expectNear(float actual, float expected, const char* message)
{
    if (std::fabs(actual - expected) > 0.00001f) {
        std::cerr << "FAIL: " << message << " (expected " << expected
                  << ", got " << actual << ")\n";
        ++failures;
    }
}

int loadScript(JamesDSPLib& dsp, const std::string& script, char* error, size_t errorSize)
{
    return LiveProgStringParser(&dsp, const_cast<char*>(script.c_str()), error, errorSize);
}

std::vector<float> process(JamesDSPLib& dsp, size_t frames, float value)
{
    std::vector<float> input(frames * 2, value);
    std::vector<float> output(frames * 2, 0.0f);
    dsp.processFloatMultiplexd(&dsp, input.data(), output.data(), frames);
    return output;
}

void expectStereo(const std::vector<float>& output, float left, float right, const char* message)
{
    for (size_t frame = 0; frame < output.size(); frame += 2) {
        expectNear(output[frame], left, message);
        expectNear(output[frame + 1], right, message);
    }
}

void testLiveProgLifecycle(JamesDSPLib& dsp)
{
    const std::string script =
        "@init\n"
        "block_count = 0; slider_count = 0; slider1 = 0.5;\n"
        "@slider\n"
        "slider_count += 1; gain = slider1 + slider_count * 0.001;\n"
        "@block\n"
        "block_count += 1; block_size = samplesblock;\n"
        "@sample\n"
        "spl0 = spl0 * gain + block_count * 0.01;\n"
        "spl1 = spl1 * gain + block_size * 0.000001;\n";

    char error[512] = {};
    expect(loadScript(dsp, script, error, sizeof(error)) == 1,
           "@slider/@block script compiles");
    LiveProgEnable(&dsp);
    expectStereo(process(dsp, 8, 0.2f), 0.1102f, 0.100208f,
                 "first block executes slider once and block once");
    expectStereo(process(dsp, 8, 0.2f), 0.1202f, 0.100208f,
                 "second block executes @block once");

    expect(LiveProgSetVariable(&dsp, "slider1", 0.25f) == 1,
           "known finite variable update succeeds");
    expectStereo(process(dsp, 8, 0.2f), 0.0804f, 0.050408f,
                 "variable update reruns @slider");
    expect(LiveProgSetVariable(&dsp, "unknown_slider", 1.0f) == 0,
           "unknown variable update is rejected");
    expect(LiveProgSetVariable(&dsp, "bad.name", 1.0f) == 0,
           "invalid variable name is rejected");
    expect(LiveProgSetVariable(&dsp, "slider1", std::numeric_limits<float>::infinity()) == 0,
           "non-finite variable value is rejected");
}

void testTransactionalReplacement(JamesDSPLib& dsp)
{
    char error[512] = {};
    expect(loadScript(dsp, "@sample\nspl0 = 0.25; spl1 = 0.25;", error, sizeof(error)) == 1,
           "first replacement script compiles");
    LiveProgEnable(&dsp);
    expectStereo(process(dsp, 4, 0.0f), 0.25f, 0.25f,
                 "first replacement script is active");

    error[0] = '\0';
    expect(loadScript(dsp, "@sample\nspl0 = ; spl1 = 0;", error, sizeof(error)) == -3,
           "invalid replacement reports @sample failure");
    expect(error[0] != '\0', "invalid replacement returns compiler detail");
    expectStereo(process(dsp, 4, 0.0f), 0.25f, 0.25f,
                 "invalid replacement preserves previous script");
}

void testVariableBlockCapacity(JamesDSPLib& dsp)
{
    char error[128] = {};
    expect(loadScript(dsp,
        "@block\nblock_size = samplesblock;\n@sample\nspl0 = block_size; spl1 = spl0;",
        error, sizeof(error)) == 1, "block-size script compiles");
    LiveProgEnable(&dsp);
    expectStereo(process(dsp, 16, 0.0f), 16.0f, 16.0f,
                 "larger variable block processes at actual size");
    expect(dsp.blockSizeMax >= 16, "larger variable block expands native capacity");
    expectStereo(process(dsp, 3, 0.0f), 3.0f, 3.0f,
                 "smaller variable block reports actual samplesblock");
}

void testConvolverValidation(JamesDSPLib& dsp)
{
    float impulse[2] = {1.0f, 0.0f};
    float nonFinite[1] = {std::numeric_limits<float>::quiet_NaN()};
    expect(Convolver1DLoadImpulseResponse(&dsp, nullptr, 1, 1, 0) == -2,
           "null convolver impulse is rejected");
    expect(Convolver1DLoadImpulseResponse(&dsp, impulse, 3, 1, 0) == -2,
           "unsupported convolver channel count is rejected");
    expect(Convolver1DLoadImpulseResponse(&dsp, nonFinite, 1, 1, 0) == -2,
           "non-finite convolver impulse is rejected");
}

} // namespace

int main()
{
    JamesDSPGlobalMemoryAllocation();
    JamesDSPLib dsp = {};
    JamesDSPInit(&dsp, 8, 48000.0f);
    JLimiterSetEnabled(&dsp, 0);

    testLiveProgLifecycle(dsp);
    testTransactionalReplacement(dsp);
    testVariableBlockCapacity(dsp);
    testConvolverValidation(dsp);

    JamesDSPFree(&dsp);
    JamesDSPGlobalMemoryDeallocation();

    if (failures) {
        std::cerr << failures << " native test assertion(s) failed.\n";
        return EXIT_FAILURE;
    }
    std::cout << "All native LiveProg stability tests passed.\n";
    return EXIT_SUCCESS;
}
