#include "gfcfxstack.h"
//#include "network.h"

// fxcontrolwindow.h transitively pulls all the FLTK widget headers used by
// handleGUICB below. Only needed in the FLTK build; the rest of this file
// (storage, networking, save/load) is FLTK-free.
#include "trilerp.h"

#include "gfclutmanager.h"
extern gfcLUTManager lutManager;

#include "gfcnetworkmanager.h"
extern gfcNetworkManager networkManager;

#include "gfcfxmanager.h"
extern gfcFXManager fxManager;

//extern std::vector<CubeLUT> lutArray; //contains loaded LUTs

gfcFXStack::gfcFXStack() {
    numOfActiveFX=0;
}


gfcFXStack::~gfcFXStack() {
}

void gfcFXStack::addFX(gfcFX theFX) {
    fxs.push_back(theFX);
    if (theFX.active)
        numOfActiveFX++;
}

int gfcFXStack::getNumOfFXs() {
    return fxs.size();
}

gfcFX gfcFXStack::getFX(int index) {
    if (index<fxs.size()) {
        return fxs[index];
    } else {
        gfcFX empty;
	printf("returning empty FX from gfcFXStack::getFX with index %i\n",index);
        return empty;
    }
}

void gfcFXStack::addFXGUIInfo(fxParamInfo theInfo, void* widgetHandle) {
    guiToFX[widgetHandle]=theInfo;
}

void gfcFXStack::setWidgetValue(int fxIndex,
                                const std::string& groupName,
                                const std::string& widgetName,
                                float value) {
    if (fxIndex < 0 || fxIndex >= (int)fxs.size()) return;
    auto& fx = fxs[fxIndex];
    auto gIt = fx.groups.find(groupName);
    if (gIt == fx.groups.end()) return;
    auto wIt = gIt->second.widgets.find(widgetName);
    if (wIt == gIt->second.widgets.end()) return;
    wIt->second.value = value;
}

/**
 *
 * @param o
 * @param data
 * @return Returns 1 if the fxControl window needs updating, otherwise 0
 */
int gfcFXStack::handleGUICB(void * widgetHandle, void * data) {
    // Non-FLTK builds: FX widget callbacks are not yet wired up.
    (void)widgetHandle; (void)data;
    return 0;
}

int gfcFXStack::getNumOfActiveFXs() {
    std::vector<gfcFX>::iterator iter=fxs.begin(), end=fxs.end();
    int count=0;
    for (iter;iter!=end;iter++) {
        if (iter->active)
            count++;
    }
    return count;
}

void gfcFXStack::setActive(int fxIndex, bool active) {
    if (fxIndex < 0 || fxIndex >= (int)fxs.size()) return;
    if (fxs[fxIndex].active == active) return;
    fxs[fxIndex].active = active;
    if (active)
        numOfActiveFX++;
    else
        numOfActiveFX--;
}

void gfcFXStack::moveFX(int from, int to) {
    const int n = (int)fxs.size();
    if (from < 0 || from >= n) return;
    if (to < 0 || to >= n) return;
    if (from == to) return;
    gfcFX moved = fxs[from];
    fxs.erase(fxs.begin() + from);
    fxs.insert(fxs.begin() + to, moved);
}

std::vector< int > gfcFXStack::getActiveFXIndexes() {
    std::vector<int> result;
    return result;
}

void gfcFXStack::processNetFXAttribInfo(gfcNetFXAttribInfo &info) {
    //printf("Processing attrib info\n");

    switch ( info.attribType ) {
    case FX_GUI_FLOAT: {
        fxs[info.id.index].groups[info.groupName].widgets[info.variableName].value=info.theFloat;

    }
    break;

    case FX_GUI_BOOL:
    case FX_GUI_CHOICE:
    case FX_GUI_TEXTURE: {
        fxs[info.id.index].groups[info.groupName].widgets[info.variableName].value=info.theInt;
    }
    break;

    case FX_GUI_LUT:
    case FX_GUI_CUBE: {
        int lutArrayIndex=0;

        fxs[info.id.index].groups[info.groupName].widgets[info.variableName].value=lutManager.getLutIndexByName(info.lutOrCube);
    }
    break;
    }
}

void gfcFXStack::processNetFXCommonInfo(gfcNetFXCommonInfo &info) {
    //printf("Processing common info\n");
    //delete?
    if ( info.remove ) {

        if ( info.id.index >= fxs.size() ) //dont delete if we don't have it
            return;

        if ( fxs[ info.id.index].active )
            numOfActiveFX--;
        fxs.erase ( fxs.begin() + info.id.index );

        return;
    }

    //reset?
    if ( info.reset ) {
        fxs[ info.id.index].reset();
        return;
    }

    //on/Off?
    if ( info.onOff==1 ) {
        fxs[ info.id.index ].active=false;
        numOfActiveFX--;
        return;
    }
    if ( info.onOff==2 ) {
        fxs[ info.id.index ].active=true;
        numOfActiveFX++;
        return;
    }

    //move upDown?
    if ( info.upDown==1 ) {
        if ( info.id.index>0 ) {
            gfcFX tmpFX=fxs[ info.id.index-1];
            fxs[ info.id.index-1]=fxs[ info.id.index];
            fxs[ info.id.index]=tmpFX;
        }
        return;
    }

    if ( info.upDown==2 ) { //MOVE DOWN
        if ( info.id.index<fxs.size()-1 ) {
            gfcFX tmpFX=fxs[ info.id.index+1];
            fxs[ info.id.index+1]=fxs[ info.id.index];
            fxs[ info.id.index]=tmpFX;
        }
        return;
    }
}




/**
 * This write to a stack to an XMLNode, it adds a child FXS node, and then all the fxs and params under it.
 It would usually be used by passing a node with info about the plate and a parent with the correct xml header and stuff
 this way we can use the same routine to save a single stack to file, or to save several stacks in the same file as children of the 
 same "stacks" node.
 XML
   |-root //this could be a session xml or just a stack xml.
       |-stacks
            |-stackQ0
                \-FXS //these are the ones this routine writes and receives stack0 as param.
                   \-FX1 
                   \-FX2
                   \-FXn
 * @param node 
 */
void gfcFXStack::saveStackToNode(XMLNode &pnode) const {
    
   XMLNode xRootNode=pnode.addChild("FXS");
    
   
    //node->addAttribute("version","1.0");
    //For each FX in the stack, create an FX node
    int fxCount=0;
    int fxAmount=fxs.size();
    for (int fxCount=0;fxCount<fxAmount;fxCount++) {
        gfcFX tmpFXRef=fxs[fxCount];
		gfcFX *tmpFX=&tmpFXRef;
        //printf("Saving FX: %s\n",tmpFX->name.c_str());

        XMLNode fxNode=xRootNode.addChild("FX");
        //add the FXs attributes to the FX node
        fxNode.addAttribute("name",tmpFX->name.c_str());
        fxNode.addAttribute("menuName",tmpFX->menuName.c_str());
        fxNode.addAttribute("hash",tmpFX->md5Hash.c_str());
        if (tmpFX->active)
            fxNode.addAttribute("active","1.0");
        else
            fxNode.addAttribute("active","0.0");

        //iterate the groups
        std::map<std::string,gfcFXWidgetGroup>::iterator groupsIter=tmpFX->groups.begin();
        std::map<std::string,gfcFXWidgetGroup>::iterator groupsIterEnd=tmpFX->groups.end();

        for (groupsIter;groupsIter!=groupsIterEnd;groupsIter++) {
            //for each attribute save the name, type and value if any.

            gfcFXWidgetGroup *tmpGroup=&groupsIter->second;
            std::vector<std::string>::iterator widgetIter=tmpGroup->widgetsOrder.begin();
            std::vector<std::string>::iterator widgetIterEnd=tmpGroup->widgetsOrder.end();

            for (widgetIter;widgetIter!=widgetIterEnd;widgetIter++) {
                gfcFXWidget *tmpWidget= &tmpGroup->widgets[*widgetIter];


                switch (tmpWidget->type) {
                case FX_GUI_FLOAT: {
                    XMLNode widgetNode=fxNode.addChild("widget");
                    widgetNode.addAttribute("varName",tmpWidget->varName.c_str());
                    widgetNode.addAttribute("type","float");
                    char tmpValue[30];
                    sprintf(tmpValue,"%f",tmpWidget->value);
                    widgetNode.addAttribute("value",tmpValue);
                    widgetNode.addAttribute("group",tmpGroup->name.c_str());
                }
                break;

                case FX_GUI_CHOICE: {
                    XMLNode widgetNode=fxNode.addChild("widget");
                    widgetNode.addAttribute("varName",tmpWidget->varName.c_str());
                    widgetNode.addAttribute("type","choice");
                    char tmpValue[30];
                    sprintf(tmpValue,"%f",tmpWidget->value);
                    widgetNode.addAttribute("value",tmpValue);
                    widgetNode.addAttribute("group",tmpGroup->name.c_str());
                }
                break;

                case FX_GUI_BOOL: {
                    XMLNode widgetNode=fxNode.addChild("widget");
                    widgetNode.addAttribute("varName",tmpWidget->varName.c_str());
                    widgetNode.addAttribute("type","bool");
                    char tmpValue[30];
                    sprintf(tmpValue,"%f",tmpWidget->value);
                    widgetNode.addAttribute("value",tmpValue);
                    widgetNode.addAttribute("group",tmpGroup->name.c_str());
                }
                break;

                case FX_GUI_TEXTURE: {
                    XMLNode widgetNode=fxNode.addChild("widget");
                    widgetNode.addAttribute("varName",tmpWidget->varName.c_str());
                    widgetNode.addAttribute("type","texture");
                    char tmpValue[30];
                    sprintf(tmpValue,"%f",tmpWidget->value);
                    widgetNode.addAttribute("value",tmpValue);
                    widgetNode.addAttribute("group",tmpGroup->name.c_str());
                }
                break;

                //cubes and luts are a special case, we don't store the value since it's only the index, and the user might not have loaded the
                //luts or cubes in the same order as the person who created the stack file. So we store the name of the lut.
                case FX_GUI_CUBE: {
                    XMLNode widgetNode=fxNode.addChild("widget");
                    widgetNode.addAttribute("varName",tmpWidget->varName.c_str());
                    widgetNode.addAttribute("type","cube");
                    widgetNode.addAttribute("value",lutManager.getLUT(tmpWidget->value).getNameNoPath().c_str());
                    widgetNode.addAttribute("group",tmpGroup->name.c_str());
                }
                break;

                case FX_GUI_LUT: {
                    XMLNode widgetNode=fxNode.addChild("widget");
                    widgetNode.addAttribute("varName",tmpWidget->varName.c_str());
                    widgetNode.addAttribute("type","lut");
                    widgetNode.addAttribute("value",lutManager.getLUT(tmpWidget->value).getNameNoPath().c_str());
                    widgetNode.addAttribute("group",tmpGroup->name.c_str());
                }
                break;

                default:
                    //widgetNode.addAttribute("type","other");
                    break;
                }



            }
        }
    }

}

/**
 * Creates an xml file with an fxs extension that contains the currently applied FX stack and all the fx's parameters. The FX's are stored by name, so they can be accesed later in a machine that loaded the FX's in a different order.
 * @param fileName the filename where the stack will be saved.
 */
void gfcFXStack::saveStackToFile(std::string filename) const
{
    if(filename.empty()){
    	printf("gfcFXStack::saveStackToFile: Error, filename is empty\n");
     return;
     }
    
    filename=AppendExtensionToFilename(filename,".fxs");
    
    XMLNode xMainNode=XMLNode::createXMLTopNode ( "xml",TRUE );
    xMainNode.addAttribute ( "version","1.0" );
    XMLNode xRootNode=xMainNode.addChild ( "root" );
    xRootNode.addAttribute ( "comment", "This is a JefeCheck XML formated FX Stack File");
    	
    XMLNode stackNode=xRootNode.addChild("stack");
    saveStackToNode(stackNode);
    
    XMLError writeError=xMainNode.writeToFile ( filename.c_str() );
    if ( writeError!=eXMLErrorNone ) {
        printf ( "Error writing FX Stack File file!: %s\n",XMLNode::getError(writeError) );
    } else {
        printf ( "FX Stack saved to %s\n",filename.c_str() );
    }
}

/**
 * Receives a node with the following structure and extracts the stack and FXs and stores them.
  XML
   |-root //this could be a session xml or just a stack xml.
       |-stacks
            |-stackQ0
                \-FXS //these are the ones this routine reads and receives stack0 as param.
                   \-FX1 
                   \-FX2
                   \-FXn
 * @param pnode The stack0 node from the diagram.
 */
std::vector<std::string> gfcFXStack::loadStackFromNode(XMLNode & pnode, int *FXSi)
{

     int notLoadedFXcounter=0;
     int notLoadedLUTcounter=0;
     std::string notLoadedFX="The Following FXs where not added to the stack because the corresponding plugin is not loaded. The Rest of the stack will be loaded normally\nLoad the FX plugins and try again:\n";
     std::string notLoadedLUT="The Following LUTs where used in an FX, but where not found. The Rest of the stack will be loaded normally.\n Load the LUTs and try again:\n";


    //obtain FXS node from the stack node received as param
    XMLNode xNode=pnode.getChildNode("FXS",FXSi);
    
    //iterate through all the FX nodes.
    int numOfFX=xNode.nChildNode("FX");
    int xmlFXIter=0;
    //printf("No. of FX: %i\n",numOfFX);

    for(int i=0; i<numOfFX;i++)
    {
        gfcFXWidgetGroup group;
        XMLNode fxNode=xNode.getChildNode("FX",&xmlFXIter);

        //printf("%i. FX Node: %s, %s\n",i,fxNode.getAttribute("name"),fxNode.getAttribute("menuName"));

        
        //get the FX from the FX manager (if it's there...)
        gfcFX tmpFX=fxManager.getFXbyHash(fxNode.getAttribute("hash"));
        //if it is not found (indicated by a NOT loaded and compiled status) skip this node and add it to the not loaded string.
        if(!tmpFX.loadedAndCompiled)
        { //the fx is not loaded.
        	//printf("FX %s not loaded, adding to not loaded string\n",fxNode.getAttribute("name"));
            notLoadedFX+="\n ";
            notLoadedFX+=fxNode.getAttribute("name");
            notLoadedFXcounter++;
        }
        else
        { //if we found it, then push the FX into the applied FX array (appliedFX) for this quadrant AFTER filling it with attributes.
            
            //after that, we must fill the values of each attribute using the values in the file.
            int numOfWidgets=fxNode.nChildNode("widget");
            int xmlWidgetIter=0;
            //printf(" No. of Widgets: %i\n",numOfWidgets);
            for(int j=0;j<numOfWidgets;j++)
            {
                XMLNode widgetNode = fxNode.getChildNode("widget",&xmlWidgetIter);

                //printf("%i  Widget Node: %s, %s (%s)\n",j,widgetNode.getAttribute("varName"),widgetNode.getAttribute("value"),widgetNode.getAttribute("type"));

                //Cubes and luts are handled differently.
                if(strcmp(widgetNode.getAttribute("type"),"lut")==0)
                { //cubes and luts need to searched for in the lutArray , keeping track of index for each particular type (cube or lut) and that will be the value asigned to the widget
                    
                    //1. Find if the lut is loaded, the index will be stored in the value of the widget
                    int lutIndex=lutManager.getLutIndexByName(widgetNode.getAttribute("value"));
                    if(lutIndex==-1)
                    { //the saved lut is not loaded
                    	notLoadedLUTcounter++;
                        notLoadedLUT+="\n ";
                        notLoadedLUT+=widgetNode.getAttribute("value");
                    }
                    else
                    {
                    	//1.5 store the value
                    	tmpFX.groups[widgetNode.getAttribute("group")].widgets[widgetNode.getAttribute("varName")].value=lutIndex;
                    	
                    	//2. Find it in the 1D lut array, that will be the GUI value (we have two different indexes since all luts are stored in the same array but only 1D are shown in LUTs and only 3D are shown in Cube attribs in FXs)
                    	int lut1Dindex=lutManager.get1DLutIndexByName(widgetNode.getAttribute("value"));
                    	if(lut1Dindex==-1)
                    	{ //we didn't find it, this should never happen since we already found it in the lutManager, but just in case the lutManager fucks up.
                    		notLoadedLUTcounter++;
                        	notLoadedLUT+="\n ";
                        	notLoadedLUT+=widgetNode.getAttribute("value");
                    	}
                    	else
                    	{	//save the gui value
                    		tmpFX.groups[widgetNode.getAttribute("group")].widgets[widgetNode.getAttribute("varName")].lutValue=lut1Dindex;
                    	}
                    }
                }
                else if(strcmp(widgetNode.getAttribute("type"),"cube")==0)
                {
                
                     //1. Find if the lut is loaded, the index will be stored in the value of the widget
                    int lutIndex=lutManager.getLutIndexByName(widgetNode.getAttribute("value"));
                    if(lutIndex==-1)
                    { //the saved lut is not loaded
                    	notLoadedLUTcounter++;
                        notLoadedLUT+="\n ";
                        notLoadedLUT+=widgetNode.getAttribute("value");
                    }
                    else
                    {
                    	//1.5 store the value
                    	tmpFX.groups[widgetNode.getAttribute("group")].widgets[widgetNode.getAttribute("varName")].value=lutIndex;
                    	
                    	//2. Find it in the 3D lut array, that will be the GUI value (we have two different indexes since all luts are stored in the same array but only 1D are shown in LUTs and only 3D are shown in Cube attribs in FXs)
                    	int lut3Dindex=lutManager.get3DLutIndexByName(widgetNode.getAttribute("value"));
                    	if(lut3Dindex==-1)
                    	{ //we didn't find it, this should never happen since we already found it in the lutManager, but just in case the lutManager fucks up.
                    		notLoadedLUTcounter++;
                        	notLoadedLUT+="\n ";
                        	notLoadedLUT+=widgetNode.getAttribute("value");
                    	}
                    	else
                    	{	//save the gui value
                    		tmpFX.groups[widgetNode.getAttribute("group")].widgets[widgetNode.getAttribute("varName")].lutValue=lut3Dindex;
                    	}
                    }
                
                }
                else //Manage all other cases that are straigt forward converting the strings to floats
                {
                    //printf("  Setting %s:%s at %f \n",widgetNode.getAttribute("group"),widgetNode.getAttribute("varName"),atof(widgetNode.getAttribute("value")));
                    tmpFX.groups[widgetNode.getAttribute("group")].widgets[widgetNode.getAttribute("varName")].value=atof(widgetNode.getAttribute("value"));
                }
            }

            if(fxNode.getAttribute("active")!=NULL)
                tmpFX.active=atof(fxNode.getAttribute("active"));

            this->numOfActiveFX+=tmpFX.active;
            fxs.push_back(tmpFX);

        }//closes the else on fxArrayIndex==-1


    }
    
    std::vector<std::string> errors;
    
    if(notLoadedFXcounter>0)
    {
	errors.push_back(notLoadedFX);
    }

    if(notLoadedLUTcounter>0)
    {
        errors.push_back(notLoadedLUT);
    }
    
    return errors;


    
}



/**
 *  Loads an XML formated FXS file, applies the effects contained in it and sets the values stored in the file to each fx.
    If an FX plugin contained in the file is not loaded, or if a LUT or CUBE is not loaded, it is simply skipped and a warning message array is returned.
 * @param fileName the stack file to load
 */
std::vector<std::string> gfcFXStack::loadStackFromFile(std::string filename)
{
    std::vector<std::string> result;	

    if(filename.empty())
    {
        printf("gfcFXStack::loadStackFromFile: Error, filename is empty\n");
        return result;
    }

     int notLoadedFXcounter=0;
     int notLoadedLUTcounter=0;
    
    XMLNode xMainNode=XMLNode::openFileHelper(filename.c_str());

    if(!xMainNode.loaded)
    {
        printf("gfcFXStack::loadStackFromFile: Error opening stack file\n");
        return result;
    }
    XMLNode stackNode = xMainNode.getChildNode("root").getChildNode("stack");
    //store the FXs from the rootNode
    return loadStackFromNode(stackNode);
}

std::vector<std::string> gfcFXStack::loadStackFromString(std::string s){
	XMLNode node = XMLNode::parseString(s.c_str());
	return loadStackFromNode(node);
	
}

void gfcFXStack::clearStack()
{
	fxs.clear();
	numOfActiveFX=0;
	guiToFX.clear();
}

std::vector< gfcFX > gfcFXStack::getAllFXs()
{
	return fxs;
}

void gfcFXStack::appendFXStack(gfcFXStack otherStack)
{
	std::vector< gfcFX > tmp=otherStack.getAllFXs();
	fxs.insert(fxs.end(),tmp.begin(),tmp.end());

}

std::string gfcFXStack::getDescriptionString()
{
	return "La pinga";
}