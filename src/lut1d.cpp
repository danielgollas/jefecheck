#include "lut1d.h"
#include "iostream"
#include "fstream"
#include <string.h>
#include <stdlib.h>
//Class to encapsulate a 1D LUT, provides methods for reading a 1D LUT File and converting a value into another.


LUT1D::LUT1D()
{
}


LUT1D::~LUT1D()
{
}

int LUT1D::loadLUT(const char *fileName)
{
  using namespace std;
   printf("*********\nREAD 1D LUT\n*********\n");
   printf("Opening file %s...",fileName);
  ifstream fs(fileName);
  if(!fs)
  {
   printf(" LUT ERROR: Could not Open LUT file %s\n",fileName);
   return 0;
   printf("*********\n");
  }
  //printf("done\n");
  printf("Reading 1DLUT file\n");
  #define MAXCHARS 40
  char line[40];
  //get header
  fs.getline(line,40);
  printf("Header: %s\n",line);
  if(strstr(line,"#JefeCheck LUT Header v1.0")==0)
  {
     printf("File is not a JefeCheck LUT Header\n");
     return 0;
  }
  fs.getline(line,MAXCHARS);
  size=atoi(line);
  printf("LUT File has %i entries\n",size);
  printf("Allocating memory (%i Bytes)...",sizeof(LUT1DPair)*size);
  lut = new LUT1DPair[size];  
  //printf("done\n");
  fs.getline(line,MAXCHARS);
  fromDepth=atoi(line);
  printf("Input bit depth: %i\n",fromDepth);
  
  fs.getline(line,MAXCHARS);
  toDepth=atoi(line);
  printf("Output bit depth: %i\n",toDepth);
  printf("Reading %i entries...",size);
 for(int i=0;i<size;i++)
 {
   fs.getline(line,MAXCHARS);
   lut[i].output=atoi(strtok(line," "));
   lut[i].input=i;
 }
 //printf("done\n");
 printf("*********\n");

  #undef MAXCHARS
 return 1;
}

unsigned int LUT1D::getOutput(unsigned int input)
{
  if(lut!= NULL && input<size && input >=0)
  {
	return lut[input].output;
  }
  return 0;
}
