#ifndef LUT1D_H
#define LUT1D_H

/**
	@author Daniel Gollas Gilman <dgollas@ollin.com.mx>
        //Class to encapsulate a 1D LUT, provides methods for reading a 1D LUT File and converting a value into another.
*/

struct LUT1DPair
{
  unsigned int input;
  unsigned int output;
};

class LUT1D{
public:
    LUT1D();

    ~LUT1D();
    
    public:
	LUT1DPair *lut;
	unsigned int size;
	unsigned int fromDepth;
	unsigned int toDepth;
	int loadLUT(const char *fileName);
	unsigned int getOutput(unsigned int input);

   private:
	int lerp();
};

#endif
