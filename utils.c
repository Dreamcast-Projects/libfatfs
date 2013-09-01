

#include <errno.h>
#include <ctype.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "utils.h"
#include "dir_entry.h"

/* 'str' is the string you want to remove characters 'c' from */
char *remove_all_chars(const char* str, char c) {
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

/* PRE: str must be either NULL or a pointer to a 
 * (possibly empty) null-terminated string. */
void strrev(char *str) {
  char temp, *end_ptr;

  /* If str is NULL or empty, do nothing */
  if( str == NULL || !(*str) )
    return;

  end_ptr = str + strlen(str) - 1;

  /* Swap the chars */
  while( end_ptr > str ) {
    temp = *str;
    *str = *end_ptr;
    *end_ptr = temp;
    str++;
    end_ptr--;
  }
}

/* 'replace_chars' contains characters that you want to replace with character 'replace_with' in string str */
void replace_all_chars(char **str, const char* replace_chars, char replace_with) {
	char *c;
	
	c = *str;
   
	while (*c)
	{
	   if (strchr(replace_chars, *c))
	   {
		  *c = replace_with;
	   }

	   c++;
	}
}

/* Returns 1 if a lowercase character is found in 'str', reurns 0 otherwise */
int contains_lowercase(const char *str)
{
	char *c = str;
	
	while (*c)
    {
		if(islower(*c))
		{
			return 1;
		}
		c++;
    }

	return 0;
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

char *generate_file_path()
{
	return NULL;
}

char *generate_short_filename(node_entry_t *curdir, char * fn)
{
	int diff = 1;
	char *temp1 = NULL;
	char *temp2 = NULL;
	
	char *copy = NULL;
	char *integer_string = malloc(7); 
	
	char *fn_temp;
	char *filename = malloc(11); // File length max is 8 chars
	char *ext = malloc(4);      // Extension length max is 3 chars
	char *fn_final;
	
	// 1. Remove all spaces. For example "My File" becomes "MyFile".
	copy = remove_all_chars(fn, ' ');
	
	printf("Remove Spaces: %s\n", copy);
	
	// 3. Translate all illegal 8.3 characters( : + , ; = [ ] ) into "_" (underscore). For example, "The[First]Folder" becomes "The_First_Folder".
	replace_all_chars(&copy, ":+,;=[]", '_');
	
	printf("Translate illegal Characters: %s\n", copy);

	/* 2. Initial periods, trailing periods, and extra periods prior to the last embedded period are removed.
	For example ".logon" becomes "logon", "junk.c.o" becomes "junkc.o", and "main." becomes "main". */

	temp1 = malloc(strlen(fn)+1);
	
	strncpy(filename, strtok(copy, "."), 10); // Copy Filename
	filename[10] = '\0';
	
	strncpy(ext, strtok(NULL, "."), 3);      // Copy Extension
	ext[3] = '\0';
	
	temp2 = strtok(NULL, ".");
	
	while(temp2 != NULL)
	{
		memset(temp1, 0, strlen(fn)+1);
		strcat(temp1, filename);
		strcat(temp1, ext);
		
		strncpy(filename, temp1, 10); // Copy Filename
		filename[10] = '\0';
		
		strncpy(ext, temp2, 3);
		ext[3] = '\0';
		
		temp2 = strtok(NULL, ".");
	}
	
	printf("Filename: %s Extension: %s \n", filename, ext);

/*
4. If the name does not contain an extension then truncate it to 6 characters. If the names does contain
an extension, then truncate the first part to 6 characters and the extension to 3 characters. For
example, "I Am A Dingbat" becomes "IAmADi" and not "IAmADing.bat", and "Super Duper
Editor.Exe" becomes "SuperD.Exe".*/
/*
	// Truncate the filename to 6 chars(length)
	if(strlen(filename) > 6)
		filename[6] = '\0';

	if(strlen(ext) > 3)
		ext[3] = '\0';  //truncate it to 3 characters (ext)
	
	printf("Truncate - Filename: %s Extension: %s \n", filename, ext);
	*/

/*
5. If the name does not contain an extension then a "-1" is appended to the name. If the name does
contain an extension, then a "-1" is appended to the first part of the name, prior to the extension. For
example, "MyFile" becomes "MyFile-1, and "Junk.bat" becomes "Junk-1.bat". This numeric value is
always added to help reduce the conflicts in the 8.3 name space for automatic generated names. 
*/

	if(strlen(filename) > 8)
	{
		// Concate the filename and '~'
		memset(temp1, 0, strlen(fn)+1);
		strcat(temp1, filename);
		
		// Append the Value
		memset(integer_string, 0, 7);
		sprintf(integer_string, "~%d", diff++); // Increment diff here for maybe future use
		strcat(temp1, integer_string);
		
		// Append the period
		strcat(temp1, ".");
		
		// Append the ext
		strcat(temp1, ext);
		
		fn_temp = temp1; // Contains the filename and ext
		
		printf("Getting closer - Filename: %s\n", fn_temp);
	}
	else
	{
		// Append the filename
		memset(temp1, 0, strlen(fn)+1);
		strcat(temp1, filename);
		
		while(strlen(temp1) < 8) // Append spaces to fill end
			strcat(temp1, ' ');
		
		// Append the period
		strcat(temp1, ".");
		
		// Append the ext
		strcat(temp1, ext);
		
		fn_temp = temp1; // Contains the filename and ext
		
		printf("Getting closer - Filename: %s\n", fn_temp);
	}

/*
6. This step is optional dependent on the name colliding with an existing file. To resolve collisions
the decimal number, that was set to 1, is incremented until the name is unique. The number of
characters needed to store the number will grow as necessary from 1 digit to 2 digits and so on. If the
length of the basis (ignoring the extension) plus the dash and number exceeds 8 characters then the
length of the basis is shortened until the new name fits in 8 characters. For example, if "FILENA-
1.EXE" conflicts the next names tried are "FILENA-2.EXE", "FILENA-3.EXE", ..., "FILEN-
10.EXE", "FILEN-11.EXE", etc.
*/

	while(isChildof(curdir, fn_temp) != NULL) // Should equal to null because that means it doesnt exist.
	{
		if(diff > 99999)
		{
			printf("Too many entries(Short Entry Name) with the same name(beginning) :p.\n");
			return NULL;
		}
		
		// Rebuild the file name
		memset(temp1, 0, strlen(fn)+1);
		strcat(temp1, filename); // Cat six chars intro temp1
		
		memset(integer_string, 0, 7);
		sprintf(integer_string, "~%d", diff++); // Increment diff here for maybe future use
		
		memcpy(temp1 + (8 - strlen(integer_string)), integer_string, strlen(integer_string));
		
		temp1[8] = '\0';
		
		// Append the period
		strcat(temp1, ".");
		
		// Append the ext
		strcat(temp1, ext);
		
		fn_temp = temp1; // Contains the filename and ext	
		
		printf("Do over - Filename: %s\n", fn_temp);
	}
	
	// Short Entry Name is unique. Now to copy it to its final string so it can fit nice and snug.
	fn_final = malloc(strlen(fn_temp)+1);
	memset(fn_final, 0, strlen(fn_temp)+1);
	strncpy(fn_final, fn_temp, strlen(fn_temp));
	fn_final[strlen(fn_temp)] = '\0';
	
	printf("Final Version - Filename: %s\n", fn_final);

	// Free memory
	free(temp1);
	free(copy);
	
	free(filename);
	free(ext);
	free(integer_string);
	
	return fn_final;
}
