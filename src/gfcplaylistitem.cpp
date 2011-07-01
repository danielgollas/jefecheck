#include "gfcplaylistitem.h"
#include "gfctrackmanager.h"
#include "gfcplaybackmanager.h"

gfcPlaylistItemProgramState::gfcPlaylistItemProgramState(){

	this->layout=FRAMINGSINGLE_ID;
	this->playbackInfo.targetFPS=24;
	this->playbackInfo.loopPriority=GFC_LOOPPRIORITY_SHORTEST;
	this->playbackInfo.playbackMode=LOOPMODELOOP_ID;

};

gfcPlaylistItemProgramState::~gfcPlaylistItemProgramState(){};

void gfcPlaylistItemProgramState::saveStateToNode(XMLNode node) const
{
	XMLNode stateNode = node.addChild("pState");
	
	//plabackInfo
	XMLNode playbackNode = stateNode.addChild("pb");
	saveSetting("fr",playbackInfo.from,playbackNode);
	saveSetting("lp",playbackInfo.loopPriority,playbackNode);
	saveSetting("pbm",playbackInfo.playbackMode-LOOPMODEONCE_ID,playbackNode);
	saveSetting("fps",playbackInfo.targetFPS,playbackNode);
	saveSetting("to",playbackInfo.to,playbackNode);

	//layout
	saveSetting("layout",this->layout-FRAMINGSINGLE_ID,stateNode);
	
	//plate state info
	XMLNode platesNode = stateNode.addChild("plates");
	int plateSize=plateStateInfo.size();
	for (int i=0;i<plateSize;i++) { //save plate parameters for each plate
		XMLNode plateNode=platesNode.addChild("plate");
		
		saveSetting("plateID",i,plateNode);
		//printf("Saving trackID(%i): %i-%c\n",i,(int)plateStateInfo[i].track,plateStateInfo[i].track);
		saveSetting("trackID",(int)plateStateInfo[i].track,plateNode);
		//saveSetting("tX",this->plateStateInfo[i].tX,plateNode);
		//saveSetting("tY",this->plateStateInfo[i].tY,plateNode);
		//saveSetting("scale",this->plateStateInfo[i].scale,plateNode);
		saveSetting("flip",(int)this->plateStateInfo[i].flip,plateNode);
		saveSetting("flop",(int)this->plateStateInfo[i].flop,plateNode);
		saveSetting("crop",(int)this->plateStateInfo[i].crop,plateNode);
		saveSetting("aspect",this->plateStateInfo[i].aspect,plateNode);
		//saveSetting("aspect",this->plateStateInfo[i].myGUI->getAspectString(),plateNode);
		saveSetting("r",(int)this->plateStateInfo[i].r,plateNode);
		saveSetting("g",(int)this->plateStateInfo[i].g,plateNode);
		saveSetting("b",(int)this->plateStateInfo[i].b,plateNode);
		saveSetting("a",(int)this->plateStateInfo[i].a,plateNode);
	}
	
	//

	XMLNode tracksNode = stateNode.addChild("tracks");
	int tracksSize=this->trackStateInfo.size();
	for (int i=0;i<tracksSize;i++) { //save info for each track
		XMLNode trackNode=tracksNode.addChild("track");
		saveSetting("trackID",i,trackNode);
		saveSetting("offset",this->trackStateInfo[i].frameOffset,trackNode);
		saveSetting("holdMode",this->trackStateInfo[i].holdMode,trackNode);
		saveSetting("holdFrame",this->trackStateInfo[i].holdFrame,trackNode);
	}
}

void gfcPlaylistItemProgramState::loadStateFromNode(XMLNode node)
{
	XMLNode stateNode = node.getChildNode("pState");

	//plabackInfo
	XMLNode playbackNode = stateNode.getChildNode("pb");
	
	readSetting("fr",playbackInfo.from,playbackNode);
	readSetting("lp",playbackInfo.loopPriority,playbackNode);
	readSetting("pbm",playbackInfo.playbackMode,playbackNode);
	playbackInfo.playbackMode+=LOOPMODEONCE_ID;
	readSetting("fps",playbackInfo.targetFPS,playbackNode);
	readSetting("to",playbackInfo.to,playbackNode);

	//layout
	readSetting("layout",this->layout,stateNode);
	layout+=FRAMINGSINGLE_ID;

	//plate state info
	XMLNode platesNode = stateNode.getChildNode("plates");
	int plateSize=platesNode.nChildNode("plate");
	this->plateStateInfo.clear();
	this->plateStateInfo.resize(plateSize);
	int iter=0;
	for (int i=0;i<plateSize;i++) { //save plate parameters for each plate
		XMLNode plateNode=platesNode.getChildNode("plate",&iter);
		
		int track;
		track=readAttributeFromNode<int>("trackID",plateNode,i);

		this->plateStateInfo[i].track=track;
		//printf("gfcPlaylistItemProgramState::loadStateFromNode(XMLNode node): track: %i\n",track);
//		readSetting("tX",this->plateStateInfo[i].tX,plateNode);
//		readSetting("tY",this->plateStateInfo[i].tY,plateNode);
//		readSetting("scale",this->plateStateInfo[i].scale,plateNode);
		readSetting("flip",this->plateStateInfo[i].flip,plateNode);
		readSetting("flop",this->plateStateInfo[i].flop,plateNode);
		readSetting("crop",this->plateStateInfo[i].crop,plateNode);
		readSetting("aspect",this->plateStateInfo[i].aspect,plateNode);
		//printf("gfcPlaylistItemProgramState::loadStateFromNode(XMLNode node): aspect: %i\n",this->plateStateInfo[i].aspect.c_str());
//		readSetting("aspect",this->plateStateInfo[i].myGUI->getAspectString(),plateNode);
		readSetting("r",this->plateStateInfo[i].r,plateNode);
		readSetting("g",this->plateStateInfo[i].g,plateNode);
		readSetting("b",this->plateStateInfo[i].b,plateNode);
		readSetting("a",this->plateStateInfo[i].a,plateNode);

		//printf("plateNode: loadedFrom:%i track: %c \n",track,this->plateStateInfo[i].track);
	}

	//

	XMLNode tracksNode = stateNode.getChildNode("tracks");
	int tracksSize=stateNode.nChildNode("track");
	this->trackStateInfo.clear();
	this->trackStateInfo.resize(tracksSize);
	iter=0;
	for (int i=0;i<tracksSize;i++) { //save info for each track
		XMLNode trackNode=tracksNode.getChildNode("track",&iter);
		
		readSetting("offset",this->trackStateInfo[i].frameOffset,trackNode);
		readSetting("holdMode",this->trackStateInfo[i].holdMode,trackNode);
		readSetting("holdFrame",this->trackStateInfo[i].holdFrame,trackNode);
	}
}

gfcPlaylistItem::gfcPlaylistItem(void)
{
	selected=0;
	index=0;

}

gfcPlaylistItem::~gfcPlaylistItem(void)
{
}

bool gfcPlaylistItem::operator==(const gfcPlaylistItem &that){
	std::string ourString=this->asString();
	gfcPlaylistItem localThat=that;
	std::string theirString=that.asString();
	bool theResult=ourString==theirString;
	//printf("Comparing A:*******\n%s\n*****With B:******\n%s\n******Result is: %i\n",ourString.c_str(),theirString.c_str(),theResult);
	
	return theResult;
}

void gfcPlaylistItem::savePlaylistItemParameters(XMLNode &entryNode) const
{
	//XMLNode entryNode=plNode.addChild("e");
	
	//XMLNode entryNode=XMLNode::createXMLTopNode("e");
	//entryNode.updateName("e");
	for (int j=0;j<loadParams.size();j++)
	{

		gfcLoadParams tmplp=loadParams[j];

		XMLNode trackNode = entryNode.addChild("t");
		saveSetting("fn",tmplp.fileName,trackNode);
		if(tmplp.fileName!=""){
			saveSetting("fr",tmplp.fromFrame,trackNode);
			saveSetting("to",tmplp.toFrame,trackNode);
			saveSetting("ap",(char)tmplp.append,trackNode);
			saveSetting("s",tmplp.scale,trackNode);
			saveSetting("ch",tmplp.channel,trackNode);
			saveSetting("cn",tmplp.channelName,trackNode);
			saveSetting("fl",tmplp.filterType,trackNode);
			saveSetting("cm",(char)tmplp.compressed,trackNode);
			saveSetting("cr",(bool)tmplp.crop,trackNode);
			saveSetting("ax",tmplp.aoi.x,trackNode);
			saveSetting("ay",tmplp.aoi.y,trackNode);
			saveSetting("aw",tmplp.aoi.w,trackNode);
			saveSetting("ah",tmplp.aoi.h,trackNode);
		}
	}
	//also save the FX stacks
	for (int k=0;k<fxstacks.size();k++)
	{
		fxstacks[k].saveStackToNode(entryNode);
	}
	

	//also save the program state! damn!
	programState.saveStateToNode(entryNode);

}

void gfcPlaylistItem::loadPlaylistItemParameters(XMLNode &entryNode)
{
	//2. Iterate through all the tracks in the entryNode
	int tsize=entryNode.nChildNode("t");

	int titerator=0;
	for (int t=0;t<tsize;t++)
	{
		XMLNode trackNode = entryNode.getChildNode("t",&titerator);
		gfcLoadParams p;
		//extract all the info from the trackNode into an entry
		/*if(trackNode.getAttribute("fn")!=NULL)
		{
		printf("filename: %s\n",trackNode.getAttribute("fn"));
		p.fileName=trackNode.getAttribute("fn");
		}*/

		readSettingString("fn",p.fileName,trackNode);
		//printf("filename: %s\n",p.fileName.c_str());
		if (p.fileName!="")
		{
			readSetting("fr",p.fromFrame,trackNode);
			readSetting("to",p.toFrame,trackNode);
			readSetting("ap",p.append,trackNode);
			readSetting("s",p.scale,trackNode);
			readSetting("ch",p.channel,trackNode);
			readSettingString("cn",p.channelName,trackNode);
			readSetting("fl",p.filterType,trackNode);
			readSetting("cm",p.compressed,trackNode);
			readSetting("cr",p.crop,trackNode);
			readSetting("ax",p.aoi.x,trackNode);
			readSetting("ay",p.aoi.y,trackNode);
			readSetting("aw",p.aoi.w,trackNode);
			readSetting("ah",p.aoi.h,trackNode);

			loadParams.push_back(p);
		}

	}

	//2. Iterate through all the FXs
	int fsize=entryNode.nChildNode("FXS");
	int fiterator=0;
	for (int f=0;f<fsize;f++)
	{
		gfcFXStack fxs;
		fxs.loadStackFromNode(entryNode,&fiterator);
		fxstacks.push_back(fxs);
	}

	//also load the program state
	this->programState.loadStateFromNode(entryNode);
}

std::string gfcPlaylistItem::asString() const
{
	XMLNode entryNode=XMLNode::createXMLTopNode("e");
	this->savePlaylistItemParameters(entryNode);
	return entryNode.createXMLString();
}


void gfcPlaylistItem::appendTracks(const std::vector<std::string> &files)
{
	int i=0;
	printf("Load params size=%i\nfiles.size:%i\n",loadParams.size(),files.size());
	while (i<files.size() && loadParams.size()< gfcTrackManager::MAX_SEQUENCES)
	{
		loadParams.push_back(gfcSequence::getDefaultLoadParamsFor(getFirstSequenceInDirectory(files[i])));
		i++;
		printf("Load params size=%i\n",loadParams.size());
	}

	
	std::cout<<"Appended "<< i << " new items\n";
}



void gfcPlaylistItem::fixWindowsPaths()
{
	for (int j=0;j<loadParams.size();j++)
	{
	loadParams[j].fixWindowsPath();
	}
}