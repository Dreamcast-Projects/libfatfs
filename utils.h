
#ifndef UTILS_H
#define	UTILS_H

__BEGIN_DECLS

#include "dir_entry.h"

char *remove_all_chars(const unsigned char* str, unsigned char c);
void replace_all_chars(char **str, const char* replace_chars, unsigned char replace_with);

short int generate_time(int hour, int minutes, int seconds);
short int generate_date(int year, int month, int day);
unsigned char generate_checksum(char * short_filename);
char *generate_short_filename(fatfs_t *fat, node_entry_t *curdir, char * fn, int *lfn, unsigned char *res);
fat_lfn_entry_t *generate_long_filename_entry(char * fn, unsigned char checksum, unsigned char order);

int correct_filename(const char* str);
int contains_lowercase(const char *str);

int write_entry(fatfs_t *fat, void * entry, unsigned char attr, int loc[]);
int *get_free_locations(fatfs_t *fat, node_entry_t *curdir, int num_entries);

void clear_cluster(fatfs_t *fat, unsigned int cluster_num);
unsigned int end_cluster(fatfs_t *fat, unsigned int start_cluster);

int strcasecmp( const char *s1, const char *s2 );

unsigned int get_fsinfo_nextfree(fatfs_t *fat, unsigned short fat_info);
void set_fsinfo_nextfree(fatfs_t *fat);

__END_DECLS

#endif	/* UTILS_H */

