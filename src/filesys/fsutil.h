#ifndef FILESYS_FSUTIL_H
//2911809535113
#define FILESYS_FSUTIL_H

void fsutil_ls (char **argv);
void fsutil_cat (char **argv);
void fsutil_rm (char **argv);
void fsutil_extract (char **argv);
void fsutil_append (char **argv);

void fsutil_dump_snapshot (void);
void fsutil_retrieve_snapshot (void);

#endif /* filesys/fsutil.h */
