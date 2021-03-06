// for finding memory leaks in debug mode with Visual Studio 
#if defined _DEBUG && defined _MSC_VER
#include <crtdbg.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>
#ifndef _WIN32
#include <unistd.h>
#include <limits.h>
#endif
#include "pt2_helpers.h"
#include "pt2_header.h"
#include "pt2_config.h"
#include "pt2_tables.h"
#include "pt2_audio.h"
#include "pt2_diskop.h"
#include "pt2_config.h"
#include "pt2_textout.h"

#ifndef _WIN32
static char oldCwd[PATH_MAX];
#endif

static bool loadProTrackerDotIni(FILE *f);
static FILE *openPTDotConfig(void);
static bool loadPTDotConfig(FILE *f);
static bool loadColorsDotIni(void);

void loadConfig(void)
{
	bool proTrackerDotIniFound, ptDotConfigFound;
#ifndef _WIN32
	bool colorsDotIniFound;
#endif
	FILE *f;

	// set default config values first
	ptConfig.fullScreenStretch = false;
	ptConfig.pattDots = false;
	ptConfig.dottedCenterFlag = true;
	ptConfig.a500LowPassFilter = false;
	ptConfig.soundFrequency = 48000;
	ptConfig.rememberPlayMode = false;
	ptConfig.stereoSeparation = 20;
	ptConfig.videoScaleFactor = 2;
	ptConfig.realVuMeters = false;
	ptConfig.modDot = false;
	ptConfig.accidental = 0; // sharp
	ptConfig.quantizeValue = 1;
	ptConfig.transDel = false;
	ptConfig.blankZeroFlag = false;
	ptConfig.compoMode = false;
	ptConfig.soundBufferSize = 1024;
	ptConfig.autoCloseDiskOp = true;
	ptConfig.vsyncOff = false;
	ptConfig.hwMouse = false;

#ifndef _WIN32
	getcwd(oldCwd, PATH_MAX);
#endif

	// load protracker.ini
	proTrackerDotIniFound = false;

#ifdef _WIN32
	f = fopen("protracker.ini", "r");
	if (f != NULL)
		proTrackerDotIniFound = true;
#else
	// check in program directory
	f = fopen("protracker.ini", "r");
	if (f != NULL)
		proTrackerDotIniFound = true;

	// check in ~/.protracker/
	if (!proTrackerDotIniFound && changePathToHome() && chdir(".protracker") == 0)
	{
		f = fopen("protracker.ini", "r");
		if (f != NULL)
			proTrackerDotIniFound = true;
	}

	chdir(oldCwd);
#endif

	if (proTrackerDotIniFound)
		loadProTrackerDotIni(f);

	editor.oldTempo = editor.initialTempo;

	// load PT.Config (if available)
	ptDotConfigFound = false;

#ifdef _WIN32
	f = openPTDotConfig();
	if (f != NULL)
		ptDotConfigFound = true;
#else
	// check in program directory
	f = openPTDotConfig();
	if (f != NULL)
		ptDotConfigFound = true;

	// check in ~/.protracker/
	if (!ptDotConfigFound && changePathToHome() && chdir(".protracker") == 0)
	{
		f = openPTDotConfig();
		if (f != NULL)
			ptDotConfigFound = true;
	}

	chdir(oldCwd);
#endif

	if (ptDotConfigFound)
		loadPTDotConfig(f);

	if (proTrackerDotIniFound || ptDotConfigFound)
		editor.configFound = true;

	// load colors.ini (if available)
#ifdef _WIN32
	loadColorsDotIni();
#else
	// check in program directory
	colorsDotIniFound = loadColorsDotIni();

	// check in ~/.protracker/
	if (!colorsDotIniFound && changePathToHome() && chdir(".protracker") == 0)
		loadColorsDotIni();
#endif

#ifndef _WIN32
	chdir(oldCwd);
#endif
}

static bool loadProTrackerDotIni(FILE *f)
{
	char *configBuffer, *configLine;
	uint32_t configFileSize, lineLen, i;

	fseek(f, 0, SEEK_END);
	configFileSize = ftell(f);
	rewind(f);

	configBuffer = (char *)malloc(configFileSize + 1);
	if (configBuffer == NULL)
	{
		fclose(f);
		showErrorMsgBox("Couldn't parse protracker.ini: Out of memory!");
		return false;
	}

	fread(configBuffer, 1, configFileSize, f);
	configBuffer[configFileSize] = '\0';
	fclose(f);

	configLine = strtok(configBuffer, "\n");
	while (configLine != NULL)
	{
		lineLen = (uint32_t)strlen(configLine);

		// remove CR in CRLF linefeed (if present)
		if (lineLen > 1)
		{
			if (configLine[lineLen-1] == '\r')
			{
				configLine[lineLen-1] = '\0';
				lineLen--;
			}
		}

		// COMMENT OR CATEGORY
		if (*configLine == ';' || *configLine == '[')
		{
			configLine = strtok(NULL, "\n");
			continue;
		}

		// HWMOUSE
		else if (!_strnicmp(configLine, "HWMOUSE=", 8))
		{
			     if (!_strnicmp(&configLine[8], "TRUE",  4)) ptConfig.hwMouse = true;
			else if (!_strnicmp(&configLine[8], "FALSE", 5)) ptConfig.hwMouse = false;
		}

		// VSYNCOFF
		else if (!_strnicmp(configLine, "VSYNCOFF=", 9))
		{
			     if (!_strnicmp(&configLine[9], "TRUE",  4)) ptConfig.vsyncOff = true;
			else if (!_strnicmp(&configLine[9], "FALSE", 5)) ptConfig.vsyncOff = false;
		}

		// FULLSCREENSTRETCH
		else if (!_strnicmp(configLine, "FULLSCREENSTRETCH=", 18))
		{
			     if (!_strnicmp(&configLine[18], "TRUE",  4)) ptConfig.fullScreenStretch = true;
			else if (!_strnicmp(&configLine[18], "FALSE", 5)) ptConfig.fullScreenStretch = false;
		}

		// HIDEDISKOPDATES
		else if (!_strnicmp(configLine, "HIDEDISKOPDATES=", 16))
		{
			     if (!_strnicmp(&configLine[16], "TRUE",  4)) ptConfig.hideDiskOpDates = true;
			else if (!_strnicmp(&configLine[16], "FALSE", 5)) ptConfig.hideDiskOpDates = false;
		}

		// AUTOCLOSEDISKOP
		else if (!_strnicmp(configLine, "AUTOCLOSEDISKOP=", 16))
		{
			     if (!_strnicmp(&configLine[16], "TRUE",  4)) ptConfig.autoCloseDiskOp = true;
			else if (!_strnicmp(&configLine[16], "FALSE", 5)) ptConfig.autoCloseDiskOp = false;
		}

		// COMPOMODE
		else if (!_strnicmp(configLine, "COMPOMODE=", 10))
		{
			     if (!_strnicmp(&configLine[10], "TRUE",  4)) ptConfig.compoMode = true;
			else if (!_strnicmp(&configLine[10], "FALSE", 5)) ptConfig.compoMode = false;
		}

		// PATTDOTS
		else if (!_strnicmp(configLine, "PATTDOTS=", 9))
		{
			     if (!_strnicmp(&configLine[9], "TRUE",  4)) ptConfig.pattDots = true;
			else if (!_strnicmp(&configLine[9], "FALSE", 5)) ptConfig.pattDots = false;
		}

		// BLANKZERO
		else if (!_strnicmp(configLine, "BLANKZERO=", 10))
		{
			     if (!_strnicmp(&configLine[10], "TRUE",  4)) ptConfig.blankZeroFlag = true;
			else if (!_strnicmp(&configLine[10], "FALSE", 5)) ptConfig.blankZeroFlag = false;
		}

		// REALVUMETERS
		else if (!_strnicmp(configLine, "REALVUMETERS=", 13))
		{
			     if (!_strnicmp(&configLine[13], "TRUE",  4)) ptConfig.realVuMeters = true;
			else if (!_strnicmp(&configLine[13], "FALSE", 5)) ptConfig.realVuMeters = false;
		}

		// ACCIDENTAL
		else if (!_strnicmp(configLine, "ACCIDENTAL=", 11))
		{
			     if (!_strnicmp(&configLine[11], "SHARP", 4)) ptConfig.accidental = 0;
			else if (!_strnicmp(&configLine[11], "FLAT",  5)) ptConfig.accidental = 1;
		}

		// QUANTIZE
		else if (!_strnicmp(configLine, "QUANTIZE=", 9))
		{
			if (configLine[9] != '\0')
				ptConfig.quantizeValue = (int16_t)(CLAMP(atoi(&configLine[9]), 0, 63));
		}

		// TRANSDEL
		else if (!_strnicmp(configLine, "TRANSDEL=", 9))
		{
			     if (!_strnicmp(&configLine[9], "TRUE",  4)) ptConfig.transDel = true;
			else if (!_strnicmp(&configLine[9], "FALSE", 5)) ptConfig.transDel = false;
		}

		// DOTTEDCENTER
		else if (!_strnicmp(configLine, "DOTTEDCENTER=", 13))
		{
			     if (!_strnicmp(&configLine[13], "TRUE",  4)) ptConfig.dottedCenterFlag = true;
			else if (!_strnicmp(&configLine[13], "FALSE", 5)) ptConfig.dottedCenterFlag = false;
		}

		// MODDOT
		else if (!_strnicmp(configLine, "MODDOT=", 7))
		{
			     if (!_strnicmp(&configLine[7], "TRUE",  4)) ptConfig.modDot = true;
			else if (!_strnicmp(&configLine[7], "FALSE", 5)) ptConfig.modDot = false;
		}

		// SCALE3X (deprecated)
		else if (!_strnicmp(configLine, "SCALE3X=", 8))
		{
			     if (!_strnicmp(&configLine[8], "TRUE",  4)) ptConfig.videoScaleFactor = 3;
			else if (!_strnicmp(&configLine[8], "FALSE", 5)) ptConfig.videoScaleFactor = 2;
		}

		// VIDEOSCALE
		else if (!_strnicmp(configLine, "VIDEOSCALE=", 11))
		{
			if (lineLen >= 13 && configLine[12] == 'X' && isdigit(configLine[11]))
				ptConfig.videoScaleFactor = configLine[11] - '0';
		}

		// REMEMBERPLAYMODE
		else if (!_strnicmp(configLine, "REMEMBERPLAYMODE=", 17))
		{
			     if (!_strnicmp(&configLine[17], "TRUE",  4)) ptConfig.rememberPlayMode = true;
			else if (!_strnicmp(&configLine[17], "FALSE", 5)) ptConfig.rememberPlayMode = false;
		}

		// DEFAULTDIR
		else if (!_strnicmp(configLine, "DEFAULTDIR=", 11))
		{
			if (lineLen > 11)
			{
				i = 11;
				while (configLine[i] == ' ') i++; // remove spaces before string (if present)
				while (configLine[lineLen-1] == ' ') lineLen--; // remove spaces after string (if present)

				lineLen -= i;
				if (lineLen > 0)
					strncpy(ptConfig.defModulesDir, &configLine[i], (lineLen > PATH_MAX) ? PATH_MAX : lineLen);
			}
		}

		// DEFAULTSMPDIR
		else if (!_strnicmp(configLine, "DEFAULTSMPDIR=", 14))
		{
			if (lineLen > 14)
			{
				i = 14;
				while (configLine[i] == ' ') i++; // remove spaces before string (if present)
				while (configLine[lineLen-1] == ' ') lineLen--; // remove spaces after string (if present)

				lineLen -= i;
				if (lineLen > 0)
					strncpy(ptConfig.defSamplesDir, &configLine[i], (lineLen > PATH_MAX) ? PATH_MAX : lineLen);
			}
		}

		// A500LOWPASSFILTER
		else if (!_strnicmp(configLine, "A500LOWPASSFILTER=", 18))
		{
			     if (!_strnicmp(&configLine[18], "TRUE",  4)) ptConfig.a500LowPassFilter = true;
			else if (!_strnicmp(&configLine[18], "FALSE", 5)) ptConfig.a500LowPassFilter = false;
		}

		// A4000LOWPASSFILTER (deprecated, same as A500LOWPASSFILTER)
		else if (!_strnicmp(configLine, "A4000LOWPASSFILTER=", 19))
		{
			     if (!_strnicmp(&configLine[19], "TRUE",  4)) ptConfig.a500LowPassFilter = true;
			else if (!_strnicmp(&configLine[19], "FALSE", 5)) ptConfig.a500LowPassFilter = false;
		}

		// FREQUENCY
		else if (!_strnicmp(configLine, "FREQUENCY=", 10))
		{
			if (configLine[10] != '\0')
				ptConfig.soundFrequency = (uint32_t)(CLAMP(atoi(&configLine[10]), 32000, 96000));
		}

		// BUFFERSIZE
		else if (!_strnicmp(configLine, "BUFFERSIZE=", 11))
		{
			if (configLine[11] != '\0')
				ptConfig.soundBufferSize = (uint32_t)(CLAMP(atoi(&configLine[11]), 128, 8192));
		}

		// STEREOSEPARATION
		else if (!_strnicmp(configLine, "STEREOSEPARATION=", 17))
		{
			if (configLine[17] != '\0')
				ptConfig.stereoSeparation = (int8_t)(CLAMP(atoi(&configLine[17]), 0, 100));
		}

		configLine = strtok(NULL, "\n");
	}

	free(configBuffer);
	return true;
}

static FILE *openPTDotConfig(void)
{
	char tmpFilename[16];
	uint8_t i;
	FILE *f;

	f = fopen("PT.Config", "rb"); // PT didn't read PT.Config with no number, but let's support it
	if (f == NULL)
	{
		for (i = 0; i < 100; i++)
		{
			sprintf(tmpFilename, "PT.Config-%02d", i);
			f = fopen(tmpFilename, "rb");
			if (f != NULL)
				break;
		}

		if (i == 100)
			return NULL;
	}

	return f;
}

static bool loadPTDotConfig(FILE *f)
{
	char cfgString[24];
	uint8_t tmp8;
	uint16_t tmp16;
	int32_t i;
	uint32_t configFileSize;

	// get filesize
	fseek(f, 0, SEEK_END);
	configFileSize = ftell(f);
	if (configFileSize != 1024)
	{
		// not a valid PT.Config file
		fclose(f);
		return false;
	}
	rewind(f);

	// check if file is a PT.Config file
	fread(cfgString, 1, 24, f);

	/* force version string to 2.3 so that we'll accept all versions.
	** AFAIK we're only loading values that were present since 1.0,
	** so it should be safe. */
	cfgString[2] = '2';
	cfgString[4] = '3';

	if (strncmp(cfgString, "PT2.3 Configuration File", 24) != 0)
	{
		fclose(f);
		return false;
	}

	// Palette
	fseek(f, 154, SEEK_SET);
	for (i = 0; i < 8; i++)
	{
		fread(&tmp16, 2, 1, f); // stored as Big-Endian
		tmp16 = SWAP16(tmp16);
		palette[i] = RGB12_to_RGB24(tmp16);
	}

	// Transpose Delete (delete out of range notes on transposing)
	fseek(f, 174, SEEK_SET);
	fread(&tmp8, 1, 1, f);
	ptConfig.transDel = tmp8 ? true : false;
	ptConfig.transDel = ptConfig.transDel;

	// Note style (sharps/flats)
	fseek(f, 200, SEEK_SET);
	fread(&tmp8, 1, 1, f);
	ptConfig.accidental = tmp8 ? 1 : 0;
	ptConfig.accidental = ptConfig.accidental;

	// Multi Mode Next
	fseek(f, 462, SEEK_SET);
	fread(&editor.multiModeNext[0], 1, 1, f);
	fread(&editor.multiModeNext[1], 1, 1, f);
	fread(&editor.multiModeNext[2], 1, 1, f);
	fread(&editor.multiModeNext[3], 1, 1, f);

	// Effect Macros
	fseek(f, 466, SEEK_SET);
	for (i = 0; i < 10; i++)
	{
		fread(&tmp16, 2, 1, f); // stored as Big-Endian
		tmp16 = SWAP16(tmp16);
		editor.effectMacros[i] = tmp16;
	}

	// Timing Mode (CIA/VBLANK)
	fseek(f, 487, SEEK_SET);
	fread(&tmp8, 1, 1, f);
	editor.timingMode = tmp8 ? TEMPO_MODE_CIA : TEMPO_MODE_VBLANK;

	// Blank Zeroes
	fseek(f, 490, SEEK_SET);
	fread(&tmp8, 1, 1, f);
	ptConfig.blankZeroFlag = tmp8 ? true : false;
	ptConfig.blankZeroFlag = ptConfig.blankZeroFlag;

	// Initial Tempo (don't load if timing is set to VBLANK)
	if (editor.timingMode == TEMPO_MODE_CIA)
	{
		fseek(f, 497, SEEK_SET);
		fread(&tmp8, 1, 1, f);
		if (tmp8 < 32) tmp8 = 32;
		editor.initialTempo = tmp8;
		editor.oldTempo = tmp8;
	}

	// Tuning Tone Note
	fseek(f, 501, SEEK_SET);
	fread(&tmp8, 1, 1, f);
	if (tmp8 > 35) tmp8 = 35;
	editor.tuningNote = tmp8;

	// Tuning Tone Volume
	fseek(f, 503, SEEK_SET);
	fread(&tmp8, 1, 1, f);
	if (tmp8 > 64) tmp8 = 64;
	editor.tuningVol = tmp8;

	// Initial Speed
	fseek(f, 545, SEEK_SET);
	fread(&tmp8, 1, 1, f);
	if (editor.timingMode == TEMPO_MODE_VBLANK)
	{
		editor.initialSpeed = tmp8;
	}
	else
	{
		if (tmp8 > 0x20) tmp8 = 0x20;
		editor.initialSpeed = tmp8;
	}

	// VU-Meter Colors
	fseek(f, 546, SEEK_SET);
	for (i = 0; i < 48; i++)
	{
		fread(&vuMeterColors[i], 2, 1, f); // stored as Big-Endian
		vuMeterColors[i] = SWAP16(vuMeterColors[i]);
	}

	// Spectrum Analyzer Colors
	fseek(f, 642, SEEK_SET);
	for (i = 0; i < 36; i++)
	{
		fread(&analyzerColors[i], 2, 1, f); // stored as Big-Endian
		analyzerColors[i] = SWAP16(analyzerColors[i]);
	}

	fclose(f);
	return true;
}

static uint8_t hex2int(char ch)
{
	ch = (char)toupper(ch);

	     if (ch >= 'A' && ch <= 'F') return 10 + (ch - 'A');
	else if (ch >= '0' && ch <= '9') return ch - '0';

	return 0; // not a hex
}

static bool loadColorsDotIni(void)
{
	char *configBuffer, *configLine;
	uint16_t color;
	uint32_t line, fileSize, lineLen;
	FILE *f;

	f = fopen("colors.ini", "r");
	if (f == NULL)
		return false;

	// get filesize
	fseek(f, 0, SEEK_END);
	fileSize = ftell(f);
	rewind(f);

	configBuffer = (char *)malloc(fileSize + 1);
	if (configBuffer == NULL)
	{
		fclose(f);
		showErrorMsgBox("Couldn't parse colors.ini: Out of memory!");
		return false;
	}

	fread(configBuffer, 1, fileSize, f);
	configBuffer[fileSize] = '\0';
	fclose(f);

	// do parsing
	configLine = strtok(configBuffer, "\n");
	while (configLine != NULL)
	{
		lineLen = (uint32_t)strlen(configLine);

		// read palette
		if (lineLen >= (sizeof ("[Palette]")-1))
		{
			if (!_strnicmp("[Palette]", configLine, sizeof ("[Palette]")-1))
			{
				configLine = strtok(NULL, "\n");

				line = 0;
				while (configLine != NULL && line < 8)
				{
					color = (hex2int(configLine[0]) << 8) | (hex2int(configLine[1]) << 4) | hex2int(configLine[2]);
					color &= 0xFFF;
					palette[line] = RGB12_to_RGB24(color);

					configLine = strtok(NULL, "\n");
					line++;
				}
			}

			if (configLine == NULL)
				break;

			lineLen = (uint32_t)strlen(configLine);
		}

		// read VU-meter colors
		if (lineLen >= sizeof ("[VU-meter]")-1)
		{
			if (!_strnicmp("[VU-meter]", configLine, sizeof ("[VU-meter]")-1))
			{
				configLine = strtok(NULL, "\n");

				line = 0;
				while (configLine != NULL && line < 48)
				{
					color = (hex2int(configLine[0]) << 8) | (hex2int(configLine[1]) << 4) | hex2int(configLine[2]);
					vuMeterColors[line] = color & 0xFFF;

					configLine = strtok(NULL, "\n");
					line++;
				}
			}

			if (configLine == NULL)
				break;

			lineLen = (uint32_t)strlen(configLine);
		}

		// read spectrum analyzer colors
		if (lineLen >= sizeof ("[SpectrumAnalyzer]")-1)
		{
			if (!_strnicmp("[SpectrumAnalyzer]", configLine, sizeof ("[SpectrumAnalyzer]")-1))
			{
				configLine = strtok(NULL, "\n");

				line = 0;
				while (configLine != NULL && line < 36)
				{
					color = (hex2int(configLine[0]) << 8) | (hex2int(configLine[1]) << 4) | hex2int(configLine[2]);
					analyzerColors[line] = color & 0xFFF;

					configLine = strtok(NULL, "\n");
					line++;
				}
			}

			if (configLine == NULL)
				break;
		}

		configLine = strtok(NULL, "\n");
	}

	free(configBuffer);
	return true;
}
