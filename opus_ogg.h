#pragma once
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
extern "C" {
#include "opus_header.h"
}
#include "opus.h"
#include "ogg.h"


/*ogg header*/
void writeOggHeader(FILE *fout);

/*ogg file*/
void writeOgg(unsigned char *cbits, int length, FILE *fout);