#pragma once
#include <string>
using namespace std;

enum ENCODING_TYPE {
	PCM8,
	PCM16,
	PCM32,
	IEEE_FLOAT
};

#define  WAVE_FORMAT_UNKNOWN                    0x0000 /* Microsoft Corporation */
#define  WAVE_FORMAT_ADPCM                      0x0002 /* Microsoft Corporation */
#define  WAVE_FORMAT_IEEE_FLOAT                 0x0003 /* Microsoft Corporation */
#define  WAVE_FORMAT_VSELP                      0x0004 /* Compaq Computer Corp. */
#define  WAVE_FORMAT_IBM_CVSD                   0x0005 /* IBM Corporation */
#define  WAVE_FORMAT_ALAW                       0x0006 /* Microsoft Corporation */
#define  WAVE_FORMAT_MULAW                      0x0007 /* Microsoft Corporation */

string GetIndexPlayer(
	int encoding,
	int blockAlign,
	int channels,
	int sampleRate,
	int flushingTime);