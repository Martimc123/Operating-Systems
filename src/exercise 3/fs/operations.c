#include "operations.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>

pthread_mutex_t global_lock = PTHREAD_MUTEX_INITIALIZER;

pthread_cond_t canPrint = PTHREAD_COND_INITIALIZER, mustStop = PTHREAD_COND_INITIALIZER;

int state = UNDEFINED;


// number of running critical commands (the ones that change the fs's state)
int running_crit_cmds = 0;


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


/**
 * Critical commands' last lines of code
*/
void crit_cmd_end() {
	running_crit_cmds--;
	
	if ( pthread_cond_broadcast(&canPrint) != 0 ) {
		perror("Error: failed to broadcast");
		exit(EXIT_FAILURE);
	}

	if ( pthread_mutex_unlock(&global_lock) != SUCCESS ) {
		perror("Error: failed to unlock");
		exit(EXIT_FAILURE);
	}
}


/*
 * Creates a new node given a path.
 * Input:
 *  - name: path of node
 *  - nodeType: type of node
 * Returns: SUCCESS or FAIL
 */
int create(char *name, type nodeType){

	if ( pthread_mutex_lock(&global_lock) != SUCCESS ) {
		perror("Error: failed to lock");
		exit(EXIT_FAILURE);
	}
	while (state == PRINTING)
	{
		if ( pthread_cond_wait(&mustStop,&global_lock) != SUCCESS ) {
			perror("Error: failed to wait");
			exit(EXIT_FAILURE);
		}
	}
	running_crit_cmds++;

	
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
		
		crit_cmd_end();
		
		return FAIL;
	}

	inode_get(parent_inumber, &pType, &pdata);


	if(pType != T_DIRECTORY) {
		printf("failed to create %s, parent %s is not a dir\n",
		        name, parent_name);
		unlock_all_nodes(inodes_locks->locks_numbers,inodes_locks->num_locks);
		free(inodes_locks);
		
		crit_cmd_end();
		
		return FAIL;
	}
	

	if (lookup_sub_node(child_name, pdata.dirEntries) != FAIL) {
		printf("failed to create %s, already exists in dir %s\n",
		       child_name, parent_name);
		unlock_all_nodes(inodes_locks->locks_numbers,inodes_locks->num_locks);
		free(inodes_locks);
		
		crit_cmd_end();
		
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
		
		crit_cmd_end();
		
		return FAIL;
	}
	
	if (dir_add_entry(parent_inumber, child_inumber, child_name) == FAIL) {
		printf("could not add entry %s in dir %s\n",
		       child_name, parent_name);
		unlock_all_nodes(inodes_locks->locks_numbers,inodes_locks->num_locks);
		inode_unlock(child_inumber);
		free(inodes_locks);
		
		crit_cmd_end();
		
		return FAIL;
	}
	unlock_all_nodes(inodes_locks->locks_numbers,inodes_locks->num_locks);
	inode_unlock(child_inumber);
	free(inodes_locks);
	
	crit_cmd_end();

	return SUCCESS;
}

/*
 * Deletes a node given a path.
 * Input:
 *  - name: path of node
 * Returns: SUCCESS or FAIL
 */
int delete(char *name){
	
	if ( pthread_mutex_lock(&global_lock) != SUCCESS ) {
		perror("Error: failed to lock");
		exit(EXIT_FAILURE);
	}
	while (state == PRINTING)
	{
		if ( pthread_cond_wait(&mustStop,&global_lock) != SUCCESS ) {
			perror("Error: failed to wait");
			exit(EXIT_FAILURE);
		}
	}
	running_crit_cmds++;

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
		
		crit_cmd_end();
		
		return FAIL;
	}

	inode_get(parent_inumber, &pType, &pdata);

	if(pType != T_DIRECTORY) {
		printf("failed to delete %s, parent %s is not a dir\n",
		        child_name, parent_name);
		unlock_all_nodes(inodes_locks->locks_numbers,inodes_locks->num_locks);
		free(inodes_locks);
		
		crit_cmd_end();
		
		return FAIL;
	}

	child_inumber = lookup_sub_node(child_name, pdata.dirEntries);

	if (child_inumber == FAIL) {
		printf("could not delete %s, does not exist in dir %s\n",
		       name, parent_name);
		unlock_all_nodes(inodes_locks->locks_numbers,inodes_locks->num_locks);
		free(inodes_locks);
		
		crit_cmd_end();
		
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
		
		crit_cmd_end();
		
		return FAIL;
	}
	
	/* remove entry from folder that contained deleted node */
	if (dir_reset_entry(parent_inumber, child_inumber) == FAIL) {
		printf("failed to delete %s from dir %s\n",
		       child_name, parent_name);
		unlock_all_nodes(inodes_locks->locks_numbers,inodes_locks->num_locks);
		inode_unlock(child_inumber);
		free(inodes_locks);
		
		crit_cmd_end();
		
		return FAIL;
	}

	if (inode_delete(child_inumber) == FAIL) {
		printf("could not delete inode number %d from dir %s\n",
		       child_inumber, parent_name);
		unlock_all_nodes(inodes_locks->locks_numbers,inodes_locks->num_locks);
		inode_unlock(child_inumber);
		free(inodes_locks);
		
		
		crit_cmd_end();
		
		return FAIL;
	}
	unlock_all_nodes(inodes_locks->locks_numbers,inodes_locks->num_locks);
	inode_unlock(child_inumber);
	free(inodes_locks);

	crit_cmd_end();
	
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

int lookupmove(char *name) {
	char full_path[MAX_FILE_NAME];
	char delim[] = "/";
	strcpy(full_path, name);

	/* start at root node */
	int current_inumber = FS_ROOT;

	/* use for copy */
	type nType;
	union Data data;
	char *saveptr;

	/* get root inode data */
	inode_get(current_inumber, &nType, &data);

	char *path = strtok_r(full_path, delim,&saveptr);

	/* search for all sub nodes */
	while (path != NULL && (current_inumber = lookup_sub_node(path, data.dirEntries)) != FAIL) {
		inode_get(current_inumber, &nType, &data);
		path = strtok_r(NULL, delim,&saveptr);
	}
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

	char *path = strtok_r(full_path, delim, &saveptr);

	/* search for all sub nodes */
	while (path != NULL && (current_inumber = lookup_sub_node(path, data.dirEntries)) != FAIL) {
		if ( strcmp("", saveptr) != 0 )
		{
			inode_lock(current_inumber, 'r');
			slocks->locks_numbers[count] = current_inumber;
			count++;
			slocks->inumber = current_inumber;
			inode_get(current_inumber, &nType, &data);
			path = strtok_r(NULL, delim, &saveptr);
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
	if ( pthread_mutex_lock(&global_lock) != SUCCESS ) {
		perror("Error: failed to lock");
		exit(EXIT_FAILURE);
	}
	while (state == PRINTING)
	{
		if ( pthread_cond_wait(&mustStop,&global_lock) != SUCCESS ) {
			perror("Error: failed to wait");
			exit(EXIT_FAILURE);
		}
	}
	running_crit_cmds++;

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
		
		// gets parent and child's inumbers
		
		strcpy(current_path_copy, current_path);
		split_parent_child_from_path(current_path_copy, &parent_name, &child_name);
		parent_inumber = lookupmove(parent_name);
		if ( parent_inumber == FAIL ) {
			printf("failed to move %s, invalid parent dir %s\n",
		        current_path, parent_name);
			
			crit_cmd_end();
			
			return FAIL;
		}
		inode_get(parent_inumber, &pType, &pdata);
		if ( pType != T_DIRECTORY ) {
			printf("failed to move %s, parent %s is not a dir\n",
		        current_path, parent_name);
			
			crit_cmd_end();
			
			return FAIL;
		}
		child_inumber = lookup_sub_node(child_name, pdata.dirEntries);
		
		// gets new parent inumber
		strcpy(new_path_copy, new_path);
		split_parent_child_from_path(new_path_copy, &new_parent_name, &useless);

		new_parent_inumber = lookupmove(new_parent_name);
		
		if ( new_parent_inumber == FAIL ) {
			printf("failed to move %s, invalid new parent dir %s\n",
		        child_name, new_parent_name);
			
			crit_cmd_end();
			
			return FAIL;
		}

		inode_get(new_parent_inumber, &pType, &pdata);
		
		if ( pType != T_DIRECTORY ) {
			printf("failed to move %s, new parent %s is not a dir\n",
		        child_name, new_parent_name);
			
			crit_cmd_end();
			
			return FAIL;
		}
		
		// removes entry from parent's dirEntries and adds it to the new parent's dirEntries
		dir_add_entry(new_parent_inumber, child_inumber, child_name);
		dir_reset_entry(parent_inumber, child_inumber);
		
		// unlocks all locked nodes
		crit_cmd_end();
		
		return SUCCESS;
	}
	else
	{
		crit_cmd_end();
		
		return FAIL;
	}
}


/*
 * Prints tecnicofs tree.
 * Input:
 *  - filename: name of file
 */
int print_tecnicofs_tree(char *filename){
	int res;
	FILE *fp;
	if ( pthread_mutex_lock(&global_lock) != SUCCESS ) {
		perror("Error: failed to lock");
		exit(EXIT_FAILURE);
	}
	while ( running_crit_cmds != 0 )
	{
		if ( pthread_cond_wait(&canPrint,&global_lock) != SUCCESS ) {
			perror("Error: failed to wait");
			exit(EXIT_FAILURE);
		}
	}
	state = PRINTING;
	
	if ( (fp = fopen(filename, "w")) == NULL ) {
		perror("Print: failed to open file");
		exit(EXIT_FAILURE);
	}
	
	res = inode_print_tree(fp, FS_ROOT, "");
	
	if ( fclose(fp) != 0 ) {
		perror("Print: failed to close file");
		exit(EXIT_FAILURE);
	}
	
	state = NOTPRINTING;
	if ( pthread_cond_broadcast(&mustStop) != SUCCESS ) {
		perror("Error: failed to broadcast");
		exit(EXIT_FAILURE);
	}
	if ( pthread_mutex_unlock(&global_lock) != SUCCESS ) {
		perror("Error: failed to unlock");
		exit(EXIT_FAILURE);
	}
	return res;
}

