void NSEEL_HOSTSTUB_EnterMutex() { }
void NSEEL_HOSTSTUB_LeaveMutex() { }
#include "../jdsp_header.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum
{
	LIVEPROG_SECTION_NONE = -1,
	LIVEPROG_SECTION_INIT,
	LIVEPROG_SECTION_SLIDER,
	LIVEPROG_SECTION_BLOCK,
	LIVEPROG_SECTION_SAMPLE,
	LIVEPROG_SECTION_COUNT,
	LIVEPROG_SECTION_OTHER
};

typedef struct
{
	const char *start;
	size_t length;
	int present;
} LiveProgSourceSection;

static int LiveProgSectionAtLine(const char *line, const char *lineEnd)
{
	while (line < lineEnd && (*line == ' ' || *line == '\t'))
		line++;
	if (line >= lineEnd || *line != '@')
		return LIVEPROG_SECTION_NONE;

	const char *name = ++line;
	while (line < lineEnd && (isalnum((unsigned char)*line) || *line == '_'))
		line++;
	const size_t nameLength = (size_t)(line - name);
	if (!nameLength)
		return LIVEPROG_SECTION_NONE;

	while (line < lineEnd && (*line == ' ' || *line == '\t' || *line == '\r'))
		line++;
	if (line < lineEnd && !(line + 1 < lineEnd && line[0] == '/' && line[1] == '/'))
		return LIVEPROG_SECTION_NONE;

	if (nameLength == 4 && !strncmp(name, "init", nameLength))
		return LIVEPROG_SECTION_INIT;
	if (nameLength == 6 && !strncmp(name, "slider", nameLength))
		return LIVEPROG_SECTION_SLIDER;
	if (nameLength == 5 && !strncmp(name, "block", nameLength))
		return LIVEPROG_SECTION_BLOCK;
	if (nameLength == 6 && !strncmp(name, "sample", nameLength))
		return LIVEPROG_SECTION_SAMPLE;
	return LIVEPROG_SECTION_OTHER;
}

static int LiveProgSplitSource(const char *source, LiveProgSourceSection sections[LIVEPROG_SECTION_COUNT])
{
	memset(sections, 0, sizeof(LiveProgSourceSection) * LIVEPROG_SECTION_COUNT);
	const char *end = source + strlen(source);
	const char *line = source;
	int currentSection = LIVEPROG_SECTION_NONE;

	while (line < end)
	{
		const char *newLine = memchr(line, '\n', (size_t)(end - line));
		const char *lineEnd = newLine ? newLine : end;
		const int section = LiveProgSectionAtLine(line, lineEnd);
		if (section != LIVEPROG_SECTION_NONE)
		{
			if (currentSection >= 0 && currentSection < LIVEPROG_SECTION_COUNT)
				sections[currentSection].length = (size_t)(line - sections[currentSection].start);
			currentSection = LIVEPROG_SECTION_NONE;

			if (section >= 0 && section < LIVEPROG_SECTION_COUNT)
			{
				if (sections[section].present)
					return -6;
				sections[section].present = 1;
				sections[section].start = newLine ? newLine + 1 : end;
				currentSection = section;
			}
		}
		line = newLine ? newLine + 1 : end;
	}

	if (currentSection >= 0 && currentSection < LIVEPROG_SECTION_COUNT)
		sections[currentSection].length = (size_t)(end - sections[currentSection].start);
	return sections[LIVEPROG_SECTION_SAMPLE].present ? 1 : -2;
}

static char *LiveProgCopySection(const LiveProgSourceSection *section)
{
	if (!section->present)
		return 0;
	char *copy = (char*)calloc(section->length + 1, sizeof(char));
	if (copy && section->length)
		memcpy(copy, section->start, section->length);
	return copy;
}

static void LiveProgClearCode(LiveProg *pg)
{
	if (pg->codehandleProcess)
		NSEEL_code_free(pg->codehandleProcess);
	if (pg->codehandleBlock)
		NSEEL_code_free(pg->codehandleBlock);
	if (pg->codehandleSlider)
		NSEEL_code_free(pg->codehandleSlider);
	if (pg->codehandleInit)
		NSEEL_code_free(pg->codehandleInit);
	pg->codehandleInit = 0;
	pg->codehandleSlider = 0;
	pg->codehandleBlock = 0;
	pg->codehandleProcess = 0;
}

static void LiveProgDestroyState(LiveProg *pg)
{
	if (pg->vm)
	{
		LiveProgClearCode(pg);
		NSEEL_VM_free(pg->vm);
	}
	memset(pg, 0, sizeof(*pg));
}

static int LiveProgInitializeState(LiveProg *pg, float sampleRate)
{
	memset(pg, 0, sizeof(*pg));
	pg->active = 1;
	pg->vm = NSEEL_VM_alloc();
	if (!pg->vm)
		return 0;
	pg->vmFs = NSEEL_VM_regvar(pg->vm, "srate");
	pg->samplesBlock = NSEEL_VM_regvar(pg->vm, "samplesblock");
	pg->input1 = NSEEL_VM_regvar(pg->vm, "spl0");
	pg->input2 = NSEEL_VM_regvar(pg->vm, "spl1");
	if (!pg->vmFs || !pg->samplesBlock || !pg->input1 || !pg->input2)
	{
		LiveProgDestroyState(pg);
		return 0;
	}
	*pg->vmFs = sampleRate;
	*pg->samplesBlock = 0;
	return 1;
}

void LiveProgConstructor(JamesDSPLib *jdsp)
{
	LiveProgInitializeState(&jdsp->eel, jdsp->fs);
}

void LiveProgDestructor(JamesDSPLib *jdsp)
{
	LiveProgDestroyState(&jdsp->eel);
}

void LiveProgEnable(JamesDSPLib *jdsp)
{
	if (jdsp->eel.vmFs && jdsp->eel.compileSucessfully)
	{
		*jdsp->eel.vmFs = jdsp->fs;
		jdsp->liveprogEnabled = 1;
	}
	else
		jdsp->liveprogEnabled = 0;
}

void LiveProgDisable(JamesDSPLib *jdsp)
{
	jdsp->liveprogEnabled = 0;
}

static int LiveProgLoadCode(LiveProg *pg, float sampleRate, const char *codeTextInit, const char *codeTextSlider,
	const char *codeTextBlock, const char *codeTextProcess)
{
	pg->compileSucessfully = 0;
	compileContext *ctx = (compileContext*)pg->vm;
	LiveProgClearCode(pg);
	NSEEL_VM_freevars(pg->vm);
	NSEEL_init_memRegion(pg->vm);
	memset(ctx->ram_state, 0, sizeof(ctx->ram_state));
	pg->vmFs = NSEEL_VM_regvar(pg->vm, "srate");
	pg->samplesBlock = NSEEL_VM_regvar(pg->vm, "samplesblock");
	pg->input1 = NSEEL_VM_regvar(pg->vm, "spl0");
	pg->input2 = NSEEL_VM_regvar(pg->vm, "spl1");
	if (!pg->vmFs || !pg->samplesBlock || !pg->input1 || !pg->input2)
		return -7;
	*pg->vmFs = sampleRate;
	*pg->samplesBlock = 0;
	ctx->functions_common = 0;

	if (codeTextInit && *codeTextInit)
	{
		pg->codehandleInit = NSEEL_code_compile_ex(pg->vm, codeTextInit, 0,
			NSEEL_CODE_COMPILE_FLAG_COMMONFUNCS | NSEEL_CODE_COMPILE_FLAG_COMMONFUNCS_RESET);
		if (!pg->codehandleInit)
			return -1;
	}
	if (codeTextSlider && *codeTextSlider)
	{
		pg->codehandleSlider = NSEEL_code_compile(pg->vm, codeTextSlider, 0);
		if (!pg->codehandleSlider)
			return -4;
	}
	if (codeTextBlock && *codeTextBlock)
	{
		pg->codehandleBlock = NSEEL_code_compile(pg->vm, codeTextBlock, 0);
		if (!pg->codehandleBlock)
			return -5;
	}
	pg->codehandleProcess = NSEEL_code_compile(pg->vm, codeTextProcess, 0);
	if (!pg->codehandleProcess)
		return -3;

	if (pg->codehandleInit)
		NSEEL_code_execute(pg->codehandleInit);
	if (pg->codehandleSlider)
		NSEEL_code_execute(pg->codehandleSlider);
	pg->compileSucessfully = 1;
	return 1;
}

const char* checkErrorCode(int errCode)
{
	switch (errCode)
	{
	case -1:
		return "Syntax error at @init section";
	case -2:
		return "@sample section not found";
	case -3:
		return "Syntax error at @sample section";
	case -4:
		return "Syntax error at @slider section";
	case -5:
		return "Syntax error at @block section";
	case -6:
		return "Duplicate LiveProg section";
	case -7:
		return "Failed to allocate LiveProg source sections";
	default:
		return "No syntax errors detected";
	}
}

int LiveProgStringParser(JamesDSPLib *jdsp, char *eelCode, char *errorBuffer, size_t errorBufferSize)
{
	if (errorBuffer && errorBufferSize)
		errorBuffer[0] = '\0';
	LiveProgSourceSection sections[LIVEPROG_SECTION_COUNT];
	int errorMsg = LiveProgSplitSource(eelCode, sections);
	char *codeText[LIVEPROG_SECTION_COUNT] = { 0 };
	if (errorMsg > 0)
	{
		for (int i = 0; i < LIVEPROG_SECTION_COUNT; i++)
		{
			codeText[i] = LiveProgCopySection(&sections[i]);
			if (sections[i].present && !codeText[i])
			{
				errorMsg = -7;
				break;
			}
		}
		if (errorMsg > 0)
		{
			LiveProg candidate;
			if (!LiveProgInitializeState(&candidate, jdsp->fs))
				errorMsg = -7;
			else
			{
				jdsp_lock(jdsp);
				errorMsg = LiveProgLoadCode(&candidate, jdsp->fs,
					codeText[LIVEPROG_SECTION_INIT], codeText[LIVEPROG_SECTION_SLIDER],
					codeText[LIVEPROG_SECTION_BLOCK], codeText[LIVEPROG_SECTION_SAMPLE]);
				if (errorMsg > 0)
				{
					LiveProg previous = jdsp->eel;
					candidate.active = previous.active;
					jdsp->eel = candidate;
					memset(&candidate, 0, sizeof(candidate));
					LiveProgDestroyState(&previous);
				}
				else
				{
					const char *error = NSEEL_code_getcodeerror(candidate.vm);
					if (error && errorBuffer && errorBufferSize)
						snprintf(errorBuffer, errorBufferSize, "%s", error);
				}
				LiveProgDestroyState(&candidate);
				jdsp_unlock(jdsp);
			}
		}
	}
	for (int i = 0; i < LIVEPROG_SECTION_COUNT; i++)
		free(codeText[i]);
	return errorMsg;
}

int LiveProgSetVariable(JamesDSPLib *jdsp, const char *name, float value)
{
	if (!jdsp || !name || !*name || !isfinite(value))
		return 0;
	const size_t nameLength = strlen(name);
	if (nameLength > NSEEL_MAX_VARIABLE_NAMELEN ||
		!(isalpha((unsigned char)name[0]) || name[0] == '_'))
		return 0;
	for (size_t i = 1; i < nameLength; i++)
		if (!(isalnum((unsigned char)name[i]) || name[i] == '_'))
			return 0;
	jdsp_lock(jdsp);
	LiveProg *pg = &jdsp->eel;
	float *variable = pg->vm && pg->compileSucessfully ? NSEEL_VM_getvar(pg->vm, name) : 0;
	if (!variable)
	{
		jdsp_unlock(jdsp);
		return 0;
	}
	*variable = value;
	if (pg->codehandleSlider)
		NSEEL_code_execute(pg->codehandleSlider);
	jdsp_unlock(jdsp);
	return 1;
}

void LiveProgProcess(JamesDSPLib *jdsp, size_t n)
{
	LiveProg *eel = &jdsp->eel;
	if (eel->compileSucessfully && eel->active)
	{
		*eel->samplesBlock = (float)n;
		if (eel->codehandleBlock)
			NSEEL_code_execute(eel->codehandleBlock);
		for (size_t i = 0; i < n; i++)
		{
			*eel->input1 = jdsp->tmpBuffer[0][i];
			*eel->input2 = jdsp->tmpBuffer[1][i];
			NSEEL_code_execute(eel->codehandleProcess);
			if (isinf((float)*eel->input1) || isnan((float)*eel->input1))
				jdsp->tmpBuffer[0][i] = 0;
			else
				jdsp->tmpBuffer[0][i] = (float)*eel->input1;

			if (isinf((float)*eel->input2) || isnan((float)*eel->input2))
				jdsp->tmpBuffer[1][i] = 0;
			else
				jdsp->tmpBuffer[1][i] = (float)*eel->input2;
		}
	}
}
