#include "operations.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>

/* Given a path, fills pointers with strings for the parent path and child
 * file name
 * Input:
 *  - path: the path to split. ATENTION: the function may alter this parameter
 *  - parent: reference to a char*, to store parent path
 *  - child: reference to a char*, to store child file name
 */
void split_parent_child_from_path(char * path, char ** parent, char ** child) {

	int n_slashes = 0, last_slash_location = 0;
	int len = strlen(path);

	// deal with trailing slash ( a/x vs a/x/ )
	if (path[len-1] == '/') {
		path[len-1] = '\0';
	}

	for (int i=0; i < len; ++i) {
		if (path[i] == '/' && path[i+1] != '\0') {
			last_slash_location = i;
			n_slashes++;
		}
	}

	if (n_slashes == 0) { // root directory
		*parent = "";
		*child = path;
		return;
	}

	path[last_slash_location] = '\0';
	*parent = path;
	*child = path + last_slash_location + 1;

}


/*
 * Initializes tecnicofs and creates root node.
 */
void init_fs() {
	inode_table_init();
	
	/* create root inode */
	int root = inode_create(T_DIRECTORY, 'x');
	
	if (root != FS_ROOT) {
		printf("failed to create node for tecnicofs root\n");
		exit(EXIT_FAILURE);
	}
}


/*
 * Destroy tecnicofs and inode table.
 */
void destroy_fs() {
	inode_table_destroy();
}


/*
 * Checks if content of directory is not empty.
 * Input:
 *  - entries: entries of directory
 * Returns: SUCCESS or FAIL
 */
int is_dir_empty(DirEntry *dirEntries) {

	if (dirEntries == NULL) {
		return FAIL;
	}
	for (int i = 0; i < MAX_DIR_ENTRIES; i++) {
		if (dirEntries[i].inumber != FREE_INODE) {
			return FAIL;
		}
	}
	return SUCCESS;
}


/*
 * Looks for node in directory entry from name.
 * Input:
 *  - name: path of node
 *  - entries: entries of directory
 * Returns:
 *  - inumber: found node's inumber
 *  - FAIL: if not found
 */
int lookup_sub_node(char *name, DirEntry *entries) {

	if (entries == NULL) {
		return FAIL;
	}	
	for (int i = 0; i < MAX_DIR_ENTRIES; i++) {
		if (entries[i].inumber != FREE_INODE && strcmp(entries[i].name, name) == 0) {
            return entries[i].inumber;
        }
    }
	return FAIL;
}


/*
 * Creates a new node given a path.
 * Input:
 *  - name: path of node
 *  - nodeType: type of node
 * Returns: SUCCESS or FAIL
 */
int create(char *name, type nodeType){

	int parent_inumber, child_inumber;
	char *parent_name, *child_name, name_copy[MAX_FILE_NAME];
	/* use for copy */
	type pType;
	union Data pdata;
	save_locks* inodes_locks;

	strcpy(name_copy, name);
	split_parent_child_from_path(name_copy, &parent_name, &child_name);


	inodes_locks = lookup_commands(parent_name,'w');
	parent_inumber = inodes_locks->inumber;

	if (parent_inumber == FAIL) {
		printf("failed to create %s, invalid parent dir %s\n",
		        name, parent_name);
		unlock_all_nodes(inodes_locks->locks_numbers,inodes_locks->num_locks);
		free(inodes_locks);
		return FAIL;
	}

	inode_get(parent_inumber, &pType, &pdata);


	if(pType != T_DIRECTORY) {
		printf("failed to create %s, parent %s is not a dir\n",
		        name, parent_name);
		unlock_all_nodes(inodes_locks->locks_numbers,inodes_locks->num_locks);
		free(inodes_locks);
		return FAIL;
	}
	

	if (lookup_sub_node(child_name, pdata.dirEntries) != FAIL) {
		printf("failed to create %s, already exists in dir %s\n",
		       child_name, parent_name);
		unlock_all_nodes(inodes_locks->locks_numbers,inodes_locks->num_locks);
		free(inodes_locks);
		return FAIL;
	}

	/* create node and add entry to folder that contains new node */
	child_inumber = inode_create(nodeType, 'w');
	if (child_inumber == FAIL) {
		printf("failed to create %s in  %s, couldn't allocate inode\n",
		        child_name, parent_name);
		unlock_all_nodes(inodes_locks->locks_numbers,inodes_locks->num_locks);
		inode_unlock(child_inumber);
		free(inodes_locks);
		return FAIL;
	}
	
	if (dir_add_entry(parent_inumber, child_inumber, child_name) == FAIL) {
		printf("could not add entry %s in dir %s\n",
		       child_name, parent_name);
		unlock_all_nodes(inodes_locks->locks_numbers,inodes_locks->num_locks);
		inode_unlock(child_inumber);
		free(inodes_locks);
		return FAIL;
	}
	unlock_all_nodes(inodes_locks->locks_numbers,inodes_locks->num_locks);
	inode_unlock(child_inumber);
	free(inodes_locks);

	return SUCCESS;
}

/*
 * Deletes a node given a path.
 * Input:
 *  - name: path of node
 * Returns: SUCCESS or FAIL
 */
int delete(char *name){

	int parent_inumber, child_inumber;
	char *parent_name, *child_name, name_copy[MAX_FILE_NAME];
	/* use for copy */
	type pType, cType;
	union Data pdata, cdata;
	save_locks* inodes_locks;
	
	strcpy(name_copy, name);
	split_parent_child_from_path(name_copy, &parent_name, &child_name);

	inodes_locks = lookup_commands(parent_name,'w');
	parent_inumber = inodes_locks->inumber;

	if (parent_inumber == FAIL) {
		printf("failed to delete %s, invalid parent dir %s\n",
		        child_name, parent_name);
		unlock_all_nodes(inodes_locks->locks_numbers,inodes_locks->num_locks);
		free(inodes_locks);
		return FAIL;
	}

	inode_get(parent_inumber, &pType, &pdata);

	if(pType != T_DIRECTORY) {
		printf("failed to delete %s, parent %s is not a dir\n",
		        child_name, parent_name);
		unlock_all_nodes(inodes_locks->locks_numbers,inodes_locks->num_locks);
		free(inodes_locks);
		return FAIL;
	}

	child_inumber = lookup_sub_node(child_name, pdata.dirEntries);

	if (child_inumber == FAIL) {
		printf("could not delete %s, does not exist in dir %s\n",
		       name, parent_name);
		unlock_all_nodes(inodes_locks->locks_numbers,inodes_locks->num_locks);
		free(inodes_locks);
		return FAIL;
	}

	inode_lock(child_inumber,'w');
	inode_get(child_inumber, &cType, &cdata);
	
	if (cType == T_DIRECTORY && is_dir_empty(cdata.dirEntries) == FAIL) {
		printf("could not delete %s: is a directory and not empty\n",
		       name);
		unlock_all_nodes(inodes_locks->locks_numbers,inodes_locks->num_locks);
		inode_unlock(child_inumber);
		free(inodes_locks);
		return FAIL;
	}
	
	/* remove entry from folder that contained deleted node */
	if (dir_reset_entry(parent_inumber, child_inumber) == FAIL) {
		printf("failed to delete %s from dir %s\n",
		       child_name, parent_name);
		unlock_all_nodes(inodes_locks->locks_numbers,inodes_locks->num_locks);
		inode_unlock(child_inumber);
		free(inodes_locks);
		return FAIL;
	}

	if (inode_delete(child_inumber) == FAIL) {
		printf("could not delete inode number %d from dir %s\n",
		       child_inumber, parent_name);
		unlock_all_nodes(inodes_locks->locks_numbers,inodes_locks->num_locks);
		inode_unlock(child_inumber);
		free(inodes_locks);
		return FAIL;
	}
	unlock_all_nodes(inodes_locks->locks_numbers,inodes_locks->num_locks);
	inode_unlock(child_inumber);
	free(inodes_locks);

	return SUCCESS;
}


/*
 * Lookup for a given path.
 * Input:
 *  - name: path of node
 * Returns:
 *  inumber: identifier of the i-node, if found
 *     FAIL: otherwise
 */
int lookup(char *name) {
	char full_path[MAX_FILE_NAME];
	char delim[] = "/";
	int count = 0;
	save_locks* slocks = (save_locks*) malloc(sizeof(struct save_locks));
	strcpy(full_path, name);

	/* start at root node */
	int current_inumber = FS_ROOT;

	/* use for copy */
	type nType;
	union Data data;
	char *saveptr;

	/* get root inode data */
	inode_lock(current_inumber,'r');
	slocks->locks_numbers[count] = current_inumber;
	count++;
	slocks->inumber = current_inumber;
	inode_get(current_inumber, &nType, &data);

	char *path = strtok_r(full_path, delim,&saveptr);

	/* search for all sub nodes */
	while (path != NULL && (current_inumber = lookup_sub_node(path, data.dirEntries)) != FAIL) {
		inode_lock(current_inumber, 'r');
		slocks->locks_numbers[count] = current_inumber;
		count++;
		slocks->inumber = current_inumber;
		inode_get(current_inumber, &nType, &data);
		path = strtok_r(NULL, delim,&saveptr);
	}
	unlock_all_nodes(slocks->locks_numbers,count);
	free(slocks);

	return current_inumber;
}

/*
 * Lookup for a given path used in a command i.e delete,move or destroy.
 * Input:
 *  - name: path of node
 *  - ltype: type of the lock used in the last inode of the path
 * Returns:
 *  current_inumber: structure that contains all the locks aqquired within the command,the name's inumber 
 * 					 and the total amount of locks aqquired  
 *     FAIL: otherwise
 */
save_locks* lookup_commands(char *name, char ltype) {
	char full_path[MAX_FILE_NAME];
	char delim[] = "/";
	int count = 0;
	save_locks* slocks = (save_locks*) malloc(sizeof(struct save_locks));
	strcpy(full_path, name);

	/* start at root node */
	int current_inumber = FS_ROOT;

	/* use for copy */
	type nType;
	union Data data;
	char *saveptr;

	/* get root inode data */
	if ( strcmp(name, "") == 0) {
		inode_lock(current_inumber, ltype);
	} else {
		inode_lock(current_inumber,'r');
	}
	
	slocks->locks_numbers[count] = current_inumber;
	count++;
	slocks->inumber = current_inumber;
	inode_get(current_inumber, &nType, &data);

	char *path = strtok_r(full_path, delim,&saveptr);

	/* search for all sub nodes */
	while (path != NULL && (current_inumber = lookup_sub_node(path, data.dirEntries)) != FAIL) {
		if ( strcmp("", saveptr) != 0 )
		{
			inode_lock(current_inumber,'r');
			slocks->locks_numbers[count] = current_inumber;
			count++;
			slocks->inumber = current_inumber;
			inode_get(current_inumber, &nType, &data);
			path = strtok_r(NULL, delim,&saveptr);
		}
		else
		{
			inode_lock(current_inumber,ltype);
			slocks->locks_numbers[count] = current_inumber;
			count++;
			slocks->inumber = current_inumber;
			inode_get(current_inumber, &nType, &data);
			path = strtok_r(NULL, delim,&saveptr);
		}
	}
	slocks->num_locks = count;
	return slocks;
}


/*
 * Move a file from it's current directory to a given directory.
 * Input:
 *  - current_path: path of node
 *  - new_path: new path of the node
 * Returns:
 *     SUCCESS or FAIL
 */
int move(char current_path[], char new_path[])
{
	int cp_inumber, nw_inumber;
	if ((cp_inumber = lookup(current_path)) != FAIL && (nw_inumber = lookup(new_path)) == FAIL)
	{
		// variables
		char *parent_name, *child_name, *new_parent_name, *useless;
		char current_path_copy[MAX_FILE_NAME], new_path_copy[MAX_FILE_NAME];
		int parent_inumber, child_inumber;
		int new_parent_inumber;
		
		/* use for copy */
		type pType;
		union Data pdata;
		save_locks *inodes_locks, *inodes_locks2;
		
		// gets parent and child's inumbers
		strcpy(current_path_copy, current_path);
		split_parent_child_from_path(current_path_copy, &parent_name, &child_name);
		inodes_locks = lookup_commands(parent_name, 'w');
		parent_inumber = inodes_locks->inumber;
		if ( parent_inumber == FAIL ) {
			printf("failed to move %s, invalid parent dir %s\n",
		        current_path, parent_name);
			unlock_all_nodes(inodes_locks->locks_numbers,inodes_locks->num_locks);
			free(inodes_locks);
			return FAIL;
		}
		inode_get(parent_inumber, &pType, &pdata);
		if ( pType != T_DIRECTORY ) {
			printf("failed to move %s, parent %s is not a dir\n",
		        current_path, parent_name);
			unlock_all_nodes(inodes_locks->locks_numbers,inodes_locks->num_locks);
			free(inodes_locks);
			return FAIL;
		}
		child_inumber = lookup_sub_node(child_name, pdata.dirEntries);
		inode_lock(child_inumber, 'w');
		
		// gets new parent inumber
		strcpy(new_path_copy, new_path);
		split_parent_child_from_path(new_path_copy, &new_parent_name, &useless);

		inodes_locks2 = lookup_commands(new_parent_name, 'w');
		new_parent_inumber = inodes_locks2->inumber;
		
		if ( new_parent_inumber == FAIL ) {
			printf("failed to move %s, invalid new parent dir %s\n",
		        child_name, new_parent_name);
			unlock_all_nodes(inodes_locks->locks_numbers, inodes_locks->num_locks);
			unlock_all_nodes(inodes_locks2->locks_numbers, inodes_locks2->num_locks);
			inode_unlock(child_inumber);
			free(inodes_locks);
			free(inodes_locks2);
			return FAIL;
		}

		inode_get(new_parent_inumber, &pType, &pdata);
		
		if ( pType != T_DIRECTORY ) {
			printf("failed to move %s, new parent %s is not a dir\n",
		        child_name, new_parent_name);
			unlock_all_nodes(inodes_locks->locks_numbers, inodes_locks->num_locks);
			unlock_all_nodes(inodes_locks2->locks_numbers, inodes_locks2->num_locks);
			inode_unlock(child_inumber);
			free(inodes_locks);
			free(inodes_locks2);
			return FAIL;
		}
		
		// removes entry from parent's dirEntries and adds it to the new parent's dirEntries
		dir_add_entry(new_parent_inumber, child_inumber, child_name);
		dir_reset_entry(parent_inumber, child_inumber);
		
		// unlocks all locked nodes
		unlock_all_nodes(inodes_locks->locks_numbers, inodes_locks->num_locks);
		unlock_all_nodes(inodes_locks2->locks_numbers, inodes_locks2->num_locks);
		inode_unlock(child_inumber);	
		
		// frees and returns SUCCESS
		free(inodes_locks);
		free(inodes_locks2);
		return SUCCESS;
	}
	else
	{
		return FAIL;
	}
}


/*
 * Prints tecnicofs tree.
 * Input:
 *  - fp: pointer to output file
 */
void print_tecnicofs_tree(FILE *fp){
	inode_print_tree(fp, FS_ROOT, "");
}
