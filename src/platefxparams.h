#ifndef PLATEFXPARAMS_H
#define PLATEFXPARAMS_H
#include <vector>
/**
An object that contains parameters that a plate can use for drawing the quad and calling verti and tex coord gl calls with the correct parameters acording to the FX that is being applied at that point. Parameters will include things like texture coordinates for each gl texturing unit. Passing an object instead of individual parameters makes it easier to add new parameters without messing with the method's declaration.

	@author Daniel Gollas Gilman <dgollas@ollin.com.mx>
*/

struct FXTexCoords
{
   float s;
   float t;
   float r;
   
   /*
   x,t-----------t,s
   *              *
   *              *
   *              *
   x,y-----------s,y
   */
   
   float x;
   float y;
   float z;
};

enum PLATEPASSES{FXPASS_FIRST=0, FXPASS_INTERMEDIATE,FXPASS_HISTOGRAM_FBO ,FXPASS_LAST}; //first pass draws the same as a normal (no FX) pass BUT without transforms. Intermediates are dumb and depend on the params filled by the FX's bind. Last pass draws the same as a normal but the texture is the one in the FBO, this pass also draws the text.

class PlateFXParams{
public:
    PlateFXParams();

    ~PlateFXParams();
      const PlateFXParams& operator= ( const PlateFXParams &params );
      int pass; //the pass number 
      int numberOfTextures; //how many textures this shader uses.
      FXTexCoords texCoords[4]; //up to 4 textures can be used in an FX, this represents the texCoords of each of those textures
      int sizeX; //the size the plate should be.
      int sizeY;
      int currentFrame;
      int FBOTextureID;
};

#endif
