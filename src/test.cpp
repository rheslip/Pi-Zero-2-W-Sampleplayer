
#include <dirent.h> 
#include <stdio.h> 
#include <string.h>
#include <stdlib.h>

#define NORMAL_COLOR  "\x1B[0m"
#define GREEN  "\x1B[32m"
#define BLUE  "\x1B[34m"

struct fileinfo {
	char name[80];
	bool isdir;
} files[100];


int comp(const void *a,const void *b) {
return (strcmp((char *)a,(char *)b));
}


/* function to get the content of a given folder */

int get_dir_content(char * path)
{
  DIR * d = opendir(path); // open the path
  if(d==NULL) return 0; // if was not able, return
  struct dirent * dir; // for the directory entries
  int numfiles=0;
  
  while ((dir = readdir(d)) != NULL) // if we were able to read somehting from the directory
    {
      if(dir-> d_type != DT_DIR) {// if the type is not directory just print it with blue color
        //printf("%s%s\n",BLUE, dir->d_name);
		strcpy(files[numfiles].name,dir->d_name);
		files[numfiles].isdir=0;
		++ numfiles;
	  }
      else
      if(dir -> d_type == DT_DIR && strcmp(dir->d_name,".")!=0 && strcmp(dir->d_name,"..")!=0 ) // if it is a directory
      {
        //printf("%s%s\n",GREEN, dir->d_name); // print its name in green
        //char d_path[255]; // here I am using sprintf which is safer than strcat
        //sprintf(d_path, "%s/%s", path, dir->d_name);
       // show_dir_content(d_path); // recall with the new path
	   strcpy(files[numfiles].name,dir->d_name);
		files[numfiles].isdir=1;
		++ numfiles;
      }
    }
    closedir(d); // finally close the directory
	qsort (files, numfiles, sizeof(fileinfo), comp);
	return numfiles;
}

int main(int argc, char **argv)
{
 int i,j;
  printf("%s\n", NORMAL_COLOR);
  i=get_dir_content("./samples");
  printf("%s\n", NORMAL_COLOR);
  

  for (j=0;j<i;++j) {
	printf ("%s %d\n",files[j].name,files[j].isdir);
  }
  return(0);
}