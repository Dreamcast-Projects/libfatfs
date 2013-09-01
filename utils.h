/* 
 * File:   utils.h
 * Author: andy
 *
 * Created on August 30, 2013, 1:55 AM
 */

#ifndef UTILS_H
#define	UTILS_H

struct node_entry;
typedef struct node_entry node_entry_t;

char *remove_all_chars(const char* str, char c);
void replace_all_chars(char **str, const char* replace_chars, char replace_with);
char *trimwhitespace(char *str);
int correct_filename(const char* str);
char *generate_short_filename(node_entry_t *curdir, char * fn);
int contains_lowercase(const char *str);


#endif	/* UTILS_H */

