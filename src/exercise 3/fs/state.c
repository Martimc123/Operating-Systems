#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "state.h"
#include <pthread.h>
#include "../tecnicofs-api-constants.h"

/* Inode table */
inode_t inode_table[INODE_TABLE_SIZE];

/*
 * Sleeps for synchronization testing.
 */
void insert_delay(int cycles) {
    for (int i = 0; i < cycles; i++) {}
}


/*
 * Initializes the i-nodes table.
 */
void inode_table_init() {
    for (int i = 0; i < INODE_TABLE_SIZE; i++) {
        inode_table[i].nodeType = T_NONE;
        inode_table[i].data.dirEntries = NULL;
        inode_table[i].data.fileContents = NULL;
        pthread_rwlock_init(&inode_table[i].lock ,NULL);
    }
}


/*
 * Locks a specific inode in a given inumber for writing or reading
*/
void inode_lock(int inumber, char c)
{
    if (c == 'r') {
        if ( pthread_rwlock_rdlock(&inode_table[inumber].lock) != SUCCESS ) {
            fprintf(stderr, "Error: could not rdlock at inumber: %d.\n", inumber);
            exit(EXIT_FAILURE);
        }
    }
    else if (c == 'w') {
        if ( pthread_rwlock_wrlock(&inode_table[inumber].lock) != SUCCESS ) {
            fprintf(stderr, "Error: could not wrlock at inumber: %d.\n", inumber);
            exit(EXIT_FAILURE);
        }
    }
}

/*
 * Unlocks a specific node of the inode table in a given inumber
*/ 
void inode_unlock(int inumber)
{
    pthread_rwlock_unlock(&inode_table[inumber].lock);
}


/*
 * Releases the allocated memory for the i-nodes tables.
*/
void inode_table_destroy() {
    for (int i = 0; i < INODE_TABLE_SIZE; i++) {
        if (inode_table[i].nodeType != T_NONE) {
            /* as data is an union, the same pointer is used for both dirEntries and fileContents */
            /* just release one of them */
	    if (inode_table[i].data.dirEntries)
            free(inode_table[i].data.dirEntries);
        
        // releases hte bytes associated with the inode in the indicated index of the inode table in the memory
        pthread_rwlock_destroy(&inode_table[i].lock);
        }
    }
}

/*
 * Creates a new i-node in the table with the given information.
 * Input:
 *  - nType: the type of the node (file or directory)
 *  - c: char that indicates if newly created inode will be locked or not.
 * Returns:
 *  inumber: identifier of the new i-node, if successfully created
 *     FAIL: if an error occurs
 */
int inode_create(type nType, char c) {
    /* Used for testing synchronization speedup */
    //insert_delay(DELAY);

    for (int inumber = 0; inumber < INODE_TABLE_SIZE; inumber++) {
        if (inode_table[inumber].nodeType == T_NONE) {
            
            if ( c == 'w' )
                inode_lock(inumber, 'w');

            inode_table[inumber].nodeType = nType;

            if (nType == T_DIRECTORY) {
                /* Initializes entry table */
                inode_table[inumber].data.dirEntries = malloc(sizeof(DirEntry) * MAX_DIR_ENTRIES);
                
                for (int i = 0; i < MAX_DIR_ENTRIES; i++) {
                    inode_table[inumber].data.dirEntries[i].inumber = FREE_INODE;
                }
            }
            else {
                inode_table[inumber].data.fileContents = NULL;
            }
            return inumber;
        }
    }
    return FAIL;
}

/*
 * Deletes the i-node.
 * Input:
 *  - inumber: identifier of the i-node
 * Returns: SUCCESS or FAIL
 */
int inode_delete(int inumber) {
    /* Used for testing synchronization speedup */
    //insert_delay(DELAY);

    if ((inumber < 0) || (inumber > INODE_TABLE_SIZE) || (inode_table[inumber].nodeType == T_NONE)) {
        printf("inode_delete: invalid inumber\n");
        return FAIL;
    } 

    inode_table[inumber].nodeType = T_NONE;
    /* see inode_table_destroy function */
    if (inode_table[inumber].data.dirEntries)
        free(inode_table[inumber].data.dirEntries);
    return SUCCESS;
}

/*
 * Copies the contents of the i-node into the arguments.
 * Only the fields referenced by non-null arguments are copied.
 * Input:
 *  - inumber: identifier of the i-node
 *  - nType: pointer to type
 *  - data: pointer to data
 * Returns: SUCCESS or FAIL
 */
int inode_get(int inumber, type *nType, union Data *data) {
    /* Used for testing synchronization speedup */
    //insert_delay(DELAY);

    if ((inumber < 0) || (inumber > INODE_TABLE_SIZE) || (inode_table[inumber].nodeType == T_NONE)) {
        printf("inode_get: invalid inumber %d\n", inumber);
        return FAIL;
    }

    if (nType)
        *nType = inode_table[inumber].nodeType;

    if (data)
        *data = inode_table[inumber].data;
    return SUCCESS;
}


/*
 *Unlocks all the nodes used in lookup commands
*/
void unlock_all_nodes(int locks[],int size){
    for (int i=0;i<size;i++)
		inode_unlock(locks[i]);
}

/*
 * Resets an entry for a directory.
 * Input:
 *  - inumber: identifier of the i-node
 *  - sub_inumber: identifier of the sub i-node entry
 * Returns: SUCCESS or FAIL
 */
int dir_reset_entry(int inumber, int sub_inumber) {
    /* Used for testing synchronization speedup */
    //insert_delay(DELAY);

    if ((inumber < 0) || (inumber > INODE_TABLE_SIZE) || (inode_table[inumber].nodeType == T_NONE)) {
        printf("inode_reset_entry: invalid inumber\n");
        return FAIL;
    }

    if (inode_table[inumber].nodeType != T_DIRECTORY) {
        printf("inode_reset_entry: can only reset entry to directories\n");
        return FAIL;
    }

    if ((sub_inumber < FREE_INODE) || (sub_inumber > INODE_TABLE_SIZE) || (inode_table[sub_inumber].nodeType == T_NONE)) {
        printf("inode_reset_entry: invalid entry inumber\n");
        return FAIL;
    }

    
    for (int i = 0; i < MAX_DIR_ENTRIES; i++) {
        if (inode_table[inumber].data.dirEntries[i].inumber == sub_inumber) {
            inode_table[inumber].data.dirEntries[i].inumber = FREE_INODE;
            inode_table[inumber].data.dirEntries[i].name[0] = '\0';
            return SUCCESS;
        }
    }
    return FAIL;
}


/*
 * Adds an entry to the i-node directory data.
 * Input:
 *  - inumber: identifier of the i-node
 *  - sub_inumber: identifier of the sub i-node entry
 *  - sub_name: name of the sub i-node entry 
 * Returns: SUCCESS or FAIL
 */
int dir_add_entry(int inumber, int sub_inumber, char *sub_name) {
    /* Used for testing synchronization speedup */
    //insert_delay(DELAY);

    if ((inumber < 0) || (inumber > INODE_TABLE_SIZE) || (inode_table[inumber].nodeType == T_NONE)) {
        printf("inode_add_entry: invalid inumber\n");
        return FAIL;
    }

    if (inode_table[inumber].nodeType != T_DIRECTORY) {
        printf("inode_add_entry: can only add entry to directories\n");
        return FAIL;
    }

    if ((sub_inumber < 0) || (sub_inumber > INODE_TABLE_SIZE) || (inode_table[sub_inumber].nodeType == T_NONE)) {
        printf("inode_add_entry: invalid entry inumber\n");
        return FAIL;
    }

    if (strlen(sub_name) == 0 ) {
        printf("inode_add_entry: \
               entry name must be non-empty\n");
        return FAIL;
    }
    
    for (int i = 0; i < MAX_DIR_ENTRIES; i++) {
        if (inode_table[inumber].data.dirEntries[i].inumber == FREE_INODE) {
            inode_table[inumber].data.dirEntries[i].inumber = sub_inumber;
            strcpy(inode_table[inumber].data.dirEntries[i].name, sub_name);
            return SUCCESS;
        }
    }
    return FAIL;
}


/*
 * Prints the i-nodes table.
 * Input:
 *  - fp: file to output
 *  - inumber: identifier of the i-node
 *  - name: pointer to the name of current file/dir
 * 
 * Returns:
 *  - either SUCCESS or FAIL
 */
int inode_print_tree(FILE *fp, int inumber, char *name) {

    if (inode_table[inumber].nodeType == T_FILE) {
        fprintf(fp, "%s\n", name);
        return SUCCESS;
    }

    if (inode_table[inumber].nodeType == T_DIRECTORY) {
        fprintf(fp, "%s\n", name);
        for (int i = 0; i < MAX_DIR_ENTRIES; i++) {
            if (inode_table[inumber].data.dirEntries[i].inumber != FREE_INODE) {
                char path[MAX_FILE_NAME];
                if (snprintf(path, sizeof(path), "%s/%s", name, inode_table[inumber].data.dirEntries[i].name) > sizeof(path)) {
                    fprintf(stderr, "truncation when building full path\n");
                    return FAIL;
                }
                if ( inode_print_tree(fp, inode_table[inumber].data.dirEntries[i].inumber, path) == FAIL ) {
                    return FAIL;
                }
            }
        }
    }
    return SUCCESS;
}