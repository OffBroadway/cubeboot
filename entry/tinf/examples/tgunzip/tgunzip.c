/*
 * tgunzip - gzip decompressor example
 *
 * Copyright (c) 2003-2019 Joergen Ibsen
 *
 * This software is provided 'as-is', without any express or implied
 * warranty. In no event will the authors be held liable for any damages
 * arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 *   1. The origin of this software must not be misrepresented; you must
 *      not claim that you wrote the original software. If you use this
 *      software in a product, an acknowledgment in the product
 *      documentation would be appreciated but is not required.
 *
 *   2. Altered source versions must be plainly marked as such, and must
 *      not be misrepresented as being the original software.
 *
 *   3. This notice may not be removed or altered from any source
 *      distribution.
 */

#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>

#include "tinf.h"

static unsigned int read_le32(const unsigned char *p)
{
	return ((unsigned int) p[0])
	     | ((unsigned int) p[1] << 8)
	     | ((unsigned int) p[2] << 16)
	     | ((unsigned int) p[3] << 24);
}

static void printf_error(const char *fmt, ...)
{
	va_list arg;

	fputs("tgunzip: ", stderr);

	va_start(arg, fmt);
	vfprintf(stderr, fmt, arg);
	va_end(arg);

	fputs("\n", stderr);
}

int main(int argc, char *argv[])
{
	FILE *fin = NULL;
	FILE *fout = NULL;
	unsigned char *source = NULL;
	unsigned char *dest = NULL;
	unsigned int len, dlen, outlen;
	int retval = EXIT_FAILURE;
	int res;

	printf("tgunzip " TINF_VER_STRING " - example from the tiny inflate library (www.ibsensoftware.com)\n\n");

	if (argc != 3) {
		fputs("usage: tgunzip INFILE OUTFILE\n\n"
		      "Both input and output are kept in memory, so do not use this on huge files.\n", stderr);
		return EXIT_FAILURE;
	}

	tinf_init();

	/* -- Open files -- */

	if ((fin = fopen(argv[1], "rb")) == NULL) {
		printf_error("unable to open input file '%s'", argv[1]);
		goto out;
	}

	if ((fout = fopen(argv[2], "wb")) == NULL) {
		printf_error("unable to create output file '%s'", argv[2]);
		goto out;
	}

	/* -- Read source -- */

	fseek(fin, 0, SEEK_END);

	len = ftell(fin);

	fseek(fin, 0, SEEK_SET);

	if (len < 18) {
		printf_error("input too small to be gzip");
		goto out;
	}

	source = (unsigned char *) malloc(len);

	if (source == NULL) {
		printf_error("not enough memory");
		goto out;
	}

	if (fread(source, 1, len, fin) != len) {
		printf_error("error reading input file");
		goto out;
	}

	/* -- Get decompressed length -- */

	dlen = read_le32(&source[len - 4]);

	dest = (unsigned char *) malloc(dlen ? dlen : 1);

	if (dest == NULL) {
		printf_error("not enough memory");
		goto out;
	}

	/* -- Decompress data -- */

	outlen = dlen;

	res = tinf_gzip_uncompress(dest, &outlen, source, len);

	if ((res != TINF_OK) || (outlen != dlen)) {
		printf_error("decompression failed");
		goto out;
	}

	printf("decompressed %u bytes\n", outlen);

	/* -- Write output -- */

	fwrite(dest, 1, outlen, fout);

	retval = EXIT_SUCCESS;

out:
	if (fin != NULL) {
		fclose(fin);
	}

	if (fout != NULL) {
		fclose(fout);
	}

	if (source != NULL) {
		free(source);
	}

	if (dest != NULL) {
		free(dest);
	}

	return retval;
}
