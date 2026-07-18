#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <float.h>
#include "../jdsp_header.h"
void MultimodalEqualizerConstructor(JamesDSPLib *jdsp)
{
	double freqAx[NUMPTS] = { 25.0, 40.0, 63.0, 100.0, 160.0, 250.0, 400.0, 630.0, 1000.0, 1600.0, 2500.0, 4000.0, 6300.0, 10000.0, 16000.0 };
	memcpy(jdsp->mEQ.freq + 1, freqAx, NUMPTS * sizeof(double));
	jdsp->mEQ.freq[NUMPTS + 1] = 24000.0;
	memset(jdsp->mEQ.z1_AL, 0, sizeof(jdsp->mEQ.z1_AL));
	memset(jdsp->mEQ.z2_AL, 0, sizeof(jdsp->mEQ.z2_AL));
	memset(jdsp->mEQ.z1_AR, 0, sizeof(jdsp->mEQ.z1_AR));
	memset(jdsp->mEQ.z2_AR, 0, sizeof(jdsp->mEQ.z2_AR));
	initIerper(&jdsp->mEQ.pch1, NUMPTS + 2);
	initIerper(&jdsp->mEQ.pch2, NUMPTS + 2);
	jdsp->mEQ.instance.filterLen = InitArbitraryEq(&jdsp->mEQ.instance.coeffGen, 0);
	FFTConvolver2x2Init(&jdsp->mEQ.instance.convState);
	FFTConvolver2x2Init(&jdsp->mEQ.conv);
	float *kDelta = (float*)malloc(jdsp->mEQ.instance.filterLen * sizeof(float));
	memset(kDelta, 0, jdsp->mEQ.instance.filterLen * sizeof(float));
	kDelta[0] = 1.0f;
	FFTConvolver2x2LoadImpulseResponse(&jdsp->mEQ.instance.convState, (unsigned int)jdsp->blockSize, kDelta, kDelta, jdsp->mEQ.instance.filterLen);
	FFTConvolver2x2LoadImpulseResponse(&jdsp->mEQ.conv, (unsigned int)jdsp->blockSize, kDelta, kDelta, jdsp->mEQ.instance.filterLen);
	free(kDelta);
}
void MultimodalEqualizerDestructor(JamesDSPLib *jdsp)
{
	freeIerper(&jdsp->mEQ.pch1);
	freeIerper(&jdsp->mEQ.pch2);
	EqNodesFree(&jdsp->mEQ.instance.coeffGen);
	FFTConvolver2x2Free(&jdsp->mEQ.instance.convState);
	FFTConvolver2x2Free(&jdsp->mEQ.conv);
}
void HSHOResponse(double fs, double fc, unsigned int filterOrder, double gain, double overallGainDb, unsigned int queryPts, double *dispFreq, double *cplxRe, double *cplxIm)
{
	unsigned int L = filterOrder >> 1;
	double *sRe = (double *)malloc(queryPts * sizeof(double));
	double *sIm = (double *)malloc(queryPts * sizeof(double));
	double *s2Re = (double *)malloc(queryPts * sizeof(double));
	double *s2Im = (double *)malloc(queryPts * sizeof(double));
	for (unsigned int j = 0; j < queryPts; j++)
	{
		double digw = dispFreq[j] / fs * 2.0 * M_PI;
		sRe[j] = cos(digw);
		sIm[j] = sin(digw);
		s2Re[j] = sRe[j] * sRe[j] - sIm[j] * sIm[j];
		s2Im[j] = sRe[j] * sIm[j] + sIm[j] * sRe[j];
	}
	double Dw = M_PI * (0.5 - fc / fs);
	double GB = (1.0 / sqrt(2.0)) * gain;
	double G = pow(10.0, gain / 20.0);
	double overallGain = pow(10.0, overallGainDb / 20.0);
	if (G == 1.0)
		goto scalar_gain;
	GB = pow(10.0, GB / 20.0);
	double gR;
	if (fabs(GB * GB - 1) < DBL_EPSILON)
		gR = 0.0;
	else
		gR = (G * G - GB * GB) / (GB * GB - 1);
	double reci1Ord = 1.0 / filterOrder;
	double ratOrd = pow(gR, reci1Ord);
	double sDw = sin(Dw);
	double sDw2 = sDw * sDw;
	double cDw = cos(Dw);
	double cDw2 = cDw * cDw;
	double ratRO = pow(gR, 1.0 / (2 * filterOrder));
	double g2reO = pow(G, 2.0 * reci1Ord);
	double gP1 = pow(G, 1.0 / filterOrder);
	double t2 = 2 * g2reO * sDw2;
	double t4 = cDw2 * ratOrd;
	double t5 = 2 * cDw2 * ratOrd;
	double t8 = cDw2 * ratOrd;
	double t10 = g2reO * sDw2;
	double t11 = 2 * sDw2;
	double t12 = 2 * t8;
	double t13 = 2 * gP1 * cDw * sDw * ratRO;
	double t14 = g2reO * sDw2 + cDw2 * ratOrd;
	double t15 = 2 * cDw * sDw * ratRO;
	double t16 = sDw2 + cDw2 * ratOrd;
	for (unsigned int i = 0; i < L; i++)
	{
		double si = sin((2 * (i + 1) - 1.0) * M_PI / (2.0 * filterOrder));
		double t6 = t13 * si;
		double t1 = t14 - t6;
		double t9 = t15 * si;
		double t7 = t16 - t9;
		for (unsigned int j = 0; j < queryPts; j++)
		{
			double k1Re = (t1 - t2 * sRe[j] + t5 * sRe[j] + t10 * s2Re[j] + t4 * s2Re[j] + t6 * s2Re[j]);
			double k1Im = (-t2 * sIm[j] + t5 * sIm[j] + t10 * s2Im[j] + t4 * s2Im[j] + t6 * s2Im[j]);
			double k2Re = (t7 + s2Re[j] * sDw2 - t11 * sRe[j] + s2Re[j] * t8 + sRe[j] * t12 + s2Re[j] * t9);
			double k2Im = (s2Im[j] * sDw2 - t11 * sIm[j] + s2Im[j] * t8 + sIm[j] * t12 + s2Im[j] * t9);
			double cplx2Re, cplx2Im;
			cdivid(k1Re, k1Im, k2Re, k2Im, &cplx2Re, &cplx2Im);
			complexMultiplication(cplxRe[j], cplxIm[j], cplx2Re, cplx2Im, &cplxRe[j], &cplxIm[j]);
		}
	}
scalar_gain:
	free(sRe);
	free(sIm);
	free(s2Re);
	free(s2Im);
	for (unsigned int j = 0; j < queryPts; j++)
	{
		cplxRe[j] *= overallGain;
		cplxIm[j] *= overallGain;
	}
}
unsigned int HSHOSVF(double fs, double fc, unsigned int filterOrder, double gain, double overallGainDb, float *c1, float *c2, float *d0, float *d1, float *overallGain)
{
	*overallGain = (float)pow(10.0, overallGainDb / 20.0);
	if (fabs(gain) < 8.0 * DBL_EPSILON)
		return 0;
	unsigned int L = filterOrder >> 1;
	double Dw = M_PI * (fc / fs - 0.5);
	double GB = pow(10.0, ((1.0 / sqrt(2.0)) * gain / 20.0));
	double G = pow(10.0, gain / 20.0);
	double gR = (G * G - GB * GB) / (GB * GB - 1);
	double reci1Ord = 1.0 / filterOrder;
	double ratOrd = pow(gR, reci1Ord);
	double ntD = tan(Dw);
	double ntD2 = ntD * ntD;
	double stD = sin(Dw);
	double ctD = cos(Dw);
	double ratRO = pow(gR, 1.0 / (2 * filterOrder));
	double gP1 = pow(G, 1.0 / filterOrder);
	double gP2 = pow(G, 2.0 / filterOrder);
	for (unsigned int i = 0; i < L; i++)
	{
		double si = sin((2 * (i + 1) - 1.0) * M_PI / (2.0 * filterOrder));
		c1[i] = (float)(2.0 - 2.0 * (ntD2 - ratOrd) / (ntD2 + ratOrd - 2.0 * ratRO * ntD * si));
		c2[i] = (float)((ratRO * ctD) / (ratRO * ctD - si * stD));
		d0[i] = (float)((ratOrd + gP2 * ntD2 - 2.0 * gP1 * ratRO * ntD * si) / (ntD2 + ratOrd - 2.0 * ratRO * ntD * si));
		d1[i] = (float)((ratRO * ctD - gP1 * si * stD) / (ratRO * ctD - si * stD));
	}
	return L;
}
double reverseProjectX(double pos, double MIN_FREQ, double MAX_FREQ)
{
	double minPos = log(MIN_FREQ);
	double maxPos = log(MAX_FREQ);
	return exp(pos * (maxPos - minPos) + minPos);
}
void MultimodalEqualizerAxisInterpolation(JamesDSPLib *jdsp, int interpolationMode, int operatingMode, double *freqAx, double *gaindB)
{
	// Legacy Wrapper: Calculates and Applies immediately (Blocking)
	// Useful for non-threaded contexts or existing calls
	void* shadowConv = MultimodalEqualizerCompute(jdsp, interpolationMode, operatingMode, freqAx, gaindB, (unsigned int)jdsp->blockSize);
    void* oldConv = MultimodalEqualizerApply(jdsp, operatingMode, shadowConv);
    // Free the old (swapped out) convolver data
    if (oldConv) MultimodalEqualizerFreeConv(oldConv);
}

void* MultimodalEqualizerCompute(JamesDSPLib *jdsp, int interpolationMode, int operatingMode, double *freqAx, double *gaindB, unsigned int targetBlockSize)
{
    // Thread-Safe Implementation: Use local variables instead of modifying jdsp->mEQ
    double localFreq[NUMPTS + 2];
    double localGain[NUMPTS + 2];

    memcpy(localFreq + 1, freqAx, NUMPTS * sizeof(double));
    memcpy(localGain + 1, gaindB, NUMPTS * sizeof(double));

    for (int i = 0; i < NUMPTS; i++)
    {
        if (localGain[i] < -64.0)
            localGain[i] = -64.0;
        if (localGain[i] > 64.0)
            localGain[i] = 64.0;
    }
    localFreq[0] = 0.0;
    // CRITICAL FIX: Makima requires strictly increasing x-coordinates.
    // If localFreq[1] (copied from freqAx[0]) is 0.0, we have duplicate points (0.0, 0.0).
    // Start freqAx[0] is usually 25Hz, but if it's 0, we must shift localFreq[0] down.
    if (localFreq[1] <= 1e-6) localFreq[0] = -1.0; 

    localGain[0] = localGain[1];
    localFreq[NUMPTS + 1] = 24000.0;
    localGain[NUMPTS + 1] = localGain[NUMPTS];
    
    // FIR Modes: 0 = Min Phase
    if (operatingMode == 0)
    {
        float *eqFil;
        ierper localPch; // Use local interpolator structure
        initIerper(&localPch, NUMPTS + 2);

        if (!interpolationMode)
            pchip(&localPch, localFreq, localGain, NUMPTS + 2, 1, 1);
        else
            makima(&localPch, localFreq, localGain, NUMPTS + 2, 1, 1);

        // Generate Filter coefficients using local interpolator AND local ArbitraryEq
        // This isolates the coefficient generation from global state issues.
        ArbitraryEq *localArbEq = (ArbitraryEq*)malloc(sizeof(ArbitraryEq));
        if (localArbEq)
        {
            memset(localArbEq, 0, sizeof(ArbitraryEq));
            InitArbitraryEq(localArbEq, 0); // 0 = Minimum Phase

            // CRITICAL FIX: Copy the curve definition (nodes) from the global instance
            // The global instance holds the actual user settings (gains/freqs).
            // We only need read access to these pointers to generate the curve.
            localArbEq->nodes = jdsp->mEQ.instance.coeffGen.nodes;
            localArbEq->nodesCount = jdsp->mEQ.instance.coeffGen.nodesCount;

            eqFil = InterpolatingEqMinimumPhase(localArbEq, (float)jdsp->fs, (void *)(&localPch));
            
            freeIerper(&localPch); // Clean up local interpolator

            // DOUBLE BUFFERING: Allocate a shadow convolver and load IR (Heavy FFT)
            FFTConvolver2x2 *shadowConv = (FFTConvolver2x2*)malloc(sizeof(FFTConvolver2x2));
            if (shadowConv)
            {
                FFTConvolver2x2Init(shadowConv);
                
                // Critical: Use PASSED targetBlockSize which matches active convolver.
                // jdsp->blockSize might be mismatched with the active convolver's state.
                
                // We use LoadImpulseResponse on the shadow convolver. This performs all FFTs detached from the audio thread.
                FFTConvolver2x2LoadImpulseResponse(shadowConv, targetBlockSize, eqFil, eqFil, jdsp->mEQ.instance.filterLen);
                
                // We DO NOT free localArbEq->nodes because they point to the global state's memory
                free(localArbEq); // Free the large temp struct
                return (void*)shadowConv;
            }
            free(localArbEq);
        }
        else
        {
             freeIerper(&localPch);
        }
            
        return 0;
            
        return 0;
    }
    
    return 0;
}

void MultimodalEqualizerFreeConv(void *conv)
{
    if (conv)
    {
        FFTConvolver2x2Free((FFTConvolver2x2*)conv);
        free(conv);
    }
}

void* MultimodalEqualizerApply(JamesDSPLib *jdsp, int operatingMode, void* newConvVoid)
{
    // FIR Apply
    if (operatingMode == 0)
    {
        FFTConvolver2x2 *shadow = (FFTConvolver2x2*)newConvVoid;
        if(shadow)
        {
            FFTConvolver2x2 *active = &jdsp->mEQ.conv;
            
            if (active->_blockSize == shadow->_blockSize && active->_segCount == shadow->_segCount) {
                // Swap IR pointers (LL)
                float **tmp;
                tmp = active->_segmentsLLIRRe; active->_segmentsLLIRRe = shadow->_segmentsLLIRRe; shadow->_segmentsLLIRRe = tmp;
                tmp = active->_segmentsLLIRIm; active->_segmentsLLIRIm = shadow->_segmentsLLIRIm; shadow->_segmentsLLIRIm = tmp;
                
                // Swap IR pointers (RR)
                tmp = active->_segmentsRRIRRe; active->_segmentsRRIRRe = shadow->_segmentsRRIRRe; shadow->_segmentsRRIRRe = tmp;
                tmp = active->_segmentsRRIRIm; active->_segmentsRRIRIm = shadow->_segmentsRRIRIm; shadow->_segmentsRRIRIm = tmp;
                
                // CRITICAL FIX: Seamless State Transfer
                // Instead of resetting state or leaving it empty (causing cutouts),
                // we copy the history from the Active convolver to the Shadow convolver.
                // This preserves the audio tail and overlap, eliminating the dropout.
                
                unsigned int segByteSize = active->_fftComplexSize * sizeof(float);
                unsigned int overlapByteSize = active->_blockSize * sizeof(float);
                unsigned int inputByteSize = active->_blockSize * sizeof(float);

                // 1. Copy History Segments (Frequency Domain)
                for(unsigned int i=0; i<active->_segCount; ++i) {
                     memcpy(shadow->_segmentsReLeft[i], active->_segmentsReLeft[i], segByteSize);
                     memcpy(shadow->_segmentsImLeft[i], active->_segmentsImLeft[i], segByteSize);
                     memcpy(shadow->_segmentsReRight[i], active->_segmentsReRight[i], segByteSize);
                     memcpy(shadow->_segmentsImRight[i], active->_segmentsImRight[i], segByteSize);
                }

                // 2. Copy Overlap Buffers (Time Domain Tail)
                memcpy(shadow->_overlap[0], active->_overlap[0], overlapByteSize);
                memcpy(shadow->_overlap[1], active->_overlap[1], overlapByteSize);

                // 3. Copy Input Buffers (Current Block State)
                memcpy(shadow->_inputBuffer[0], active->_inputBuffer[0], inputByteSize);
                memcpy(shadow->_inputBuffer[1], active->_inputBuffer[1], inputByteSize);
                
                // 4. Copy State Variables
                shadow->_inputBufferFill = active->_inputBufferFill;
                shadow->_current = active->_current;

                // 5. Copy Pre-Multiplied Buffers (Current Block FD)
                // These hold the FD product of input * OLD IR.
                // Copying them means for the very first overlap-add of the new block,
                // we use the old IR's tail. This is mathematically correct for seamless transition.
                memcpy(shadow->_preMultiplied[0][0], active->_preMultiplied[0][0], segByteSize);
                memcpy(shadow->_preMultiplied[0][1], active->_preMultiplied[0][1], segByteSize);
                memcpy(shadow->_preMultiplied[1][0], active->_preMultiplied[1][0], segByteSize);
                memcpy(shadow->_preMultiplied[1][1], active->_preMultiplied[1][1], segByteSize);

                // Active keeps its runtime state but Shadow is now cloned + updated IR.
                
                /*
                // OLD BROKEN CODE:
                // CRITICAL FIX: Clear accumulated frequency-domain buffers.
                ...
                */
                
                // Active keeps its runtime state (_inputBuffer, _current, etc.)
                // Shadow takes the OLD IRs and can be freed safely.
            } else {
                // Dimensions changed (Mismatch).
                // Must perform full state reset.
                FFTConvolver2x2 tempStruct = *active;
                *active = *shadow;
                *shadow = tempStruct;
            }
            return (void*)shadow; 
        }
        return 0;
    }
	else // IIR Apply
	{
        // IIR Logic remains same
        // Check for mode transition FIR -> IIR
        if (jdsp->mEQ.operatingMode == 0) {
            // Clear IIR state buffers to prevent clicking/popping from stable/garbage state
            memset(jdsp->mEQ.z1_AL, 0, sizeof(jdsp->mEQ.z1_AL));
            memset(jdsp->mEQ.z2_AL, 0, sizeof(jdsp->mEQ.z2_AL));
            memset(jdsp->mEQ.z1_AR, 0, sizeof(jdsp->mEQ.z1_AR));
            memset(jdsp->mEQ.z2_AR, 0, sizeof(jdsp->mEQ.z2_AR));
        }

		double *freq = jdsp->mEQ.freq + 1;
		double *gains = jdsp->mEQ.gain + 1;
        
		if (operatingMode == 1) jdsp->mEQ.order = 4;
		else if (operatingMode == 2) jdsp->mEQ.order = 6;
		else if (operatingMode == 3) jdsp->mEQ.order = 8;
		else if (operatingMode == 4) jdsp->mEQ.order = 10; 
		else if (operatingMode == 5 || operatingMode == 4) jdsp->mEQ.order = 12; // Handle both just in case
        
		jdsp->mEQ.nSec = jdsp->mEQ.order >> 1;
		if (jdsp->mEQ.nSec > MAXSECTIONS)
		{
			jdsp->mEQ.order = MAXORDER;
			jdsp->mEQ.nSec = MAXSECTIONS;
		}

		for (int i = 0; i < NUMPTS - 1; i++)
		{
			double dB = gains[i + 1] - gains[i];
			double designFreq;
			if (i)
				designFreq = (freq[i + 1] + freq[i]) * 0.5;
			else
				designFreq = freq[i];
			double overallGain = i == 0 ? gains[i] : 0.0;
			if (i == 0)
				jdsp->mEQ.sec[i] = HSHOSVF(jdsp->fs, designFreq, jdsp->mEQ.order, dB, overallGain, jdsp->mEQ.c1 + i * jdsp->mEQ.nSec, jdsp->mEQ.c2 + i * jdsp->mEQ.nSec, jdsp->mEQ.d0 + i * jdsp->mEQ.nSec, jdsp->mEQ.d1 + i * jdsp->mEQ.nSec, &jdsp->mEQ.overallGain);
			else
			{
				float dummy;
				jdsp->mEQ.sec[i] = HSHOSVF(jdsp->fs, designFreq, jdsp->mEQ.order, dB, overallGain, jdsp->mEQ.c1 + i * jdsp->mEQ.nSec, jdsp->mEQ.c2 + i * jdsp->mEQ.nSec, jdsp->mEQ.d0 + i * jdsp->mEQ.nSec, jdsp->mEQ.d1 + i * jdsp->mEQ.nSec, &dummy);
			}
		}
	}
	jdsp->mEQ.operatingMode = operatingMode;
    return 0;
}

void MultimodalEqualizerEnable(JamesDSPLib *jdsp, char enable)
{
	if (jdsp->equalizerForceRefresh)
	{
		float *eqFil;
		if (!jdsp->mEQ.currentInterpolationMode)
		{
			pchip(&jdsp->mEQ.pch1, jdsp->mEQ.freq, jdsp->mEQ.gain, NUMPTS + 2, 1, 1);
			eqFil = InterpolatingEqMinimumPhase(&jdsp->mEQ.instance.coeffGen, (float)jdsp->fs, (void *)(&jdsp->mEQ.pch1));
		}
		else
		{
			makima(&jdsp->mEQ.pch2, jdsp->mEQ.freq, jdsp->mEQ.gain, NUMPTS + 2, 1, 1);
			eqFil = InterpolatingEqMinimumPhase(&jdsp->mEQ.instance.coeffGen, (float)jdsp->fs, (void *)(&jdsp->mEQ.pch2));
		}
		FFTConvolver2x2Free(&jdsp->mEQ.instance.convState);
		FFTConvolver2x2Free(&jdsp->mEQ.conv);
		FFTConvolver2x2LoadImpulseResponse(&jdsp->mEQ.instance.convState, (unsigned int)jdsp->blockSize, eqFil, eqFil, jdsp->mEQ.instance.filterLen);
		FFTConvolver2x2LoadImpulseResponse(&jdsp->mEQ.conv, (unsigned int)jdsp->blockSize, eqFil, eqFil, jdsp->mEQ.instance.filterLen);
		jdsp->equalizerForceRefresh = 0;
	}
	if (enable)
		jdsp->equalizerEnabled = 1;
}
void MultimodalEqualizerDisable(JamesDSPLib *jdsp)
{
	jdsp->equalizerEnabled = 0;
}
void MultimodalEqualizerProcess(JamesDSPLib *jdsp, size_t n)
{
	if (!jdsp->mEQ.operatingMode)
		FFTConvolver2x2Process(&jdsp->mEQ.conv, jdsp->tmpBuffer[0], jdsp->tmpBuffer[1], jdsp->tmpBuffer[0], jdsp->tmpBuffer[1], (unsigned int)n);
	else
	{
		for (size_t smp = 0; smp < n; smp++)
		{
			float x1 = jdsp->tmpBuffer[0][smp];
			float x2 = jdsp->tmpBuffer[1][smp];
			for (int i = 0; i < NUMPTS - 1; i++)
			{
				for (char j = 0; j < jdsp->mEQ.sec[i]; j++)
				{
					float y1 = x1 - jdsp->mEQ.z1_AL[i * jdsp->mEQ.nSec + j] - jdsp->mEQ.z2_AL[i * jdsp->mEQ.nSec + j];
					x1 = jdsp->mEQ.d0[i * jdsp->mEQ.nSec + j] * y1 + jdsp->mEQ.d1[i * jdsp->mEQ.nSec + j] * jdsp->mEQ.z1_AL[i * jdsp->mEQ.nSec + j] + jdsp->mEQ.z2_AL[i * jdsp->mEQ.nSec + j];
					jdsp->mEQ.z2_AL[i * jdsp->mEQ.nSec + j] = jdsp->mEQ.z2_AL[i * jdsp->mEQ.nSec + j] + jdsp->mEQ.c2[i * jdsp->mEQ.nSec + j] * jdsp->mEQ.z1_AL[i * jdsp->mEQ.nSec + j];
					jdsp->mEQ.z1_AL[i * jdsp->mEQ.nSec + j] = jdsp->mEQ.z1_AL[i * jdsp->mEQ.nSec + j] + jdsp->mEQ.c1[i * jdsp->mEQ.nSec + j] * y1;
					float y2 = x2 - jdsp->mEQ.z1_AR[i * jdsp->mEQ.nSec + j] - jdsp->mEQ.z2_AR[i * jdsp->mEQ.nSec + j];
					x2 = jdsp->mEQ.d0[i * jdsp->mEQ.nSec + j] * y2 + jdsp->mEQ.d1[i * jdsp->mEQ.nSec + j] * jdsp->mEQ.z1_AR[i * jdsp->mEQ.nSec + j] + jdsp->mEQ.z2_AR[i * jdsp->mEQ.nSec + j];
					jdsp->mEQ.z2_AR[i * jdsp->mEQ.nSec + j] = jdsp->mEQ.z2_AR[i * jdsp->mEQ.nSec + j] + jdsp->mEQ.c2[i * jdsp->mEQ.nSec + j] * jdsp->mEQ.z1_AR[i * jdsp->mEQ.nSec + j];
					jdsp->mEQ.z1_AR[i * jdsp->mEQ.nSec + j] = jdsp->mEQ.z1_AR[i * jdsp->mEQ.nSec + j] + jdsp->mEQ.c1[i * jdsp->mEQ.nSec + j] * y2;
				}
			}
			jdsp->tmpBuffer[0][smp] = x1 * jdsp->mEQ.overallGain;
			jdsp->tmpBuffer[1][smp] = x2 * jdsp->mEQ.overallGain;
		}
	}
}
