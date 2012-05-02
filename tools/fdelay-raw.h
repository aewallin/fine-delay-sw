#include <stdio.h>
#include <glob.h>
#include <errno.h>

/* return an array of sysfs pathnames, array is preallocated. Returns count */
static inline int fdelay_get_sysnames(char *result[])
{
	glob_t glob_buf = {0,};
	int i;

	glob("/sys/bus/zio/devices/zio-fd-*",0 , NULL, &glob_buf);
	for (i = 0; i < glob_buf.gl_pathc; i++)
		result[i] = strdup(glob_buf.gl_pathv[i]);
	globfree(&glob_buf);
	return i;
}

static inline int fdelay_sysfs_get(char *path, uint32_t *resp)
{
	FILE *f = fopen(path, "r");

	if (!f)
		return -1;
	if (fscanf(f, "%i", resp) != 1) {
		fclose(f);
		errno = EINVAL;
		return -1;
	}
	fclose(f);
	return 0;
}

static inline int fdelay_sysfs_set(char *path, uint32_t *value)
{
	FILE *f = fopen(path, "w");

	if (!f)
		return -1;
	if (fprintf(f, "%i\n", *value) < 2) {
		fclose(f);
		errno = EINVAL;
		return -1;
	}
	fclose(f);
	return 0;
}
