#pragma once
#include <sys/stat.h>
#include <climits>
struct _reent {int _errno;};
struct DIR_ITER {void* dirStruct;};
struct devoptab_t {
    const char* name; size_t structSize;
    int (*open_r)(_reent*,void*,const char*,int,int);
    int (*close_r)(_reent*,void*);
    ssize_t (*read_r)(_reent*,void*,char*,size_t);
    off_t (*seek_r)(_reent*,void*,off_t,int);
    int (*fstat_r)(_reent*,void*,struct stat*);
    int (*stat_r)(_reent*,const char*,struct stat*);
    size_t dirStateSize;
    DIR_ITER* (*diropen_r)(_reent*,DIR_ITER*,const char*);
    int (*dirreset_r)(_reent*,DIR_ITER*);
    int (*dirnext_r)(_reent*,DIR_ITER*,char*,struct stat*);
    int (*dirclose_r)(_reent*,DIR_ITER*);
    int (*lstat_r)(_reent*,const char*,struct stat*);
};
int AddDevice(const devoptab_t*);
int RemoveDevice(const char*);
