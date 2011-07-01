#include "gfcfxstack.h"
//#include "network.h"

#include "fxcontrolwindow.h"
#include "trilerp.h"

#include "gfclutmanager.h"
extern gfcLUTManager lutManager;

#include "gfcnetworkmanager.h"
extern gfcNetworkManager networkManager;

#include "gfcfxmanager.h"
extern gfcFXManager fxManager;

//extern std::vector<CubeLUT> lutArray; //contains loaded LUTs

gfcFXStack::gfcFXStack() {
}


gfcFXStack::~gfcFXStack() {
}

void gfcFXStack::addFX(gfcFX theFX) {
    fxs.push_back(theFX);
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

void gfcFXStack::addFXGUIInfo(fxParamInfo theInfo, Fl_Widget* o) {
    guiToFX[o]=theInfo;
}

/**
 *
 * @param o
 * @param data
 * @return Returns 1 if the fxControl window needs updating, otherwise 0
 */
int gfcFXStack::handleGUICB(Fl_Widget * o, void * data) {

    fxParamInfo info=guiToFX[o];



    switch ( info.type) {

    case FX_ACTIVATE:
        if (!((Fl_Check_Button*)o)->value()) {
//printf("Deactivating FX%i on Quadrant %i\n",info.fxIndex,info.quadrant);
            fxs[info.fxIndex].active=false;
            numOfActiveFX--;
            gfcNetFXCommonInfo message;

            message.id.quadID=info.quadrant;
            message.id.index=info.fxIndex;
            message.onOff=1;
            message.upDown=0;
            message.reset=false;
            message.remove=false;
            networkManager.sendFXCommonMessage(message);
        } else {
            //printf("Activating FX%i on Quadrant %i\n",info.fxIndex,info.quadrant);
            fxs[info.fxIndex].active=true;
            numOfActiveFX++;
            gfcNetFXCommonInfo message;

            message.id.quadID=info.quadrant;
            message.id.index=info.fxIndex;
            message.onOff=2;
            message.upDown=0;
            message.reset=false;
            message.remove=false;
            networkManager.sendFXCommonMessage(message);

        }
        //printf("Number of Active effects(Q%i): %i\n",info.quadrant,numberOfActiveEffects[info.quadrant]);
        break;

    case FX_RESET: {
        fxs[info.fxIndex].reset();

        //Send the message from here
        gfcNetFXCommonInfo message;

        message.id.quadID=info.quadrant;
        message.id.index=info.fxIndex;
        message.onOff=0;
        message.upDown=0;
        message.reset=true;
        message.remove=false;
        networkManager.sendFXCommonMessage(message);

        return 1;
    }
    break;

    case FX_DELETE: {
        //printf("Deleting FX%i\n",info.fxIndex);




        fxs.erase(fxs.begin()+info.fxIndex);
        numOfActiveFX--;



        //Send the message from here
        gfcNetFXCommonInfo message;

        message.id.quadID=info.quadrant;
        message.id.index=info.fxIndex;
        message.onOff=0;
        message.upDown=0;
        message.reset=0;
        message.remove=true;
        networkManager.sendFXCommonMessage(message);

        return 1;
    }
    break;


    case FX_MOVEUP: {
        //printf("MOVING UP FX%i\n",info.fxIndex);
        //inefficent swap but who cares, it's a GUI operation

        if (info.fxIndex>0) {
            gfcFX tmpFX=fxs[info.fxIndex-1];
            fxs[info.fxIndex-1]=fxs[info.fxIndex];
            fxs[info.fxIndex]=tmpFX;



            gfcNetFXCommonInfo message;

            message.id.quadID=info.quadrant;
            message.id.index=info.fxIndex;
            message.onOff=0;
            message.upDown=1; //move up
            message.reset=false;
            message.remove=false;
            networkManager.sendFXCommonMessage(message);

            return 1;
        }

    }
    break;

    case FX_MOVEDOWN: {
        //printf("MOVING DOWN FX%i\n",info.fxIndex);
        if (info.fxIndex<fxs.size()-1) {
            gfcFX tmpFX=fxs[info.fxIndex+1];
            fxs[info.fxIndex+1]=fxs[info.fxIndex];
            fxs[info.fxIndex]=tmpFX;


            gfcNetFXCommonInfo message;

            message.id.quadID=info.quadrant;
            message.id.index=info.fxIndex;
            message.onOff=0;
            message.upDown=2; //move down
            message.reset=false;
            message.remove=false;
            networkManager.sendFXCommonMessage(message);

            return 1;
        }
    }
    break;

    case FX_GUI_FLOAT: { //set the corresponding float in the fx (only internal storage for the fx, the actual Shader uses that internal storage later)

        //fxs[info.fxIndex].floats[info.variableName]=((Fl_Value_Input*)o)->value();
        // printf("Set Float Value %s(Q%i,FX%i):%f\n",info.variableName.c_str(),info.quadrant,info.fxIndex,fxs[info.fxIndex].floats[info.variableName]);
        fxs[info.fxIndex].groups[info.groupName].widgets[info.variableName].value=((Fl_Value_Input*)o)->value();

        gfcNetFXAttribInfo message;

        message.id.quadID=info.quadrant;
        message.id.index=info.fxIndex;
        message.attribType=FX_GUI_FLOAT;
        message.theFloat=((Fl_Value_Input*)o)->value();
        message.variableName=(info.variableName.c_str());
        message.groupName=(info.groupName.c_str());
        networkManager.sendFXAttribMessage(message);
    }
    break;
    case FX_GUI_CHOICE: { //set the corresponding float in the fx (only internal storage for the fx, the actual Shader uses that internal storage later)

        //fxs[info.fxIndex].floats[info.variableName]=((Fl_Value_Input*)o)->value();
        // printf("Set Float Value %s(Q%i,FX%i):%f\n",info.variableName.c_str(),info.quadrant,info.fxIndex,fxs[info.fxIndex].floats[info.variableName]);
        fxs[info.fxIndex].groups[info.groupName].widgets[info.variableName].value=((Fl_Choice*)o)->value();

        gfcNetFXAttribInfo message;

        message.id.quadID=info.quadrant;
        message.id.index=info.fxIndex;
        message.attribType=FX_GUI_CHOICE;
        message.theInt=((Fl_Choice*)o)->value();
        message.variableName=(info.variableName.c_str());
        message.groupName=(info.groupName.c_str());
        networkManager.sendFXAttribMessage(message);
    }

    break;
    case FX_GUI_TEXTURE: { //set the corresponding texture in the fx (only internal storage for the fx, the actual Shader uses that internal storage later)
        fxs[info.fxIndex].groups[info.groupName].widgets[info.variableName].value=((Fl_Choice*)o)->value();

        gfcNetFXAttribInfo message;

        message.id.quadID=info.quadrant;
        message.id.index=info.fxIndex;
        message.attribType=FX_GUI_TEXTURE;
        message.theInt=((Fl_Choice*)o)->value();
        message.variableName=(info.variableName.c_str());
        message.groupName=(info.groupName.c_str());
        networkManager.sendFXAttribMessage(message);

        // printf("%s changed to %f\n",fxs[info.fxIndex].groups[info.groupName].widgets[info.variableName].varName,fxs[info.fxIndex].groups[info.groupName].widgets[info.variableName].value);

    }
    break;
    case FX_GUI_CUBE: { //set the corresponding texture in the fx (only internal storage for the fx, the actual Shader uses that internal storage later)

        //find what cube this item in the menu corresponds to in the lutArray
        int lutArrayIndex=0;
        std::vector<std::string>lutNames=lutManager.getAllNames();

        for (int i=0;i<lutNames.size();i++) {
            if (strcmp(((Fl_Choice*)o)->text(),lutNames[i].c_str())==0) {
                //printf("Found the Correct LUT index at %i\n (%s)\n",i,lutArray[i].filename);
                lutArrayIndex=i;
                break;

            }
        }

        fxs[info.fxIndex].groups[info.groupName].widgets[info.variableName].value=lutArrayIndex;

        gfcNetFXAttribInfo message;

        message.id.quadID=info.quadrant;
        message.id.index=info.fxIndex;
        message.attribType=FX_GUI_CUBE;
        message.lutOrCube=(((Fl_Choice*)o)->text());
        message.variableName=(info.variableName.c_str());
        message.groupName=(info.groupName.c_str());
        networkManager.sendFXAttribMessage(message);

        //fxs[info.fxIndex].cubes[info.variableName]=lutArrayIndex;
        //printf("Set Cube Value %s(Q%i,FX%i):%i\n",info.variableName.c_str(),info.quadrant,info.fxIndex,fxs[info.fxIndex].cubes[info.variableName]);

    }

    break;

    case FX_GUI_LUT: { //set the corresponding texture in the fx (only internal storage for the fx, the actual Shader uses that internal storage later)
		if (((Fl_Choice*)o)->text()!=NULL)
		{

        //find what cube this item in the menu corresponds to in the lutArray
        int lutArrayIndex=0;
        std::vector<std::string>lutNames=lutManager.getAllNames();
        for (int i=0;i<lutNames.size();i++) {
			
			
			std::string theName=((Fl_Choice*)o)->text();
			//printf("((Fl_Choice*)o)->text()= %s i=%i , lutNames.size = %i\n",theName.c_str(), i,lutNames.size());
            if (strcmp(theName.c_str(),lutNames[i].c_str())==0) {
                //printf("Found the Correct LUT index at %i\n (%s)\n",i,lutArray[i].filename);
                lutArrayIndex=i;
                break;

            }
			
        }

        fxs[info.fxIndex].groups[info.groupName].widgets[info.variableName].value=lutArrayIndex;
        fxs[info.fxIndex].groups[info.groupName].widgets[info.variableName].lutValue=((Fl_Choice*)o)->value();

        gfcNetFXAttribInfo message;

        message.id.quadID=info.quadrant;
        message.id.index=info.fxIndex;
        message.attribType=FX_GUI_LUT;
        message.lutOrCube=(((Fl_Choice*)o)->text());
        message.variableName=(info.variableName.c_str());
        message.groupName=(info.groupName.c_str());
        networkManager.sendFXAttribMessage(message);
        //fxs[info.fxIndex].cubes[info.variableName]=lutArrayIndex;
        //printf("Set Cube Value %s(Q%i,FX%i):%i\n",info.variableName.c_str(),info.quadrant,info.fxIndex,fxs[info.fxIndex].cubes[info.variableName]);
		}
    }

    break;

    case FX_GUI_BOOL: { //set the corresponding texture in the fx (only internal storage for the fx, the actual Shader uses that internal storage later)
        if (((Fl_Check_Button*)o)->value())
            fxs[info.fxIndex].groups[info.groupName].widgets[info.variableName].value=true;
        else
            fxs[info.fxIndex].groups[info.groupName].widgets[info.variableName].value=false;

        gfcNetFXAttribInfo message;

        message.id.quadID=info.quadrant;
        message.id.index=info.fxIndex;
        message.attribType=FX_GUI_BOOL;
        message.theInt=((Fl_Button*)o)->value();
        message.variableName=(info.variableName.c_str());
        message.groupName=(info.groupName.c_str());
        networkManager.sendFXAttribMessage(message);

    }
    break;
    }

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