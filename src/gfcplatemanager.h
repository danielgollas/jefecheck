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
	
   void setFeedbackMessage(std::string theMessage);
   
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
