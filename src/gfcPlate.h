#ifndef GFCPLATE_H
#define GFCPLATE_H
//#pragma once


#include <glad/glad.h>
#include "gfcSequence.h"
#include "trilerp.h"
#include "platefxparams.h"
#include "gfcStructures.h"
#include "qt/gfcplategui_qt.h"
#include "gfcplatedrawparams.h"
#include "gfcframe.h"
#include "gfcfxstack.h"

#include "gfcnetremotepointerinfo.h"
#include "gfcpointerstorage.h"

#include "gfcpicknotifee.h"

#include "gfchistogram.h"
#include "gfchistogramglwindow.h"

//#include "mtpoly.h"

class gfcRenderParams;
struct GFLC_BITMAP;
class gfcSequence;
class gfcFrame;
class GlViewport;

class gfcPlateRenderParams
{
	public:
	std::string filename;
	float scale;
	std::string format;
	
};

class gfcPlate: public gfcPickNotifee
    {
    
    public:
        
        void zoomPlate(float zoom);
        void panPlate(float panX, float panY);
		void rotatePlate(float rotation);
        void toggleTextMode(int reset=0); ///if reset then the toggle goes to the first mode. Useful to synchronize all plates.
        void toggleHistogramMode(int reset=0); ///if reset then the toggle goes to the first mode. Useful to synchronize all plates.
        
		int getActiveFromGUI();
        
        gfcNetTransformationInfo getTransformations();
        void setTransformations(gfcNetTransformationInfo info);
        
        gfcNetPlateStateInfo getPlateStateInfo();
        void setPlateStateInfo(gfcNetPlateStateInfo info);
        
		gfcNetPlateColorCorrectionInfo getPlateColorCorrectionInfo();
		void setPlateColorCorrectionInfo(gfcNetPlateColorCorrectionInfo);

        virtual int pickNotify(gfcPickNotifyParameters &params);
        
    void setTrack(int value);
	void setZoom(float value);
	void setTX(int value);
	void setTY(int value);
	void setRZ(float value);
	void setFlip(int value);
	void setFlop(int value);
	
	void setGamma(float value, int relative=0);
	void setExposure(float value,int relative=0);
	void setBrightness(float value,int relative=0);
	void setContrast(float value,int relative=0);
	void setSaturation(float value,int relative=0);

	void scrollLUT(int direction);
	void setLUT(int lutIndex);

	void toggleFlop();
	void toggleFlip();

	void setCrop(int value);
	void setAspect(std::string value);

	void toggleChannelR();
	void toggleChannelG();
	void toggleChannelB();
	void toggleChannelA();

	void setChannelR(int value);
	void setChannelG(int value);
	void setChannelB(int value);
	void setChannelA(int value);
    
	void setRGBAGUIFromCurrentMasks();

	void recompileSuperShader();
	void startSuperShader();
	void stopSuperShader();

	void fitToViewport();

        gfcPlate(void);
        ~gfcPlate(void);
        int textureID[4];
        int quadID;
        char track;
        float scale;
        float tX;
        float tY;
        float rX;
        float rY;
        float rZ;
        
		//*******super shader variables***********
        bool rMask;
        bool gMask;
        bool bMask;
        bool aMask;

		float gamma;
		float exposure;
		float brightness;
		float contrast;
		float saturation;
		int lutID;
		int lutType;
		int lutSize;

		GLhandleARB ssProgram;
		GLhandleARB ssVertexShader;
		GLhandleARB ssFramgmentShader;
		std::string ssVertexSource;
		std::string ssFragmentSource;

		int ssVertexCompiled;
		int ssFramgentCompiled;
		
		int ssProgramCompleteLUT;
		int ssProgramCompleteLUTGammaExp;
		int ssProgramCompleteLUTGammaExpBCS;
		int ssProgramCompleteLUTBCS;
		int ssProgramCompleteGammaExp;
		int ssProgramCompleteGammaExpBCS;
		int ssProgramCompleteBCS;

		int ssProgramCreated;
		
		//the following variables determine the current state of the shader.
		int usingLUT;
		int currentLUTType;
		int usingGammaExp;
		int usingBCS;
		int usingRGBAMasks;
		int usingTextureType; //if the texture is compressed, the shader needs a different type of sampler.
		
		//we set up this variable according to the frame's type: compressed, normal, etc. We don't get it directly from the frame because
		//it might be different, e.g. when using FXs, the frame's image type could be compressed, but the one that will be rendered is a 2Drect.
		int textureTypeForSuperShader; 
		unsigned int textureIDforSuperShader;
		

		int useShader;

		std::string getInfoLog( GLhandleARB obj );

		//**************************
        bool showPreview;
        
        gfcPlateGUI* myGUI;
        
        void updateValuesFromGUI(); //this update ALL the values
        void updateValueToGUI();

		void updateColorCorrectionValuesFromGUI(); //this updates just the color values
		void updateTransformationValuesFromGUI(); //thisupdates just the transformation values

        void updateRot(float timeStep=1, bool flip=false, bool flop=false);
        
        void draw();
        void setViewport(int x, int y, int w, int h);
        
        void clearHistogramCache();
        void setHistogramQuality(int pQUality);
        
        void setRenderModeSelection(int value);
        
        void draw3Drect(int currentFrame=1);
        void draw3DrectWithFX(int currentFrame=1);
        void drawForSelection(int currentFrame=1);
        
        void drawForFX(PlateFXParams params);
        void updateAnimations();
		void drawRemotePointers();
		void setRemotePointerOptions(int pfontSize, int psize, bool pfade, int pfadeDelay, bool ptrail, float ptrailLenght,int pColor);
        void setTextDisplayOptions(int pfontSize, float pcolor, float popacity);
        
        
        void capturePointerCoords();
        void storePointerInfo(gfcNetRemotePointerInfo info);
        void removePointerInfo(gfcNetRemotePointerInfo info);
        
        void processNetFXAttribInfo(gfcNetFXAttribInfo &info);
        void processNetFXCommonInfo(gfcNetFXCommonInfo &info);
        
        void savePlateSessionParameters(XMLNode &plateNode);
        void loadPlateSessionParameters(XMLNode &plateNode);
        
        
        
        Rectang getFboVpSizeFromTrack(int currentFrame); //used to obtain the correct size of the original texture so we can create an apropiate FBO+texture. Takes into account frame slices.
        bool createFBO();
        float aspect;
        int cropMode;
        bool cropOn;
	gfcSequence *sequence;
        bool active;
        void resetTransforms(void);
		void resetColorCorrection();
	Rectang rect;
	Rectang fboVP;
	bool showText;
	bool extensiveText;
	// Cached text overlay texture
	GLuint textOverlayTexID;
	int textOverlayTexW, textOverlayTexH;
	std::string textOverlayCachedLabel;
	bool showHelp;
	int showHistogram;
	bool showVectorscope;
	int numSlices;
	GlViewport *vp; //pointer to the vp that this plate belongs too, same with all widgets under that
	gfcFrame *tf;
	CubeLUT cube;
	
	gfcFXStack fxStack;
	
	//Rectang aoi;
	
	GLuint fbo;
	GLuint fboTexture;
	
	GLuint fbov[3];
	GLuint fboTexturev[3];
	int fboTexturevCount;
	GLuint fbo8bit;
	GLuint fbo8bitTexture;
	
	int activeFBO;
	
	bool forRender; //turn on when rendering, turn off when drawing to screen. 
	//gfcPlateRenderParams renderParams;
	gfcRenderParams renderParams;
	//MtPoly poly;
	
	//network pointer vars
	Vec3D getCursorPositionIn2DSpace(int px, int py);
	float prevPointerX;
	float prevPointerY;
	float prevWinCoordX;
	float prevWinCoordY;
	
	bool forceSingleBufferedFX;
private:

	void buildShader(int useLut,int useGammaExp, int useBCS, int useRGBMask,int textureType);
    void calculatePolySizesCropEtc();
    void drawCropBars();
    void drawAOIOverlay();
	
	gfcPickObject aoiPickNW;
	gfcPickObject aoiPickSW;
	gfcPickObject aoiPickNE;
	gfcPickObject aoiPickSE;
	gfcPickObject aoiPickMove;

	gfcPickObject framePick;

	void drawDO();
	void startTransform();
	
	
	void drawVectorscope();
	
    bool multiFormatFBOSupported;
    std::map<int, gfcHistogram> histogramCache; //this map stores the histogramCache, and links each frame in the timeline to a cache.
    int histogramQuality;
    int getHistogramFromTexture(GLuint theTexture, int sizeX, int sizeY,gfcHistogram &result,GLuint readFormat);
    void updateHistogram();
    void startViewport();
    void endViewport();
    void startAlphaBackground();
    void stopAlphaBackround();
    void endTransform();
    void drawText();
    void drawTextureRectangleWarning();
    void getFrameAndSequence();
    
    int renderModeSelection;
    
    
    
    bool flip;
    bool flop;
    float rotationsTimeSinceLastRedraw;

    gfcPointerStorage pointerStorage; //stores the remote pointers and the previous ones for the trails
    int remotePointerSize;
    int remotePointerFontSize;
	int remotePointerColor;
    
    int textDisplaySize;
    float textDisplayColor;
    float textDisplayOpacity;
    
    int drawingChoice; //will tell us if we need to draw directly or with FXs or with something else.
    
    unsigned short textMode; ///0=no text, 1 text, 2 extended text (metadata if available).
    unsigned short drawFor; ///what are we drawing for? normal,preview or FX
    
    //variables used internally while drawing, stored in the class instead of passing them around.
    	gfcFrame theFrame;
    	int currentFrame;
    	gfcSequence* theSequence;
	int polySizeX;
	int polySizeY;
	gfcRectangf texCoords; //this is used for drawing and setting the texture coordinates
	gfcRectangf textureSize; //this defines the real size of the texture, not necesseraly the same as texCoords (e.g. on S3TC texcoords are normalized), and not neccessaraly the same as the polySize (aspect ration changes)
	char label[12000];
	std::string labelString;
	Rectang cropBarTop;
	Rectang cropBarBottom;
	gfcRectang viewport; //needs to be set correctly so that the plate knows how and where to draw itself.
	bool areaOfIntrestOn;
	gfcRectang aoi;
	GLuint target;
	
	
	//HISTOGRAM VARIABLES
    	void drawHistogram();
    	gfcHistogramGLWindow histogramWindow;
    
	bool histogramSelected;
	gfcPickColor histogramPickColor;
	
	bool histogramCornerSelected;

   };

#endif
