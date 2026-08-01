#include "syscard_load.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t *g_buf = NULL;
static size_t g_len = 0;

int syscard_load_file(const char *path)
{
	FILE *f = fopen(path, "rb");
	if (!f) {
		fprintf(stderr, "Cannot open System Card: %s\n", path);
		return -1;
	}
	if (fseek(f, 0, SEEK_END) != 0) {
		fclose(f);
		return -1;
	}
	long sz = ftell(f);
	if (sz <= 0) {
		fclose(f);
		return -1;
	}
	rewind(f);
	free(g_buf);
	g_buf = (uint8_t *)malloc((size_t)sz);
	if (!g_buf) {
		fclose(f);
		return -1;
	}
	if (fread(g_buf, 1, (size_t)sz, f) != (size_t)sz) {
		free(g_buf);
		g_buf = NULL;
		fclose(f);
		return -1;
	}
	fclose(f);
	g_len = (size_t)sz;
	printf("System Card file: %s (%ld bytes)\n", path, sz);
	return 0;
}

const char *syscard_find_default(void)
{
	const char *from_env = getenv("PCE_SYSCARD");
	if (from_env) {
		FILE *f = fopen(from_env, "rb");
		if (f) { fclose(f); return from_env; }
	}
	static const char *candidates[] = {
		"syscard3.pce",
		"syscard2.pce",
		"syscard1.pce",
		"bios/syscard3.pce",
		"../bios/syscard3.pce",
		NULL
	};
	for (int i = 0; candidates[i]; i++) {
		FILE *f = fopen(candidates[i], "rb");
		if (f) {
			fclose(f);
			return candidates[i];
		}
	}
	return NULL;
}

size_t syscard_get_data(unsigned char **data)
{
	if (!g_buf || g_len == 0) {
		*data = NULL;
		return 0;
	}
	*data = g_buf;
	return g_len;
}

static uint8_t *g_hucard_buf = NULL;
static size_t g_hucard_len = 0;

int hucard_load_file(const char *path)
{
	FILE *f = fopen(path, "rb");
	if (!f) {
		fprintf(stderr, "Cannot open HuCard: %s\n", path);
		return -1;
	}
	if (fseek(f, 0, SEEK_END) != 0) {
		fclose(f);
		return -1;
	}
	long sz = ftell(f);
	if (sz <= 0) {
		fclose(f);
		return -1;
	}
	rewind(f);
	free(g_hucard_buf);
	g_hucard_buf = (uint8_t *)malloc((size_t)sz);
	if (!g_hucard_buf) {
		fclose(f);
		return -1;
	}
	if (fread(g_hucard_buf, 1, (size_t)sz, f) != (size_t)sz) {
		free(g_hucard_buf);
		g_hucard_buf = NULL;
		fclose(f);
		return -1;
	}
	fclose(f);
	g_hucard_len = (size_t)sz;
	printf("HuCard file: %s (%ld bytes)\n", path, sz);
	return 0;
}

size_t hucard_get_data(unsigned char **data)
{
	if (!g_hucard_buf || g_hucard_len == 0) {
		*data = NULL;
		return 0;
	}
	*data = g_hucard_buf;
	return g_hucard_len;
}

void hucard_unload(void)
{
	free(g_hucard_buf);
	g_hucard_buf = NULL;
	g_hucard_len = 0;
}
