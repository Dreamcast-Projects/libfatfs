/* 
 * File:   utils.h
 * Author: andy
 *
 * Created on August 30, 2013, 1:55 AM
 */

#ifndef UTILS_H
#define	UTILS_H

#include "dir_entry.h"

char *remove_all_chars(const unsigned char* str, unsigned char c);
void replace_all_chars(char **str, const char* replace_chars, unsigned char replace_with);

int correct_filename(const char* str);
char *generate_short_filename(fatfs_t *fat, node_entry_t *curdir, char * fn, int *lfn, unsigned char *res);
fat_lfn_entry_t *generate_long_filename_entry(char * fn, unsigned char checksum, unsigned char order);
int write_entry(fatfs_t *fat, void * entry, unsigned char attr, int loc[]);
int *get_free_locations(fatfs_t *fat, node_entry_t *curdir, int num_entries);
unsigned char generate_checksum(char * short_filename);
int contains_lowercase(const char *str);
void clear_cluster(fatfs_t *fat, int cluster_num);

short int generate_time(int hour, int minutes, int seconds);
short int generate_date(int year, int month, int day);

int strcasecmp( const char *s1, const char *s2 );



#endif	/* UTILS_H */

