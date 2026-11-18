#include "analyzer.h"
#include <dirent.h>
#include <errno.h>
#include <regex.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static int append(Jobs *jobs, const char *path, uint64_t size) {
    if (jobs->count == jobs->capacity) {
        size_t cap = jobs->capacity ? jobs->capacity * 2 : 64;
        Job *items = realloc(jobs->items, cap * sizeof(*items));
        if (!items) return -1;
        jobs->items = items; jobs->capacity = cap;
    }
    char *copy = strdup(path);
    if (!copy) return -1;
    jobs->items[jobs->count++] = (Job){.path=copy, .size=size};
    return 0;
}

static int compare_jobs(const void *a, const void *b) {
    return strcmp(((const Job *)a)->path, ((const Job *)b)->path);
}

int scan_files(const char *input, Jobs *jobs) {
    struct stat st;
    if (lstat(input, &st)) return -1;
    if (S_ISREG(st.st_mode)) return append(jobs, input, (uint64_t)st.st_size);
    if (!S_ISDIR(st.st_mode)) { errno=EINVAL; return -1; }
    DIR *dir = opendir(input);
    if (!dir) return -1;
    regex_t pattern;
    const char *rx = "^pkt_[0-9]{4}[._][0-9]{2}[._][0-9]{2}_[0-9]{2}[._][0-9]{2}$";
    if (regcomp(&pattern, rx, REG_EXTENDED | REG_NOSUB)) { closedir(dir); errno=EINVAL; return -1; }
    int result = 0;
    for (;;) {
        errno=0;
        struct dirent *entry = readdir(dir);
        if (!entry) { if (errno) result=-1; break; }
        if (regexec(&pattern, entry->d_name, 0, NULL, 0)) continue;
        size_t len = strlen(input) + strlen(entry->d_name) + 2;
        char *path = malloc(len);
        if (!path) { result=-1; break; }
        snprintf(path, len, "%s/%s", input, entry->d_name);
        if (lstat(path, &st)) { free(path); result=-1; break; }
        if (S_ISREG(st.st_mode) && append(jobs, path, (uint64_t)st.st_size)) result=-1;
        free(path);
        if (result) break;
    }
    int saved=errno;
    regfree(&pattern); closedir(dir); errno=saved;
    if (jobs->count > 1) qsort(jobs->items, jobs->count, sizeof(Job), compare_jobs);
    return result;
}

void jobs_free(Jobs *jobs) {
    for (size_t i=0; i<jobs->count; i++) free(jobs->items[i].path);
    free(jobs->items); *jobs=(Jobs){0};
}
