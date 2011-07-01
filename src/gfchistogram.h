#ifndef GFCHISTOGRAM_H
#define GFCHISTOGRAM_H

#include <vector>
#include "glew.h"
#include "gfcpicknotifee.h"

/**
	@author Daniel Gollas Gilman <gollas@jefecorp.com>
*/
class gfcHistogram{
public:
    
     gfcHistogram();

    ~gfcHistogram();
    
    //copy operator
    gfcHistogram& operator=(const gfcHistogram&);
    
    //std::vector<std::vector<int> > values;
    float valuesA[3][256];
    void processPixels(int width, int height,unsigned  char *pixels);
    void processPixels(int width, int height,unsigned short *pixels);
    
    void draw(int caching=0);
    
    gfcPickColor myUniqueColor;
    gfcPickColor myUniqueCornerColor;
     
    float scaleX;
    float scaleY;
    int posX;
    int posY;
    int drawForPicking;
    
    
    int hWidth;
    int quality;
private:
    float maxValue;
    void normalize();
};

#endif
