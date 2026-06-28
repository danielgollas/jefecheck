#ifndef GFCPLATEMANAGER_H
#define GFCPLATEMANAGER_H

//#include <glad/glad.h>
#include "gfcPlate.h"
#include <vector>
#include "gfcNetworkStructures.h"

#ifndef GFC_MAX_PLATES
#define GFC_MAX_PLATES 4
#endif

#include "gfcnetremotepointerinfo.h"
#include "gfcrenderparams.h"
#include "gfcpickdrawee.h"
#include "gfcpicknotifee.h"
#include "gfcplatemanagergui.h"

/**
	@author Daniel Gollas Gilman <dgollas@ollin.com.mx>
*/
class gfcPlateManager: public gfcPickDrawee, public gfcPickNotifee{
public:
    gfcPlateManager();

    ~gfcPlateManager();
    
    void initializeWidgets();
    void setFramingMode(int framingMode);

	void setActiveQuad(int quad);
    int getActiveQuad();

    int getFramingMode();
    
    int getTrackOnPlate(int whichOne);
    void setTrackOnPlate(int whichOne, int theTrack);
    
    void toggleTextMode(int whichOne);
    void toggleTextModeAll();
    
	void fitToViewport(int whichOne);
	void fitToViewportAll();

	void toggleFlip(int whichOne);
	void toggleFlop(int whichOne);

	void toggleFlipAll();
	void toggleFlopAll();


    void toggleHistogramMode(int whichOne);
    void toggleHistogramModeAll();
    
    void registerPlatesAsPickNotifees();
    
    void drawPlates(int w, int h, bool resized=false);
    virtual void drawForPicking();
    virtual int pickNotify(gfcPickNotifyParameters &params);
    void draw(int w=0, int h=0, bool resized=false); 
    void updateAnimations(); //this should update the remote pointers, the flips and flops and all stuff that animates.
    
    void toggleHelp();
    
    void setDrawLUTPreview(bool showPreview, bool showUniform, int lutValue);
    
    void savePlateSessionParameters(XMLNode &platesNode);
    void loadPlateSessionParameters(XMLNode &platesNode);

    std::vector<gfcFXStack> getPlateFXStacks();
    void setPlateFXStacks(std::vector<gfcFXStack> stacks);
	
	std::string getPlateFXStacksAsString();
	void setPlateFXStacksFromString(std::string s);

    std::vector<gfcFXStack> favoriteStacks;
    gfcFXStack getFavoriteStack(int whichOne);
    void addToFavoriteStacks(gfcFXStack theStack, int whichStack);
    std::vector<gfcFXStack> getAllFavoriteStacks();
    void saveFavoriteStacksToNode(XMLNode &node);
    void loadFavoriteStacksFromNode(XMLNode & node);

	void setFavoriteOnPlate(int whatFavorite, int whatPlate);
	void appendFavoriteOnPlate(int whatFavorite);
	void setFavoriteOnPlate(int whatFavorite);
	void saveFavoriteFromPlate(int whatFavorite, int whatPlate);
	void saveFavoriteFromPlate(int whatFavorite);


	std::vector<gfcNetPlateColorCorrectionInfo> favoritesColorCorrection;
	gfcNetPlateColorCorrectionInfo getFavoriteColorCorrection(int whichOne);
	void addToFavoritesColorCorrection(gfcNetPlateColorCorrectionInfo theCC, int whichOne);
	std::vector<gfcNetPlateColorCorrectionInfo> getAllFavoriteColorCorrections();
	void saveFavoriteColorCorrectionsToNode(XMLNode &node);
	void loadFavoriteColorCorrectionsFromNode(XMLNode & node);

	void setFavoriteColorCorrectionOnPlate(int whatFavorite, int whatPlate);
	void setFavoriteColorCorrectionOnPlate(int whatFavorite);
	void saveFavoriteColorCorrectionFromPlate(int whatFavorite, int whatPlate);
	void saveFavoriteColorCorrectionFromPlate(int whatFavorite);
    


    void setCursorCoords(int cx, int cy);
    Vec3D getCursorPositionIn2DSpace(int px, int py, int quadID);
    void storePointerInfo(gfcNetRemotePointerInfo info);
    
    void setRemotePointerOptions(int pfontSize, int psize, bool pfade, int pfadeDelay, bool ptrail, float ptrailLenght,int pColor);
    void setTextDisplayOptions(int pfontSize, float pcolor, float popacity);
    
    void processNetFXAttribInfo(gfcNetFXAttribInfo &info);
    void processNetFXCommonInfo(gfcNetFXCommonInfo &info);
    void processNetFXStackMessage(gfcNetFXStackMessage &message);
    void setForceSingleBufferedFXs(int value);
    
    int activeQuad;

   void saveStackToFile(int whichOne, std::string filename);
   void saveStacksToNode(XMLNode &node);
   


   void loadStackFromFile(int whichOne, std::string filename);
   
   void clearFXStack(int whichOne);
   
   void renderPlate(gfcRenderParams params,std::vector<std::string> *renderNames=NULL);

   // Source pixel size of plate `quad`'s current frame (0 if out of range).
   void getPlateSourceSize(int quad, int& w, int& h) {
       w = 0; h = 0;
       if (quad >= 0 && quad < (int)plates.size())
           plates[quad].getRenderSourceSize(w, h);
   }
	
   void setFeedbackMessage(std::string theMessage);

   // True while something needs the per-frame tick to keep animating even
   // when playback is stopped: the fading feedback message, or a settling
   // flip/flop rotation. The Qt idle tick (needsPlaybackTick) ORs this in so
   // the animation plays + repaints without a forced viewport refresh.
   bool hasActiveAnimations();

   /*Plate operations*/
	 void drawPlate(int whichOne);
	 
	 void panPlate(int whichOne, float panX, float panY);	
	 void panAllPlates(float panX, float panY);
	 
	 void zoomPlate(int whichOne, float zoom);
	 void zoomAllPlates(float zoom);
	 
	 void resetPlate(int whichOne);
	 void resetAllPlates();
	 
	 void resetColorCorrection(int whichOne);
	 void resetAllColorCorrections();

	 void updateAllFromGUI();

	 // Per-plate refresh without touching layout (framingMode) or active-quad
	 // selection — both of those live on gfcPlateManagerGUI and the Qt build
	 // drives them via different paths than the plate manager's GUI mirror,
	 // so updateAllFromGUI clobbers them when called outside the original
	 // FLTK-era startup flow. Use this from the Qt bridge (load window
	 // open/close) when you only want to propagate per-plate state changes.
	 void updatePlatesFromGUI();

	 void updateColorCorrectionsFromGUI();
	 void updateTransformationsFromGUI();

	 void updateAllGUILUTWidgets();
	 
	 void scrollLUT(int whichOne, int direction);

	 void setGamma(int whichOne, float value, int relative=0);
	 void setExposure(int whichOne, float value, int relative=0);
	 void setBrightness(int whichOne, float value, int relative=0);
	 void setContrast(int whichOne, float value, int relative=0);
	 void setSaturation(int whichOne, float value, int relative=0);

	 void setGammaAll( float value, int relative=0);
	 void setExposureAll( float value, int relative=0);
	 void setBrightnessAll( float value, int relative=0);
	 void setContrastAll( float value, int relative=0);
	 void setSaturationAll( float value, int relative=0);

	 void toggleChannelR(int whichOne);
	 void toggleChannelG(int whichOne);
	 void toggleChannelB(int whichOne);
	 void toggleChannelA(int whichOne);

	 void setChannelR(int whichOne, int value);
	 void setChannelG(int whichOne, int value);
	 void setChannelB(int whichOne, int value);
	 void setChannelA(int whichOne, int value);

	gfcFXStack* getFXStack(int whichOne);
	void setFXStack(gfcFXStack theStack, int whichOne);
	void appendFXStack(gfcFXStack theStack, int whichOne);

	// Flips a plate's GUI into preview mode so gfcPlate::draw3Drect
	// picks the previewFrame branch. Used by the Qt drop handler after
	// loading an image into the sequence; FLTK drives this from the
	// load-window callbacks. No-op if `whichOne` is out of range.
	void setPlateShowPreview(int whichOne, bool value);

	// Direct access to a plate's GUI surface. The Qt build needs this so
	// PlateCard_Qt can reach the same gfcPlateGUI_Qt instance the plate
	// renders from (created in initializeWidgets()). Returns nullptr for
	// out-of-range indices.
	gfcPlateGUI* getPlateGUI(int whichOne);

	// Hit-tests the plate under viewport pixel (vx, vy) given the
	// current framingMode and the viewport's logical size. vx is from
	// the left edge, vy is from the TOP edge (typical Qt mouse coords).
	// Returns 0..3, or -1 if the position is somehow out of any plate.
	// Used by the Qt mouse handlers so drag/zoom act on whichever plate
	// the cursor is over in multi-plate layouts.
	int getPlateAtPosition(int vx, int vy, int viewportW, int viewportH);
	
	
	
	void addFXToPlate(int plate, gfcFX theFX);
	// FX widget callback. The handle is opaque so this header doesn't drag
	// in FLTK widget definitions; it forwards through to gfcFXStack
	// (which now also takes an opaque handle).
	int handleFXGUICB(int whichOne, void* widgetHandle, void* data);
	
	std::vector<gfcNetTransformationInfo> getTransformations();
	void setTransformations(std::vector< gfcNetTransformationInfo > transformations);
	
	std::vector<gfcNetPlateColorCorrectionInfo> getColorCorrections();
	void setColorCorrections(std::vector<gfcNetPlateColorCorrectionInfo>);
	
	void setLUTAll(int lutIndex);
	void setLUTByName(int whichOne, std::string lutName);
	void setLUT(int whichOne, int lutIndex);

	gfcNetPlateColorCorrectionInfo getColorCorrection(int whichOne);
	void setColorCorrection(int whichOne, gfcNetPlateColorCorrectionInfo);

	std::vector<gfcNetPlateStateInfo> getPlateStateInfo();
	void setPlateStateInfo(std::vector<gfcNetPlateStateInfo> states);
	void abortRender();
	bool isRendering();
	bool getChanged();
	void setChanged();
	void clearHistogramCache(int whichOne);
	void clearAllHistogramCache();
	void setHistogramQuality(int whichOne, int quality);
	void setHistogramQuality(int quality);
	
private:
    
	
	bool changed;
    void drawChatGUI();
    std::vector<gfcPlate> plates;
    int framingMode;
    bool textModeReset;
    bool histogramModeReset;
    gfcPlateManagerGUI* myGUI;
    bool showHelp;
    void drawHelp(int w=0, int h=0);
	std::string helpMessage;
    
	std::string feedbackMessage;
	float feedbackMessageOpacity;	

    bool stopRendering;
    
    int prevH; //these are the default values to use when draw receives no params, they contain the previous ones, 
    int prevW; //for now, they are only used when no params are sent to draw, which only happens in the Render function.

    ///lut Preview values****
    bool showLutPreview;
    bool showLutUniform;
    int showLutChoice;
    float showLutscale;
    float showLutTX;
    float showLutTY;
    ///**********************
    
};

#endif
