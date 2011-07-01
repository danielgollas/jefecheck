#ifndef GFCPLATEDRAWPARAMS_H
#define GFCPLATEDRAWPARAMS_H

/**
	@author Daniel Gollas Gilman <gollas@jefecorp.com>
*/
class gfcPlateDrawParams{
public:
    gfcPlateDrawParams(int forRender=0, int forPreview=0);

    ~gfcPlateDrawParams();
	
    int forRender; //tells the plate if it is being drawn for render.
    int forPreview; //usuall
};

#endif
