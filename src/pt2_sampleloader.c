// for finding memory leaks in debug mode with Visual Studio 
#if defined _DEBUG && defined _MSC_VER
#include <crtdbg.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h> // round()/roundf()
#include <ctype.h> // tolower()/toupper()
#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "pt2_header.h"
#include "pt2_textout.h"
#include "pt2_mouse.h"
#include "pt2_sampler.h"
#include "pt2_audio.h"
#include "pt2_sampleloader.h"
#include "pt2_visuals.h"
#include "pt2_helpers.h"
#include "pt2_unicode.h"

enum
{
	WAV_FORMAT_PCM = 0x0001,
	WAV_FORMAT_IEEE_FLOAT = 0x0003
};

static bool loadWAVSample(UNICHAR *fileName, char *entryName, int8_t forceDownSampling);
static bool loadIFFSample(UNICHAR *fileName, char *entryName);
static bool loadRAWSample(UNICHAR *fileName, char *entryName);
static bool loadAIFFSample(UNICHAR *fileName, char *entryName, int8_t forceDownSampling);

static bool loadedFileWasAIFF;

void extLoadWAVOrAIFFSampleCallback(bool downsample)
{
	if (loadedFileWasAIFF)
		loadAIFFSample(editor.fileNameTmpU, editor.entryNameTmp, downsample);
	else
		loadWAVSample(editor.fileNameTmpU, editor.entryNameTmp, downsample);
}

bool loadWAVSample(UNICHAR *fileName, char *entryName, int8_t forceDownSampling)
{
	bool wavSampleNameFound;
	uint8_t *audioDataU8;
	int16_t *audioDataS16, tempVol, smp16;
	uint16_t audioFormat, numChannels, bitsPerSample;
	int32_t *audioDataS32, smp32, smp32_l, smp32_r;
	uint32_t *audioDataU32, i, nameLen, chunkID, chunkSize;
	uint32_t sampleLength, sampleRate, filesize, loopFlags;
	uint32_t loopStart, loopEnd, dataPtr, dataLen, fmtPtr, endOfChunk, bytesRead;
	uint32_t fmtLen, inamPtr, inamLen, smplPtr, smplLen, xtraPtr, xtraLen;
	float *fAudioDataFloat, fSmp;
	double *dAudioDataDouble, dSmp;
	FILE *f;
	moduleSample_t *s;

	loadedFileWasAIFF = false;

	// zero out chunk pointers and lengths
	fmtPtr  = 0; fmtLen = 0;
	dataPtr = 0; dataLen = 0;
	inamPtr = 0; inamLen = 0;
	xtraPtr = 0; xtraLen = 0;
	smplPtr = 0; smplLen = 0;

	wavSampleNameFound = false;

	s = &modEntry->samples[editor.currSample];

	if (forceDownSampling == -1)
	{
		// these two *must* be fully wiped, for outputting reasons
		memset(editor.fileNameTmpU, 0, PATH_MAX);
		memset(editor.entryNameTmp, 0, PATH_MAX);
		UNICHAR_STRCPY(editor.fileNameTmpU, fileName);
		strcpy(editor.entryNameTmp, entryName);
	}

	f = UNICHAR_FOPEN(fileName, "rb");
	if (f == NULL)
	{
		displayErrorMsg("FILE I/O ERROR !");
		return false;
	}

	fseek(f, 0, SEEK_END);
	filesize = ftell(f);
	if (filesize == 0)
	{
		fclose(f);

		displayErrorMsg("NOT A WAV !");
		return false;
	}

	// look for wanted chunks and set up pointers + lengths
	fseek(f, 12, SEEK_SET);

	bytesRead = 0;
	while (!feof(f) && bytesRead < filesize-12)
	{
		fread(&chunkID, 4, 1, f); if (feof(f)) break;
		fread(&chunkSize, 4, 1, f); if (feof(f)) break;

		endOfChunk = (ftell(f) + chunkSize) + (chunkSize & 1);
		switch (chunkID)
		{
			case 0x20746D66: // "fmt "
			{
				fmtPtr = ftell(f);
				fmtLen = chunkSize;
			}
			break;

			case 0x61746164: // "data"
			{
				dataPtr = ftell(f);
				dataLen = chunkSize;
			}
			break;

			case 0x5453494C: // "LIST"
			{
				if (chunkSize >= 4)
				{
					fread(&chunkID, 4, 1, f);
					if (chunkID == 0x4F464E49) // "INFO"
					{
						bytesRead = 0;
						while (!feof(f) && (bytesRead < chunkSize))
						{
							fread(&chunkID, 4, 1, f);
							fread(&chunkSize, 4, 1, f);

							switch (chunkID)
							{
								case 0x4D414E49: // "INAM"
								{
									inamPtr = ftell(f);
									inamLen = chunkSize;
								}
								break;

								default: break;
							}

							bytesRead += (chunkSize + (chunkSize & 1));
						}
					}
				}
			}
			break;

			case 0x61727478: // "xtra"
			{
				xtraPtr = ftell(f);
				xtraLen = chunkSize;
			}
			break;

			case 0x6C706D73: // "smpl"
			{
				smplPtr = ftell(f);
				smplLen = chunkSize;
			}
			break;

			default: break;
		}

		bytesRead += (chunkSize + (chunkSize & 1));
		fseek(f, endOfChunk, SEEK_SET);
	}

	// we need at least "fmt " and "data" - check if we found them sanely
	if ((fmtPtr == 0 || fmtLen < 16) || (dataPtr == 0 || dataLen == 0))
	{
		fclose(f);
		displayErrorMsg("NOT A WAV !");
		return false;
	}

	// ---- READ "fmt " CHUNK ----
	fseek(f, fmtPtr, SEEK_SET);
	fread(&audioFormat, 2, 1, f);
	fread(&numChannels, 2, 1, f);
	fread(&sampleRate,  4, 1, f);
	fseek(f, 6, SEEK_CUR);
	fread(&bitsPerSample, 2, 1, f);
	sampleLength = dataLen;
	// ---------------------------

	if (sampleRate == 0 || sampleLength == 0 || sampleLength >= filesize*(bitsPerSample/8))
	{
		fclose(f);
		displayErrorMsg("WAV CORRUPT !");
		return false;
	}

	if (audioFormat != WAV_FORMAT_PCM && audioFormat != WAV_FORMAT_IEEE_FLOAT)
	{
		fclose(f);
		displayErrorMsg("WAV UNSUPPORTED !");
		return false;
	}

	if ((numChannels == 0) || (numChannels > 2))
	{
		fclose(f);
		displayErrorMsg("WAV UNSUPPORTED !");
		return false;
	}

	if (audioFormat == WAV_FORMAT_IEEE_FLOAT && bitsPerSample != 32 && bitsPerSample != 64)
	{
		fclose(f);
		displayErrorMsg("WAV UNSUPPORTED !");
		return false;
	}

	if (bitsPerSample != 8 && bitsPerSample != 16 && bitsPerSample != 24 && bitsPerSample != 32 && bitsPerSample != 64)
	{
		fclose(f);
		displayErrorMsg("WAV UNSUPPORTED !");
		return false;
	}

	if (sampleRate > 22050)
	{
		if (forceDownSampling == -1)
		{
			editor.ui.askScreenShown = true;
			editor.ui.askScreenType = ASK_DOWNSAMPLING;
			pointerSetMode(POINTER_MODE_MSG1, NO_CARRY);
			setStatusMessage("2X DOWNSAMPLING ?", NO_CARRY);
			renderAskDialog();
			fclose(f);
			return true;
		}
	}
	else
	{
		forceDownSampling = false;
	}

	// ---- READ SAMPLE DATA ----
	fseek(f, dataPtr, SEEK_SET);

	if (bitsPerSample == 8) // 8-BIT INTEGER SAMPLE
	{
		if (sampleLength > MAX_SAMPLE_LEN*4)
			sampleLength = MAX_SAMPLE_LEN*4;

		audioDataU8 = (uint8_t *)malloc(sampleLength * sizeof (uint8_t));
		if (audioDataU8 == NULL)
		{
			fclose(f);
			statusOutOfMemory();
			return false;
		}

		// read sample data
		if (fread(audioDataU8, 1, sampleLength, f) != sampleLength)
		{
			fclose(f);
			free(audioDataU8);
			displayErrorMsg("I/O ERROR !");
			return false;
		}

		// convert from stereo to mono (if needed)
		if (numChannels == 2)
		{
			sampleLength /= 2;
			for (i = 0; i < sampleLength-1; i++) // add right channel to left channel
			{
				smp16 = (audioDataU8[(i * 2) + 0] - 128) + (audioDataU8[(i * 2) + 1] - 128);
				smp16 = 128 + (smp16 >> 1);
				audioDataU8[i] = (uint8_t)smp16;
			}
		}

		// 2x downsampling - remove every other sample (if needed)
		if (forceDownSampling)
		{
			sampleLength /= 2;
			for (i = 1; i < sampleLength; i++)
				audioDataU8[i] = audioDataU8[i * 2];
		}

		if (sampleLength > MAX_SAMPLE_LEN)
			sampleLength = MAX_SAMPLE_LEN;

		turnOffVoices();
		for (i = 0; i < MAX_SAMPLE_LEN; i++)
		{
			if (i <= (sampleLength & 0xFFFFFFFE))
				modEntry->sampleData[(editor.currSample * MAX_SAMPLE_LEN) + i] = audioDataU8[i] - 128;
			else
				modEntry->sampleData[(editor.currSample * MAX_SAMPLE_LEN) + i] = 0; // clear rest of sample
		}

		free(audioDataU8);
	}
	else if (bitsPerSample == 16) // 16-BIT INTEGER SAMPLE
	{
		sampleLength /= 2;
		if (sampleLength > MAX_SAMPLE_LEN*4)
			sampleLength = MAX_SAMPLE_LEN*4;

		audioDataS16 = (int16_t *)malloc(sampleLength * sizeof (int16_t));
		if (audioDataS16 == NULL)
		{
			fclose(f);
			statusOutOfMemory();
			return false;
		}

		// read sample data
		if (fread(audioDataS16, 2, sampleLength, f) != sampleLength)
		{
			fclose(f);
			free(audioDataS16);
			displayErrorMsg("I/O ERROR !");
			return false;
		}

		// convert from stereo to mono (if needed)
		if (numChannels == 2)
		{
			sampleLength /= 2;
			for (i = 0; i < sampleLength-1; i++) // add right channel to left channel
			{
				smp32 = (audioDataS16[(i * 2) + 0] + audioDataS16[(i * 2) + 1]) >> 1;
				audioDataS16[i] = (int16_t)smp32;
			}
		}

		// 2x downsampling - remove every other sample (if needed)
		if (forceDownSampling)
		{
			sampleLength /= 2;
			for (i = 1; i < sampleLength; i++)
				audioDataS16[i] = audioDataS16[i * 2];
		}

		if (sampleLength > MAX_SAMPLE_LEN)
			sampleLength = MAX_SAMPLE_LEN;

		normalize16bitSigned(audioDataS16, sampleLength);

		turnOffVoices();
		for (i = 0; i < MAX_SAMPLE_LEN; i++)
		{
			if (i <= (sampleLength & 0xFFFFFFFE))
				modEntry->sampleData[(editor.currSample * MAX_SAMPLE_LEN) + i] = audioDataS16[i] >> 8;
			else
				modEntry->sampleData[(editor.currSample * MAX_SAMPLE_LEN) + i] = 0; // clear rest of sample
		}

		free(audioDataS16);
	}
	else if (bitsPerSample == 24) // 24-BIT INTEGER SAMPLE
	{
		sampleLength /= 3;
		if (sampleLength > MAX_SAMPLE_LEN*4)
			sampleLength = MAX_SAMPLE_LEN*4;

		audioDataS32 = (int32_t *)malloc(sampleLength * sizeof (int32_t));
		if (audioDataS32 == NULL)
		{
			fclose(f);
			statusOutOfMemory();
			return false;
		}

		// read sample data
		audioDataU8 = (uint8_t *)audioDataS32;
		for (i = 0; i < sampleLength; i++)
		{
			audioDataU8[0] = 0;
			fread(&audioDataU8[1], 3, 1, f);
			audioDataU8 += sizeof (int32_t);
		}

		// convert from stereo to mono (if needed)
		if (numChannels == 2)
		{
			sampleLength /= 2;
			for (i = 0; i < sampleLength-1; i++) // add right channel to left channel
			{
				smp32_l = audioDataS32[(i * 2) + 0] >> 1;
				smp32_r = audioDataS32[(i * 2) + 1] >> 1;
				audioDataS32[i] = smp32_l + smp32_r;
			}
		}

		// 2x downsampling - remove every other sample (if needed)
		if (forceDownSampling)
		{
			sampleLength /= 2;
			for (i = 1; i < sampleLength; i++)
				audioDataS32[i] = audioDataS32[i * 2];
		}

		if (sampleLength > MAX_SAMPLE_LEN)
			sampleLength = MAX_SAMPLE_LEN;

		normalize32bitSigned(audioDataS32, sampleLength);

		turnOffVoices();
		for (i = 0; i < MAX_SAMPLE_LEN; i++)
		{
			if (i <= (sampleLength & 0xFFFFFFFE))
				modEntry->sampleData[(editor.currSample * MAX_SAMPLE_LEN) + i] = (int8_t)(audioDataS32[i] >> 24);
			else
				modEntry->sampleData[(editor.currSample * MAX_SAMPLE_LEN) + i] = 0; // clear rest of sample
		}

		free(audioDataS32);
	}
	else if (audioFormat == WAV_FORMAT_PCM && bitsPerSample == 32) // 32-BIT INTEGER SAMPLE
	{
		sampleLength /= 4;
		if (sampleLength > MAX_SAMPLE_LEN*4)
			sampleLength = MAX_SAMPLE_LEN*4;

		audioDataS32 = (int32_t *)malloc(sampleLength * sizeof (int32_t));
		if (audioDataS32 == NULL)
		{
			fclose(f);
			statusOutOfMemory();
			return false;
		}

		// read sample data
		if (fread(audioDataS32, 4, sampleLength, f) != sampleLength)
		{
			fclose(f);
			free(audioDataS32);
			displayErrorMsg("I/O ERROR !");
			return false;
		}

		// convert from stereo to mono (if needed)
		if (numChannels == 2)
		{
			sampleLength /= 2;
			for (i = 0; i < sampleLength-1; i++) // add right channel to left channel
			{
				smp32_l = audioDataS32[(i * 2) + 0] >> 1;
				smp32_r = audioDataS32[(i * 2) + 1] >> 1;

				audioDataS32[i] = smp32_l + smp32_r;
			}
		}

		// 2x downsampling - remove every other sample (if needed)
		if (forceDownSampling)
		{
			sampleLength /= 2;
			for (i = 1; i < sampleLength; i++)
				audioDataS32[i] = audioDataS32[i * 2];
		}

		if (sampleLength > MAX_SAMPLE_LEN)
			sampleLength = MAX_SAMPLE_LEN;

		normalize32bitSigned(audioDataS32, sampleLength);

		turnOffVoices();
		for (i = 0; i < MAX_SAMPLE_LEN; i++)
		{
			if (i <= (sampleLength & 0xFFFFFFFE))
				modEntry->sampleData[(editor.currSample * MAX_SAMPLE_LEN) + i] = (int8_t)(audioDataS32[i] >> 24);
			else
				modEntry->sampleData[(editor.currSample * MAX_SAMPLE_LEN) + i] = 0; // clear rest of sample
		}

		free(audioDataS32);
	}
	else if (audioFormat == WAV_FORMAT_IEEE_FLOAT && bitsPerSample == 32) // 32-BIT FLOATING POINT SAMPLE
	{
		sampleLength /= 4;
		if (sampleLength > MAX_SAMPLE_LEN*4)
			sampleLength = MAX_SAMPLE_LEN*4;

		audioDataU32 = (uint32_t *)malloc(sampleLength * sizeof (uint32_t));
		if (audioDataU32 == NULL)
		{
			fclose(f);
			statusOutOfMemory();
			return false;
		}

		// read sample data
		if (fread(audioDataU32, 4, sampleLength, f) != sampleLength)
		{
			fclose(f);
			free(audioDataU32);
			displayErrorMsg("I/O ERROR !");
			return false;
		}

		fAudioDataFloat = (float *)audioDataU32;

		// convert from stereo to mono (if needed)
		if (numChannels == 2)
		{
			sampleLength /= 2;
			for (i = 0; i < sampleLength-1; i++) // add right channel to left channel
			{
				fSmp = (fAudioDataFloat[(i * 2) + 0] + fAudioDataFloat[(i * 2) + 1]) * 0.5f;
				fAudioDataFloat[i] = fSmp;
			}
		}

		// 2x downsampling - remove every other sample (if needed)
		if (forceDownSampling)
		{
			sampleLength /= 2;
			for (i = 1; i < sampleLength; i++)
				fAudioDataFloat[i] = fAudioDataFloat[i * 2];
		}

		if (sampleLength > MAX_SAMPLE_LEN)
			sampleLength = MAX_SAMPLE_LEN;

		normalize8bitFloatSigned(fAudioDataFloat, sampleLength);

		turnOffVoices();
		for (i = 0; i < MAX_SAMPLE_LEN; i++)
		{
			if (i <= (sampleLength & 0xFFFFFFFE))
			{
				smp32 = (int32_t)fAudioDataFloat[i];
				CLAMP8(smp32);
				modEntry->sampleData[(editor.currSample * MAX_SAMPLE_LEN) + i] = (int8_t)smp32;
			}
			else
			{
				modEntry->sampleData[(editor.currSample * MAX_SAMPLE_LEN) + i] = 0; // clear rest of sample
			}
		}

		free(audioDataU32);
	}
	else if (audioFormat == WAV_FORMAT_IEEE_FLOAT && bitsPerSample == 64) // 64-BIT FLOATING POINT SAMPLE
	{
		sampleLength /= 8;
		if (sampleLength > MAX_SAMPLE_LEN*4)
			sampleLength = MAX_SAMPLE_LEN*4;

		audioDataU32 = (uint32_t *)malloc(sampleLength * (sizeof (uint32_t) * 2));
		if (audioDataU32 == NULL)
		{
			fclose(f);
			statusOutOfMemory();
			return false;
		}

		// read sample data
		if (fread(audioDataU32, 8, sampleLength, f) != sampleLength)
		{
			fclose(f);
			free(audioDataU32);
			displayErrorMsg("I/O ERROR !");
			return false;
		}

		dAudioDataDouble = (double *)audioDataU32;

		// convert from stereo to mono (if needed)
		if (numChannels == 2)
		{
			sampleLength /= 2;
			for (i = 0; i < sampleLength-1; i++) // add right channel to left channel
			{
				dSmp = (dAudioDataDouble[(i * 2) + 0] + dAudioDataDouble[(i * 2) + 1]) * 0.5;
				dAudioDataDouble[i] = dSmp;
			}
		}

		// 2x downsampling - remove every other sample (if needed)
		if (forceDownSampling)
		{
			sampleLength /= 2;
			for (i = 1; i < sampleLength; i++)
				dAudioDataDouble[i] = dAudioDataDouble[i * 2];
		}

		if (sampleLength > MAX_SAMPLE_LEN)
			sampleLength = MAX_SAMPLE_LEN;

		normalize8bitDoubleSigned(dAudioDataDouble, sampleLength);

		turnOffVoices();
		for (i = 0; i < MAX_SAMPLE_LEN; i++)
		{
			if (i <= (sampleLength & 0xFFFFFFFE))
			{
				smp32 = (int32_t)dAudioDataDouble[i];
				CLAMP8(smp32);
				modEntry->sampleData[(editor.currSample * MAX_SAMPLE_LEN) + i] = (int8_t)smp32;
			}
			else
			{
				modEntry->sampleData[(editor.currSample * MAX_SAMPLE_LEN) + i] = 0; // clear rest of sample
			}
		}

		free(audioDataU32);
	}

	// set sample length
	if (sampleLength & 1)
	{
		if (++sampleLength > MAX_SAMPLE_LEN)
			sampleLength = MAX_SAMPLE_LEN;
	}

	s->length = sampleLength;
	s->fineTune = 0;
	s->volume = 64;
	s->loopStart = 0;
	s->loopLength = 2;

	// ---- READ "smpl" chunk ----
	if (smplPtr != 0 && smplLen > 52)
	{
		fseek(f, smplPtr + 28, SEEK_SET); // seek to first wanted byte

		fread(&loopFlags, 4, 1, f);
		fseek(f, 12, SEEK_CUR);
		fread(&loopStart, 4, 1, f);
		fread(&loopEnd, 4, 1, f);
		loopEnd++;

		if (forceDownSampling)
		{
			// we already downsampled 2x, so we're half the original length
			loopStart /= 2;
			loopEnd /= 2;
		}

		loopStart &= 0xFFFFFFFE;
		loopEnd &= 0xFFFFFFFE;

		if (loopFlags)
		{
			if (loopStart+(loopEnd-loopStart) <= s->length)
			{
				s->loopStart = loopStart;
				s->loopLength = loopEnd - loopStart;

				if (s->loopLength < 2)
				{
					s->loopStart = 0;
					s->loopLength = 2;
				}
			}
		}
	}
	// ---------------------------

	// ---- READ "xtra" chunk ----
	if (xtraPtr != 0 && xtraLen >= 8)
	{
		fseek(f, xtraPtr + 4, SEEK_SET); // seek to first wanted byte

		// volume (0..256)
		fseek(f, 2, SEEK_CUR);
		fread(&tempVol, 2, 1, f);
		if (tempVol > 256)
			tempVol = 256;

		s->volume = (int8_t)(tempVol / 4);
	}
	// ---------------------------

	// ---- READ "INAM" chunk ----
	if (inamPtr != 0 && inamLen > 0)
	{
		fseek(f, inamPtr, SEEK_SET); // seek to first wanted byte

		for (i = 0; i < 21; i++)
		{
			if (i < inamLen)
				s->text[i] = (char)toupper(fgetc(f));
			else
				s->text[i] = '\0';
		}

		s->text[21] = '\0';
		s->text[22] = '\0';

		wavSampleNameFound = true;
	}
	// ---------------------------

	fclose(f);

	// copy over sample name
	if (!wavSampleNameFound)
	{
		nameLen = (uint32_t)strlen(entryName);
		for (i = 0; i < 21; i++)
			s->text[i] = (i < nameLen) ? toupper(entryName[i]) : '\0';

		s->text[21] = '\0';
		s->text[22] = '\0';
	}

	// remove .wav from end of sample name (if present)
	nameLen = (uint32_t)strlen(s->text);
	if (nameLen >= 4 && !_strnicmp(&s->text[nameLen-4], ".WAV", 4))
		 memset(&s->text[nameLen-4], '\0', 4);

	editor.sampleZero = false;
	editor.samplePos = 0;

	fixSampleBeep(s);
	updateCurrSample();

	updateWindowTitle(MOD_IS_MODIFIED);
	return true;
}

bool loadIFFSample(UNICHAR *fileName, char *entryName)
{
	bool nameFound, is16Bit;
	char tmpCharBuf[23];
	int8_t *sampleData;
	int16_t sample16, *ptr16;
	int32_t filesize;
	uint32_t i, sampleLength, sampleLoopStart, sampleLoopLength;
	uint32_t sampleVolume, blockName, blockSize;
	uint32_t vhdrPtr, vhdrLen, bodyPtr, bodyLen, namePtr, nameLen;
	FILE *f;
	moduleSample_t *s;

	s = &modEntry->samples[editor.currSample];

	vhdrPtr = 0; vhdrLen = 0;
	bodyPtr = 0; bodyLen = 0;
	namePtr = 0; nameLen = 0;

	f = UNICHAR_FOPEN(fileName, "rb");
	if (f == NULL)
	{
		displayErrorMsg("FILE I/O ERROR !");
		return false;
	}

	fseek(f, 0, SEEK_END);
	filesize = ftell(f);
	if (filesize == 0)
	{
		displayErrorMsg("IFF IS CORRUPT !");
		return false;
	}

	fseek(f, 8, SEEK_SET);
	fread(tmpCharBuf, 1, 4, f);
	is16Bit = !strncmp(tmpCharBuf, "16SV", 4);

	sampleLength = 0;
	nameFound = false;
	sampleVolume = 65536; // max volume

	fseek(f, 12, SEEK_SET);
	while (!feof(f) && ftell(f) < filesize-12)
	{
		fread(&blockName, 4, 1, f); if (feof(f)) break;
		fread(&blockSize, 4, 1, f); if (feof(f)) break;

		blockName = SWAP32(blockName);
		blockSize = SWAP32(blockSize);

		switch (blockName)
		{
			case 0x56484452: // VHDR
			{
				vhdrPtr = ftell(f);
				vhdrLen = blockSize;
			}
			break;

			case 0x4E414D45: // NAME
			{
				namePtr = ftell(f);
				nameLen = blockSize;
			}
			break;

			case 0x424F4459: // BODY
			{
				bodyPtr = ftell(f);
				bodyLen = blockSize;
			}
			break;

			default: break;
		}

		fseek(f, blockSize + (blockSize & 1), SEEK_CUR);
	}

	if (vhdrPtr == 0 || vhdrLen < 20 || bodyPtr == 0)
	{
		fclose(f);
		displayErrorMsg("NOT A VALID IFF !");
		return false;
	}

	// kludge for some really strange IFFs
	if (bodyLen == 0)
		bodyLen = filesize - bodyPtr;

	if ((bodyPtr + bodyLen) > (uint32_t)filesize)
		bodyLen = filesize - bodyPtr;

	fseek(f, vhdrPtr, SEEK_SET);
	fread(&sampleLoopStart,  4, 1, f); sampleLoopStart  = SWAP32(sampleLoopStart);
	fread(&sampleLoopLength, 4, 1, f); sampleLoopLength = SWAP32(sampleLoopLength);

	fseek(f, 4 + 2 + 1, SEEK_CUR);

	if (fgetc(f) != 0) // sample type
	{
		fclose(f);
		displayErrorMsg("UNSUPPORTED IFF !");
		return false;
	}

	fread(&sampleVolume, 4, 1, f); sampleVolume = SWAP32(sampleVolume);
	if (sampleVolume > 65536)
		sampleVolume = 65536;

	sampleVolume = (uint32_t)round(sampleVolume / 1024.0);
	if (sampleVolume > 64)
		sampleVolume = 64;

	sampleLength = bodyLen;
	if (is16Bit)
	{
		if (sampleLength > MAX_SAMPLE_LEN*2)
			sampleLength = MAX_SAMPLE_LEN*2;
	}
	else
	{
		if (sampleLength > MAX_SAMPLE_LEN)
			sampleLength = MAX_SAMPLE_LEN;
	}

	if (sampleLength == 0)
	{
		fclose(f);
		displayErrorMsg("NOT A VALID IFF !");
		return false;
	}

	sampleData = (int8_t *)malloc(sampleLength);
	if (sampleData == NULL)
	{
		fclose(f);
		statusOutOfMemory();
		return false;
	}

	if (is16Bit)
	{
		sampleLength /= 2;
		sampleLoopStart /= 2;
		sampleLoopLength /= 2;
	}

	sampleLength &= 0xFFFFFFFE;
	sampleLoopStart &= 0xFFFFFFFE;
	sampleLoopLength &= 0xFFFFFFFE;

	if (sampleLength > MAX_SAMPLE_LEN)
		sampleLength = MAX_SAMPLE_LEN;

	if (sampleLoopLength < 2)
	{
		sampleLoopStart = 0;
		sampleLoopLength = 2;
	}

	if (sampleLoopStart >= MAX_SAMPLE_LEN || sampleLoopLength > MAX_SAMPLE_LEN)
	{
		sampleLoopStart= 0;
		sampleLoopLength = 2;
	}

	if (sampleLoopStart+sampleLoopLength > sampleLength)
	{
		sampleLoopStart = 0;
		sampleLoopLength = 2;
	}

	if (sampleLoopStart > sampleLength-2)
	{
		sampleLoopStart = 0;
		sampleLoopLength = 2;
	}

	turnOffVoices();

	fseek(f, bodyPtr, SEEK_SET);
	if (is16Bit) // FT2 specific 16SV format (little-endian samples)
	{
		fread(sampleData, 1, sampleLength * 2, f);

		ptr16 = (int16_t *)sampleData;
		for (i = 0; i < sampleLength; i++)
		{
			sample16 = ptr16[i];
			modEntry->sampleData[s->offset+i] = sample16 >> 8;
		}
	}
	else
	{
		fread(sampleData, 1, sampleLength, f);
		memcpy(&modEntry->sampleData[s->offset], sampleData, sampleLength);
	}

	if (sampleLength < MAX_SAMPLE_LEN) // clear rest of sample data
		memset(&modEntry->sampleData[s->offset + sampleLength], 0, MAX_SAMPLE_LEN - sampleLength);

	free(sampleData);

	// set sample attributes
	s->volume = sampleVolume;
	s->fineTune = 0;
	s->length = sampleLength;
	s->loopStart = sampleLoopStart;
	s->loopLength = sampleLoopLength;

	// read name
	if (namePtr != 0 && nameLen > 0)
	{
		fseek(f, namePtr, SEEK_SET);
		memset(tmpCharBuf, 0, sizeof (tmpCharBuf));

		if (nameLen > 21)
		{
			fread(tmpCharBuf, 1, 21, f);
			fseek(f, nameLen - 21, SEEK_CUR);
		}
		else
		{
			fread(tmpCharBuf, 1, nameLen, f);
		}

		nameFound = true;
	}

	fclose(f);

	// copy over sample name
	memset(s->text, '\0', sizeof (s->text));

	if (nameFound)
	{
		nameLen = (uint32_t)strlen(tmpCharBuf);
		if (nameLen > 21)
			nameLen = 21;

		for (i = 0; i < nameLen; i++)
			s->text[i] = toupper(tmpCharBuf[i]);
	}
	else
	{
		nameLen = (uint32_t)strlen(entryName);
		if (nameLen > 21)
			nameLen = 21;

		for (i = 0; i < nameLen; i++)
			s->text[i] = toupper(entryName[i]);
	}

	// remove .iff from end of sample name (if present)
	nameLen = (uint32_t)strlen(s->text);
	if (nameLen >= 4 && !strncmp(&s->text[nameLen-4], ".IFF", 4))
		memset(&s->text[nameLen-4], '\0', 4);

	editor.sampleZero = false;
	editor.samplePos = 0;

	fixSampleBeep(s);
	updateCurrSample();

	updateWindowTitle(MOD_IS_MODIFIED);
	return false;
}

bool loadRAWSample(UNICHAR *fileName, char *entryName)
{
	uint8_t i;
	uint32_t nameLen, fileSize;
	FILE *f;
	moduleSample_t *s;

	s = &modEntry->samples[editor.currSample];

	f = UNICHAR_FOPEN(fileName, "rb");
	if (f == NULL)
	{
		displayErrorMsg("FILE I/O ERROR !");
		return false;
	}

	fseek(f, 0, SEEK_END);
	fileSize = ftell(f);
	fseek(f, 0, SEEK_SET);

	fileSize &= 0xFFFFFFFE;
	if (fileSize > MAX_SAMPLE_LEN)
		fileSize = MAX_SAMPLE_LEN;

	turnOffVoices();

	fread(&modEntry->sampleData[s->offset], 1, fileSize, f);
	fclose(f);

	if (fileSize < MAX_SAMPLE_LEN)
		memset(&modEntry->sampleData[s->offset + fileSize], 0, MAX_SAMPLE_LEN - fileSize);

	// set sample attributes
	s->volume = 64;
	s->fineTune = 0;
	s->length = fileSize;
	s->loopStart = 0;
	s->loopLength = 2;

	// copy over sample name
	nameLen = (uint32_t)strlen(entryName);
	for (i = 0; i < 21; i++)
		s->text[i] = (i < nameLen) ? toupper(entryName[i]) : '\0';

	s->text[21] = '\0';
	s->text[22] = '\0';

	editor.sampleZero = false;
	editor.samplePos = 0;

	fixSampleBeep(s);
	updateCurrSample();

	updateWindowTitle(MOD_IS_MODIFIED);
	return true;
}

static int32_t getAIFFRate(uint8_t *in)
{
	int32_t exp;
	uint32_t lo, hi;
	double dOut;

	exp = (int32_t)(((in[0] & 0x7F) << 8) | in[1]);
	lo  = (in[2] << 24) | (in[3] << 16) | (in[4] << 8) | in[5];
	hi  = (in[6] << 24) | (in[7] << 16) | (in[8] << 8) | in[9];

	if (exp == 0 && lo == 0 && hi == 0)
		return 0;

	exp -= 16383;

	dOut = ldexp(lo, -31 + exp) + ldexp(hi, -63 + exp);
	return (int32_t)round(dOut);
}

bool loadAIFFSample(UNICHAR *fileName, char *entryName, int8_t forceDownSampling)
{
	char compType[4];
	int8_t *audioDataS8;
	uint8_t *audioDataU8, sampleRateBytes[10];
	int16_t *audioDataS16, smp16;
	uint16_t bitDepth, numChannels;
	int32_t filesize, *audioDataS32, smp32, smp32_l, smp32_r;
	uint32_t nameLen, i, offset, sampleRate, sampleLength, blockName, blockSize;
	uint32_t commPtr, commLen, ssndPtr, ssndLen;
	FILE *f;
	moduleSample_t *s;

	loadedFileWasAIFF = true;

	if (forceDownSampling == -1)
	{
		// these two *must* be fully wiped, for outputting reasons
		memset(editor.fileNameTmpU, 0, PATH_MAX);
		memset(editor.entryNameTmp, 0, PATH_MAX);
		UNICHAR_STRCPY(editor.fileNameTmpU, fileName);
		strcpy(editor.entryNameTmp, entryName);
	}

	s = &modEntry->samples[editor.currSample];

	commPtr = 0; commLen = 0;
	ssndPtr = 0; ssndLen = 0;

	f = UNICHAR_FOPEN(fileName, "rb");
	if (f == NULL)
	{
		displayErrorMsg("FILE I/O ERROR !");
		return false;
	}

	fseek(f, 0, SEEK_END);
	filesize = ftell(f);
	if (filesize == 0)
	{
		displayErrorMsg("AIFF IS CORRUPT !");
		return false;
	}

	fseek(f, 12, SEEK_SET);
	while (!feof(f) && ftell(f) < filesize-12)
	{
		fread(&blockName, 4, 1, f); if (feof(f)) break;
		fread(&blockSize, 4, 1, f); if (feof(f)) break;

		blockName = SWAP32(blockName);
		blockSize = SWAP32(blockSize);

		switch (blockName)
		{
			case 0x434F4D4D: // "COMM"
			{
				commPtr = ftell(f);
				commLen = blockSize;
			}
			break;

			case 0x53534E44: // "SSND"
			{
				ssndPtr = ftell(f);
				ssndLen = blockSize;
			}
			break;

			default: break;
		}

		fseek(f, blockSize + (blockSize & 1), SEEK_CUR);
	}

	if (commPtr == 0 || commLen < 18 || ssndPtr == 0)
	{
		fclose(f);
		displayErrorMsg("NOT A VALID AIFF!");
		return false;
	}

	// kludge for some really strange AIFFs
	if (ssndLen == 0)
		ssndLen = filesize - ssndPtr;

	if (ssndPtr+ssndLen > (uint32_t)filesize)
		ssndLen = filesize - ssndPtr;

	fseek(f, commPtr, SEEK_SET);
	fread(&numChannels, 2, 1, f); numChannels = SWAP16(numChannels);
	fseek(f, 4, SEEK_CUR);
	fread(&bitDepth, 2, 1, f); bitDepth = SWAP16(bitDepth);
	fread(sampleRateBytes, 1, 10, f);

	fseek(f, 4 + 2 + 1, SEEK_CUR);

	if (numChannels != 1 && numChannels != 2) // sample type
	{
		fclose(f);
		displayErrorMsg("UNSUPPORTED AIFF!");
		return false;
	}

	if (bitDepth != 8 && bitDepth != 16 && bitDepth != 24 && bitDepth != 32)
	{
		fclose(f);
		displayErrorMsg("UNSUPPORTED AIFF!");
		return false;
	}

	// read compression type (if present)
	if (commLen > 18)
	{
		fread(&compType, 1, 4, f);
		if (memcmp(compType, "NONE", 4))
		{
			fclose(f);
			displayErrorMsg("UNSUPPORTED AIFF!");
			return false;
		}
	}

	sampleRate = getAIFFRate(sampleRateBytes);

	// sample data chunk

	fseek(f, ssndPtr, SEEK_SET);

	fread(&offset, 4, 1, f);
	if (offset > 0)
	{
		fclose(f);
		displayErrorMsg("UNSUPPORTED AIFF!");
		return false;
	}

	fseek(f, 4, SEEK_CUR);

	ssndLen -= 8; // don't include offset and blockSize datas

	sampleLength = ssndLen;
	if (sampleLength == 0)
	{
		fclose(f);
		displayErrorMsg("NOT A VALID AIFF!");
		return false;
	}

	if (sampleRate > 22050)
	{
		if (forceDownSampling == -1)
		{
			editor.ui.askScreenShown = true;
			editor.ui.askScreenType = ASK_DOWNSAMPLING;
			pointerSetMode(POINTER_MODE_MSG1, NO_CARRY);
			setStatusMessage("2X DOWNSAMPLING ?", NO_CARRY);
			renderAskDialog();
			fclose(f);
			return true;
		}
	}
	else
	{
		forceDownSampling = false;
	}

	if (bitDepth == 8) // 8-BIT INTEGER SAMPLE
	{
		if (sampleLength > MAX_SAMPLE_LEN*4)
			sampleLength = MAX_SAMPLE_LEN*4;

		audioDataS8 = (int8_t *)malloc(sampleLength * sizeof (int8_t));
		if (audioDataS8 == NULL)
		{
			fclose(f);
			statusOutOfMemory();
			return false;
		}

		// read sample data
		if (fread(audioDataS8, 1, sampleLength, f) != sampleLength)
		{
			fclose(f);
			free(audioDataS8);
			displayErrorMsg("I/O ERROR !");
			return false;
		}

		// convert from stereo to mono (if needed)
		if (numChannels == 2)
		{
			sampleLength /= 2;
			for (i = 0; i < sampleLength-1; i++) // add right channel to left channel
			{
				smp16 = (audioDataS8[(i * 2) + 0] + audioDataS8[(i * 2) + 1]) >> 1;
				audioDataS8[i] = (uint8_t)smp16;
			}
		}

		// 2x downsampling - remove every other sample (if needed)
		if (forceDownSampling)
		{
			sampleLength /= 2;
			for (i = 1; i < sampleLength; i++)
				audioDataS8[i] = audioDataS8[i * 2];
		}

		if (sampleLength > MAX_SAMPLE_LEN)
			sampleLength = MAX_SAMPLE_LEN;

		turnOffVoices();
		for (i = 0; i < MAX_SAMPLE_LEN; i++)
		{
			if (i <= (sampleLength & 0xFFFFFFFE))
				modEntry->sampleData[(editor.currSample * MAX_SAMPLE_LEN) + i] = audioDataS8[i];
			else
				modEntry->sampleData[(editor.currSample * MAX_SAMPLE_LEN) + i] = 0; // clear rest of sample
		}

		free(audioDataS8);
	}
	else if (bitDepth == 16) // 16-BIT INTEGER SAMPLE
	{
		sampleLength /= 2;
		if (sampleLength > MAX_SAMPLE_LEN*4)
			sampleLength = MAX_SAMPLE_LEN*4;

		audioDataS16 = (int16_t *)malloc(sampleLength * sizeof (int16_t));
		if (audioDataS16 == NULL)
		{
			fclose(f);
			statusOutOfMemory();
			return false;
		}

		// read sample data
		if (fread(audioDataS16, 2, sampleLength, f) != sampleLength)
		{
			fclose(f);
			free(audioDataS16);
			displayErrorMsg("I/O ERROR !");
			return false;
		}

		// fix endianness
		for (i = 0; i < sampleLength; i++)
			audioDataS16[i] = SWAP16(audioDataS16[i]);

		// convert from stereo to mono (if needed)
		if (numChannels == 2)
		{
			sampleLength /= 2;
			for (i = 0; i < sampleLength-1; i++) // add right channel to left channel
			{
				smp32 = (audioDataS16[(i * 2) + 0] + audioDataS16[(i * 2) + 1]) >> 1;
				audioDataS16[i] = (int16_t)(smp32);
			}
		}

		// 2x downsampling - remove every other sample (if needed)
		if (forceDownSampling)
		{
			sampleLength /= 2;
			for (i = 1; i < sampleLength; i++)
				audioDataS16[i] = audioDataS16[i * 2];
		}

		if (sampleLength > MAX_SAMPLE_LEN)
			sampleLength = MAX_SAMPLE_LEN;

		normalize16bitSigned(audioDataS16, sampleLength);

		turnOffVoices();
		for (i = 0; i < MAX_SAMPLE_LEN; i++)
		{
			if (i <= (sampleLength & 0xFFFFFFFE))
				modEntry->sampleData[(editor.currSample * MAX_SAMPLE_LEN) + i] = audioDataS16[i] >> 8;
			else
				modEntry->sampleData[(editor.currSample * MAX_SAMPLE_LEN) + i] = 0; // clear rest of sample
		}

		free(audioDataS16);
	}
	else if (bitDepth == 24) // 24-BIT INTEGER SAMPLE
	{
		sampleLength /= 3;
		if (sampleLength > MAX_SAMPLE_LEN*4)
			sampleLength = MAX_SAMPLE_LEN*4;

		audioDataS32 = (int32_t *)malloc((sampleLength * 2) * sizeof (int32_t));
		if (audioDataS32 == NULL)
		{
			fclose(f);
			statusOutOfMemory();
			return false;
		}

		// read sample data
		if (fread(&audioDataS32[sampleLength / 4], 3, sampleLength, f) != sampleLength)
		{
			fclose(f);
			free(audioDataS32);
			displayErrorMsg("I/O ERROR !");
			return false;
		}

		// convert to 32-bit
		audioDataU8 = (uint8_t *)audioDataS32 + sampleLength;
		for (i = 0; i < sampleLength; i++)
		{
			audioDataS32[i] = (audioDataU8[0] << 24) | (audioDataU8[1] << 16) | (audioDataU8[2] << 8);
			audioDataU8 += 3;
		}

		// convert from stereo to mono (if needed)
		if (numChannels == 2)
		{
			sampleLength /= 2;
			for (i = 0; i < sampleLength-1; i++) // add right channel to left channel
			{
				smp32_l = audioDataS32[(i * 2) + 0] >> 1;
				smp32_r = audioDataS32[(i * 2) + 1] >> 1;
				audioDataS32[i] = smp32_l + smp32_r;
			}
		}

		// 2x downsampling - remove every other sample (if needed)
		if (forceDownSampling)
		{
			sampleLength /= 2;
			for (i = 1; i < sampleLength; i++)
				audioDataS32[i] = audioDataS32[i * 2];
		}

		if (sampleLength > MAX_SAMPLE_LEN)
			sampleLength = MAX_SAMPLE_LEN;

		normalize32bitSigned(audioDataS32, sampleLength);

		turnOffVoices();
		for (i = 0; i < MAX_SAMPLE_LEN; i++)
		{
			if (i <= (sampleLength & 0xFFFFFFFE))
				modEntry->sampleData[(editor.currSample * MAX_SAMPLE_LEN) + i] = (int8_t)(audioDataS32[i] >> 24);
			else
				modEntry->sampleData[(editor.currSample * MAX_SAMPLE_LEN) + i] = 0; // clear rest of sample
		}

		free(audioDataS32);
	}
	else if (bitDepth == 32) // 32-BIT INTEGER SAMPLE
	{
		sampleLength /= 4;
		if (sampleLength > MAX_SAMPLE_LEN*4)
			sampleLength = MAX_SAMPLE_LEN*4;

		audioDataS32 = (int32_t *)malloc(sampleLength * sizeof (int32_t));
		if (audioDataS32 == NULL)
		{
			fclose(f);
			statusOutOfMemory();
			return false;
		}

		// read sample data
		if (fread(audioDataS32, 4, sampleLength, f) != sampleLength)
		{
			fclose(f);
			free(audioDataS32);
			displayErrorMsg("I/O ERROR !");
			return false;
		}

		// fix endianness
		for (i = 0; i < sampleLength; i++)
			audioDataS32[i] = SWAP32(audioDataS32[i]);

		// convert from stereo to mono (if needed)
		if (numChannels == 2)
		{
			sampleLength /= 2;
			for (i = 0; i < sampleLength-1; i++) // add right channel to left channel
			{
				smp32_l = audioDataS32[(i * 2) + 0] >> 1;
				smp32_r = audioDataS32[(i * 2) + 1] >> 1;

				audioDataS32[i] = smp32_l + smp32_r;
			}
		}

		// 2x downsampling - remove every other sample (if needed)
		if (forceDownSampling)
		{
			sampleLength /= 2;
			for (i = 1; i < sampleLength; i++)
				audioDataS32[i] = audioDataS32[i * 2];
		}

		if (sampleLength > MAX_SAMPLE_LEN)
			sampleLength = MAX_SAMPLE_LEN;

		normalize32bitSigned(audioDataS32, sampleLength);

		turnOffVoices();
		for (i = 0; i < MAX_SAMPLE_LEN; i++)
		{
			if (i <= (sampleLength & 0xFFFFFFFE))
				modEntry->sampleData[(editor.currSample * MAX_SAMPLE_LEN) + i] = (int8_t)(audioDataS32[i] >> 24);
			else
				modEntry->sampleData[(editor.currSample * MAX_SAMPLE_LEN) + i] = 0; // clear rest of sample
		}

		free(audioDataS32);
	}

	// set sample length
	if (sampleLength & 1)
	{
		if (++sampleLength > MAX_SAMPLE_LEN)
			sampleLength = MAX_SAMPLE_LEN;
	}

	s->length = sampleLength;
	s->fineTune = 0;
	s->volume = 64;
	s->loopStart = 0;
	s->loopLength = 2;

	fclose(f);

	// copy over sample name
	nameLen = (uint32_t)strlen(entryName);
	for (i = 0; i < 21; i++)
		s->text[i] = (i < nameLen) ? toupper(entryName[i]) : '\0';

	s->text[21] = '\0';
	s->text[22] = '\0';

	// remove .aiff from end of sample name (if present)
	nameLen = (uint32_t)strlen(s->text);
	if (nameLen >= 5 && !_strnicmp(&s->text[nameLen-5], ".AIFF", 5))
		memset(&s->text[nameLen-5], '\0', 5);

	editor.sampleZero = false;
	editor.samplePos = 0;

	fixSampleBeep(s);
	updateCurrSample();

	updateWindowTitle(MOD_IS_MODIFIED);
	return true;
}

bool loadSample(UNICHAR *fileName, char *entryName)
{
	uint32_t fileSize, ID;
	FILE *f;

	f = UNICHAR_FOPEN(fileName, "rb");
	if (f == NULL)
	{
		displayErrorMsg("FILE I/O ERROR !");
		return false;
	}

	fseek(f, 0, SEEK_END);
	fileSize = ftell(f);
	fseek(f, 0, SEEK_SET);

	// first, check heades before we eventually load as RAW
	if (fileSize > 16)
	{
		fread(&ID, 4, 1, f);

		// check if it's actually a WAV sample
		if (ID == 0x46464952) // "RIFF"
		{
			fseek(f, 4, SEEK_CUR);
			fread(&ID, 4, 1, f);

			if (ID == 0x45564157) // "WAVE"
			{
				fread(&ID, 4, 1, f);
				if (ID == 0x20746D66) // "fmt "
				{
					fclose(f);
					return loadWAVSample(fileName, entryName, -1);
				}
			}
		}	
		else if (ID == 0x4D524F46) // "FORM"
		{
			fseek(f, 4, SEEK_CUR);
			fread(&ID, 4, 1, f);

			// check if it's an Amiga IFF sample
			if (ID == 0x58565338 || ID == 0x56533631) // "8SVX" (normal) and "16SV" (FT2 sample)
			{
				fclose(f);
				return loadIFFSample(fileName, entryName);
			}

			// check if it's an AIFF sample
			else if (ID == 0x46464941) // "AIFF"
			{
				fclose(f);
				return loadAIFFSample(fileName, entryName, -1);
			}
		}
	}

	// nope, continue loading as RAW
	fclose(f);

	return loadRAWSample(fileName, entryName);
}

static void removeWavIffExt(char *text) // for sample saver
{
	uint32_t fileExtPos;
	uint32_t filenameLength;
	
	filenameLength = (uint32_t)strlen(text);
	if (filenameLength < 5)
		return;

	// remove .wav/.iff/from end of sample name (if present)
	fileExtPos = filenameLength - 4;
	if (fileExtPos > 0 && (!strncmp(&text[fileExtPos], ".wav", 4) || !strncmp(&text[fileExtPos], ".iff", 4)))
		text[fileExtPos] = '\0';
}

bool saveSample(bool checkIfFileExist, bool giveNewFreeFilename)
{
	char fileName[128], tmpBuffer[64];
	uint8_t smp;
	uint16_t j;
	int32_t i;
	uint32_t sampleSize, iffSize, iffSampleSize;
	uint32_t loopStart, loopLen, tmp32;
	FILE *f;
	struct stat statBuffer;
	moduleSample_t *s;
	wavHeader_t wavHeader;
	samplerChunk_t samplerChunk;

	if (modEntry->samples[editor.currSample].length == 0)
	{
		displayErrorMsg("SAMPLE IS EMPTY");
		return false;
	}

	memset(fileName, 0, sizeof (fileName));

	if (*modEntry->samples[editor.currSample].text == '\0')
	{
		strcpy(fileName, "untitled");
	}
	else
	{
		for (i = 0; i < 22; i++)
		{
			tmpBuffer[i] = (char)tolower(modEntry->samples[editor.currSample].text[i]);
			if (tmpBuffer[i] == '\0') break;
			sanitizeFilenameChar(&tmpBuffer[i]);
		}

		strcpy(fileName, tmpBuffer);
	}
	
	removeWavIffExt(fileName);

	switch (editor.diskop.smpSaveType)
	{
		case DISKOP_SMP_WAV: strcat(fileName, ".wav"); break;
		case DISKOP_SMP_IFF: strcat(fileName, ".iff"); break;
		case DISKOP_SMP_RAW: break;
		default: return false; // make compiler happy
	}

	if (giveNewFreeFilename)
	{
		if (stat(fileName, &statBuffer) == 0)
		{
			for (j = 1; j <= 9999; j++) // This number should satisfy all! ;)
			{
				memset(fileName, 0, sizeof (fileName));

				if (*modEntry->samples[editor.currSample].text == '\0')
				{
					sprintf(fileName, "untitled-%d", j);
				}
				else
				{
					for (i = 0; i < 22; i++)
					{
						tmpBuffer[i] = (char)tolower(modEntry->samples[editor.currSample].text[i]);
						if (tmpBuffer[i] == '\0') break;
						sanitizeFilenameChar(&tmpBuffer[i]);
					}

					removeWavIffExt(tmpBuffer);
					sprintf(fileName, "%s-%d", tmpBuffer, j);
				}

				switch (editor.diskop.smpSaveType)
				{
					default: case DISKOP_SMP_WAV: strcat(fileName, ".wav"); break;
					         case DISKOP_SMP_IFF: strcat(fileName, ".iff"); break;
					         case DISKOP_SMP_RAW: break;
				}

				if (stat(fileName, &statBuffer) != 0)
					break;
			}
		}
	}

	if (checkIfFileExist && stat(fileName, &statBuffer) == 0)
	{
		editor.ui.askScreenShown = true;
		editor.ui.askScreenType = ASK_SAVESMP_OVERWRITE;
		pointerSetMode(POINTER_MODE_MSG1, NO_CARRY);
		setStatusMessage("OVERWRITE FILE ?", NO_CARRY);
		renderAskDialog();
		return -1;
	}

	if (editor.ui.askScreenShown)
	{
		editor.ui.answerNo = false;
		editor.ui.answerYes = false;
		editor.ui.askScreenShown = false;
	}

	f = fopen(fileName, "wb");
	if (f == NULL)
	{
		displayErrorMsg("FILE I/O ERROR !");
		return false;
	}

	sampleSize = modEntry->samples[editor.currSample].length;

	switch (editor.diskop.smpSaveType)
	{
		default:
		case DISKOP_SMP_WAV:
		{
			s = &modEntry->samples[editor.currSample];

			wavHeader.format = 0x45564157; // "WAVE"
			wavHeader.chunkID = 0x46464952; // "RIFF"
			wavHeader.subchunk1ID = 0x20746D66; // "fmt "
			wavHeader.subchunk2ID = 0x61746164; // "data"
			wavHeader.subchunk1Size = 16;
			wavHeader.subchunk2Size = sampleSize;
			wavHeader.chunkSize = wavHeader.subchunk2Size + 36;
			wavHeader.audioFormat = 1;
			wavHeader.numChannels = 1;
			wavHeader.bitsPerSample = 8;
			wavHeader.sampleRate = 16574;
			wavHeader.byteRate = wavHeader.sampleRate * wavHeader.numChannels * wavHeader.bitsPerSample / 8;
			wavHeader.blockAlign = wavHeader.numChannels * wavHeader.bitsPerSample / 8;

			if (s->loopLength > 2)
			{
				wavHeader.chunkSize += sizeof (samplerChunk_t);
				memset(&samplerChunk, 0, sizeof (samplerChunk_t));
				samplerChunk.chunkID = 0x6C706D73; // "smpl"
				samplerChunk.chunkSize = 60;
				samplerChunk.dwSamplePeriod = 1000000000 / 16574;
				samplerChunk.dwMIDIUnityNote = 60; // 60 = C-4
				samplerChunk.cSampleLoops = 1;
				samplerChunk.loop.dwStart = (uint32_t)s->loopStart;
				samplerChunk.loop.dwEnd = (uint32_t)((s->loopStart + s->loopLength) - 1);
			}

			fwrite(&wavHeader, sizeof (wavHeader_t), 1, f);

			for (i = 0; i < (int32_t)sampleSize; i++)
			{
				smp = modEntry->sampleData[modEntry->samples[editor.currSample].offset + i] + 128;
				fputc(smp, f);
			}

			if (sampleSize & 1)
				fputc(0, f); // pad align byte

			if (s->loopLength > 2)
				fwrite(&samplerChunk, sizeof (samplerChunk), 1, f);
		}
		break;

		case DISKOP_SMP_IFF:
		{
			// dwords are big-endian in IFF
			loopStart = modEntry->samples[editor.currSample].loopStart  & 0xFFFFFFFE;
			loopLen = modEntry->samples[editor.currSample].loopLength & 0xFFFFFFFE;

			loopStart = SWAP32(loopStart);
			loopLen = SWAP32(loopLen);
			iffSize = SWAP32(sampleSize + 100);
			iffSampleSize = SWAP32(sampleSize);

			fputc(0x46, f);fputc(0x4F, f);fputc(0x52, f);fputc(0x4D, f); // "FORM"
			fwrite(&iffSize, 4, 1, f);

			fputc(0x38, f);fputc(0x53, f);fputc(0x56, f);fputc(0x58, f); // "8SVX"
			fputc(0x56, f);fputc(0x48, f);fputc(0x44, f);fputc(0x52, f); // "VHDR"
			fputc(0x00, f);fputc(0x00, f);fputc(0x00, f);fputc(0x14, f); // 0x00000014

			if (modEntry->samples[editor.currSample].loopLength > 2)
			{
				fwrite(&loopStart, 4, 1, f);
				fwrite(&loopLen, 4, 1, f);
			}
			else
			{
				fwrite(&iffSampleSize, 4, 1, f);
				fputc(0x00, f);fputc(0x00, f);fputc(0x00, f);fputc(0x00, f); // 0x00000000
			}

			fputc(0x00, f);fputc(0x00, f);fputc(0x00, f);fputc(0x00, f); // 0x00000000

			fputc(0x41, f);fputc(0x56, f); // 16726 (rate)
			fputc(0x01, f);fputc(0x00, f); // numSamples and compression

			tmp32 = modEntry->samples[editor.currSample].volume * 1024;
			tmp32 = SWAP32(tmp32);
			fwrite(&tmp32, 4, 1, f);

			fputc(0x4E, f);fputc(0x41, f);fputc(0x4D, f);fputc(0x45, f); // "NAME"
			fputc(0x00, f);fputc(0x00, f);fputc(0x00, f);fputc(0x16, f); // 0x00000016

			for (i = 0; i < 22; i++)
				fputc(tolower(modEntry->samples[editor.currSample].text[i]), f);

			fputc(0x41, f);fputc(0x4E, f);fputc(0x4E, f);fputc(0x4F, f); // "ANNO"
			fputc(0x00, f);fputc(0x00, f);fputc(0x00, f);fputc(0x15, f); // 0x00000012
			fprintf(f, "ProTracker 2 clone");
			fputc(0x00, f); // even padding

			fputc(0x42, f);fputc(0x4F, f);fputc(0x44, f);fputc(0x59, f);    // "BODY"
			fwrite(&iffSampleSize, 4, 1, f);
			fwrite(modEntry->sampleData + modEntry->samples[editor.currSample].offset, 1, sampleSize, f);

			// shouldn't happen, but in just case: safety even padding
			if (sampleSize & 1)
				fputc(0x00, f);
		}
		break;

		case DISKOP_SMP_RAW:
			fwrite(modEntry->sampleData + modEntry->samples[editor.currSample].offset, 1, sampleSize, f);
		break;
	}

	fclose(f);

	editor.diskop.cached = false;
	if (editor.ui.diskOpScreenShown)
		editor.ui.updateDiskOpFileList = true;

	displayMsg("SAMPLE SAVED !");
	setMsgPointer();

	return true;
}
