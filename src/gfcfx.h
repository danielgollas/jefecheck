#ifndef GFCFX_H
#define GFCFX_H


/**
	@author Daniel Gollas Gilman <dgollas@ollin.com.mx>
*/

enum GFC_FX_GUI_TYPE{FX_GUI_UNKNOWN=0, FX_GUI_FLOAT,FX_GUI_TEXTURE,FX_GUI_CUBE,FX_GUI_LUT,FX_GUI_BOOL, FX_GUI_CHOICE,FX_GUI_SPACER,FX_GUI_NEWLINE, FX_ACTIVATE,FX_ADDTOACTIVE,FX_DELETE,FX_RESET,FX_MOVEUP, FX_MOVEDOWN};

#include "glew.h"
#include <map>
#include <vector>
#include <string>
#include <FL/Fl_Progress.H>
#include "platefxparams.h"
#include "fxcontrolwindow.h"
#include "vec3d.h"
//#include "gfcStructures.h"
#include "UIConstants.h"

//a compositing plugin uses more that the original texture, and due to current limitations has to be applied before all other plugins.
//



enum FXLWWIDGETTYPES{FXLWWIDGET_DELETE=0,FXLWWIDGET_AUTOLOAD};



class fxLoaderParamInfo{
	public:
	fxLoaderParamInfo(){};
	fxLoaderParamInfo(int pfxIndex,const char* pFxName, FXLWWIDGETTYPES pType){
		fxIndex=pfxIndex;
		fxName=pFxName;
		type=pType; //will control what to do with the LUT, delete it, autoload it.
	};
	int fxIndex;
	std::string fxName;
	FXLWWIDGETTYPES type;
};

class gfcFXWidget{

public:

gfcFXWidget(){
	//value=0.0;
	lutValue=0.0;
	maximum=999.0;
	minimum=-999.0;
	step=0.001;
	defaultValue=0.0;
	labelColor.x=255.0;
	labelColor.y=255.0;
	labelColor.z=255.0;
	width=10;
	/*label="";
	tooltip=" ";
	*/
}

GFC_FX_GUI_TYPE type;

int width;
float value;
int lutValue; //this value is only used for the lut and cube widgets, since the widgets value is not the same value as the actual value stored.
float maximum;
float minimum;
float step;
float defaultValue;
std::string label;
Vec3D labelColor;
std::string varName;
std::string tooltip;

std::vector<std::string> options; //when the widgets is a choice, it has a vector of options.

};

class gfcFXWidgetGroup{
public:
std::string name;

//the size this group needs in the fx controls window gui, calculated when loading the fx
int sizeX;
int sizeY;

//contains the widgets
std::map<std::string, gfcFXWidget> widgets;
//contains the order in which the widgets where added to the widget map, needed when drawing the GUI since the order of the widgets is very imporant. 
std::vector<std::string> widgetsOrder;
};

class gfcFX{
public:
    gfcFX();
	//gfcFX(const gfcFX &theFX);

    //~gfcFX();
	
    int load(const std::string filename, int type, Fl_Progress *pprogress=NULL);
    PlateFXParams bind(int previousTexID, FXTexCoords fboTexCoords, bool forcedLoading=0);
    void unbind();
    void reset();
    void freeResources();


    std::string filename;
    std::string name;
    std::string menuName;
    std::string author;
    std::string version;
    std::string description;
    std::string vertex;
    std::string fragment;
    //char *fragmentSource;
    //char *vertexSource;
    std::string md5Hash;
    bool compositing; 
    int textureNum; 
    bool autoload;
    bool active;
    bool guiOpen;

    //contains all the groups
    std::map<std::string, gfcFXWidgetGroup> groups;
   
   //the size this fx needs in the fx controls window gui, calculated when loading the fx
   int sizeX;
   int sizeY;
   
    int numOfTextures;
    
    std::map<std::string, int> textures; 
	
    int cubesNum;
    std::map<std::string, int> cubes;

    int floatsNum;
    std::map<std::string, float> floats;

    int boolsNum;
    std::map<std::string, bool> bools;

    //overload the assignment operator
    //gfcFX &operator=(const gfcFX &fx);
    std::string getNameNoPath();
    public:


    std::string compilationError;
	
    bool loadedAndCompiled;
	bool errorWhileLoading; //this variable is used so that we aaaalways load the FX, even if it does not work. This allows the remote sessions to work smoothly
	//of course, we need to display a message that says that this plug in did not work here. And that it will draw a black image.
    //the vertex Shader (handle)
    GLhandleARB vertexShader;
    //the fragment Shader (handle)
    GLhandleARB fragmentShader;
    //the Shader Program (handle)
    GLhandleARB ShaderProgram;
    //the texture uniforms (map)
    //the cube uniforms (map)
    //the float uniforms (map)
    //the bool uniforms (map)
};



#endif
