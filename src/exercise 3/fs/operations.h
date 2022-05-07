#ifndef FS_H
#define FS_H
#include "state.h"

enum flags{PRINTING, NOTPRINTING, UNDEFINED};

/* Prototype functions of operations.c*/
void init_fs();
void destroy_fs();
int is_dir_empty(DirEntry *dirEntries);
int create(char *name, type nodeType);
int delete(char *name);
int lookup(char *name);
int move(char *current_path, char *new_path);
int print_tecnicofs_tree(char *fp);
save_locks* lookup_commands(char *name,char ltype);

#endif /* FS_H */
