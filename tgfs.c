/*
  FUSE: Filesystem in Userspace
  Copyright (C) 2001-2007  Miklos Szeredi <miklos@szeredi.hu>

  This program can be distributed under the terms of the GNU GPL.
  See the file COPYING.

  gcc -Wall `pkg-config fuse --cflags --libs` hello.c -o hello
*/

#define FUSE_USE_VERSION 26

#define _XOPEN_SOURCE 600

#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <malloc.h>
#include <stdlib.h>

#include "string_list.h"
#include "socket_tasks.h"
#include "jsmn/jsmn.h"

string_list* files;

static int tgfs_getattr(const char *path, struct stat *stbuf)
{
	memset(stbuf, 0, sizeof(struct stat));
	if (strcmp(path, "/") == 0) {
		stbuf->st_mode = S_IFDIR | 0755;
		stbuf->st_nlink = 2;
		return 0;
	} else {
		string_list* cur = files;
		while(cur != NULL) {
			if(strcmp(path, cur->name) == 0) {
				stbuf->st_mode = S_IFREG | 0444;
				stbuf->st_nlink = 1;
				stbuf->st_size = strlen(cur->name);
				return 0;
			}
			cur = cur->next;
		}
	}

	return -ENOENT;
}

static int tgfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
			 off_t offset, struct fuse_file_info *fi)
{
	(void) offset;
	(void) fi;

	if (strcmp(path, "/") != 0)
		return -ENOENT;

	filler(buf, ".", NULL, 0);
	filler(buf, "..", NULL, 0);
	string_list* cur = files;
	while(cur != NULL) {
		filler(buf, cur->name + 1, NULL, 0);
		cur = cur->next;
	}

	return 0;
}

static int tgfs_open(const char *path, struct fuse_file_info *fi)
{
	string_list* cur = files;
	while(cur != NULL) {
		if(strcmp(path, cur->name) == 0) {
			return 0;
		}
		cur = cur->next;
	}
	return -ENOENT;

	/*if ((fi->flags & 3) != O_RDONLY)
		return -EACCES;*/

	return 0;
}

static int tgfs_read(const char *path, char *buf, size_t size, off_t offset,
		      struct fuse_file_info *fi)
{
	(void) fi;
	FILE *fp;
	char p[1035];
	char* cmd1 = "nc localhost 2391 -c 'history ";
	char* cmd2 = " 1'";
	printf("Stage 1\n");
	size_t len = (strlen(path+1) + strlen(cmd1) + strlen(cmd2))*sizeof(char);
	char* cmd = (char*)malloc(len);
	strcpy(cmd, cmd1);
	strcat(cmd, path+1);
	strcat(cmd, cmd2);
	
	printf("Stage 2\n");
	printf("Exec: %s\n", cmd);
	printf("Try to access: %s\n", path+1);
	fp = popen(cmd, "r");
	printf("Stage 3\n");
	if(fgets(p, sizeof(p)-1, fp) == NULL) {
		return -ENOENT;
	}
	printf("Content: %s\n", p);
	len = strlen(p);
	if (offset < len) {
		if (offset + size > len)
			size = len - offset;
		memcpy(buf, p + offset, size);
	} else
		size = 0;
	return size;
}

static struct fuse_operations tgfs_oper = {
	.getattr	= tgfs_getattr,
	.readdir	= tgfs_readdir,
	.open		= tgfs_open,
	.read		= tgfs_read,
};

char* getNameFromString(char* src) {
	size_t i, j = 1;
	size_t len = strlen(src);
	char* result;
	if(len < 15)
		return NULL;
	result = (char*)malloc(sizeof(char)*len);
	i = (src[4] == ' ') ? 5 : 8;
	result[0] = '/';
	for(; i < len; i++, j++) {
		if(src[i] == ':') 
			break;
		if(src[i] == ' ') {
			result[j] = '_';
			continue;
		}
		result[j] = src[i];
	}
	if(i == len) {
		free(result);
		return NULL;
	}
	result[j+1] = 0;
	return result;
}

int main(int argc, char *argv[])
{
	files = string_list_init("/config");
	int fd = socket_init("tg_socket");
	socket_send_string(fd, "dialog_list\n", 12);
	size_t size = socket_read_answer_size(fd);
	printf("Size: %lu\n", size);
	char* json = (char*)malloc(size*sizeof(char));
	socket_read_answer(fd, json, size);
	jsmn_parser parser;
	jsmntok_t tokens[10000];
	printf("result: %s\n", json);
	//size_t tokens_count = jsmn_parse(&parser, json, size, NULL, 0);
	//printf("Tokens count: %li\n", tokens_count);
	//tokens = (jsmntok_t*)malloc(tokens_count*sizeof(jsmntok_t));
	jsmn_parse(&parser, json, size, tokens, 10000);
	for(size_t i = 0; i < 5; i++) {
		switch(tokens[i].type) {
			case 0:
				printf("UNDEF ");
				break;
			case 1:
				printf("OBJ ");
				break;
			case 2:
				printf("ARRAY ");
				break;
			case 3:
				printf("STR ");
				break;
			case 4:
				printf("PRIM ");
				break;
			default:
				break;
		}
		printf("Start: %i end: %i\n", tokens[i].start, tokens[i].end);
		for(int j = tokens[i].start; j < tokens[i].end; j++) {
			printf("%c", json[j]);
		}
		printf("<\n");
	}
	printf("Mounting...\n");
	return fuse_main(argc, argv, &tgfs_oper, NULL);
}