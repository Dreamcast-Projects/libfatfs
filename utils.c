

#include <errno.h>
#include <ctype.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "utils.h"

char * remove_all_chars(const char* str, char c) {
	char *copy = (char *)malloc(strlen(str));
	strcpy(copy,str);
    	char *pr = copy, *pw = copy;
    	while (*pr) {
        	*pw = *pr++;
        	pw += (*pw != c);
   	}
    	*pw = '\0';
	
	return copy;
}

/* http://stackoverflow.com/questions/122616/how-do-i-trim-leading-trailing-whitespace-in-a-standard-way */
char *trimwhitespace(char *str)
{
  char *end;

  // Trim leading space
  while(isspace(*str)) str++;

  if(*str == 0)  // All spaces?
    return str;

  // Trim trailing space
  end = str + strlen(str) - 1;
  while(end > str && isspace(*end)) end--;

  // Write new null terminator
  *(end+1) = 0;

  return str;
}

/* http://stackoverflow.com/questions/1071542/in-c-check-if-a-char-exists-in-a-char-array */
int correct_filename(const char* str)
{
   const char *invalid_characters = "\\?:*\"><|";
   char *c = str;
   
   /* Make sure the string is long and short enough */
   if(strlen(str) > 255 || strlen(str) <= 0)
   {
       printf("Invalid filename, strlen: %d\n", strlen(str));
       errno = ENAMETOOLONG;
       return -1;
   }
   
   while (*c)
   {
       if (strchr(invalid_characters, *c))
       {
          printf("Invalid filename, char found: %c\n", *c);
          errno = ENAMETOOLONG;
          return -1;
       }

       c++;
   }

   return 0;
}
