#include "gfcplaylistmanager.h"
#include <vector>
#include <string>
#include "gfctrackmanager.h"
/*OTHER MANAGERS AND WINDOWS*/
#include "mainWindow.h"
extern MainWindow mw;

#include "playlistwindow.h"
extern PlaylistWindow plw;

#include "loadWindow.h"
extern LoadWindow lw;

#include "gfcsessionmanager.h"
extern gfcSessionManager sessionManager;

#include "gfcplaybackmanager.h"
extern gfcPlaybackManager playbackManager;

#include "gfcplatemanager.h"
extern gfcPlateManager plateManager;

#include "gfcnetworkmanager.h"
extern gfcNetworkManager networkManager;

/**************************/

gfcPlaylistManager::gfcPlaylistManager(void)
{
}

gfcPlaylistManager::~gfcPlaylistManager(void)
{
}

void gfcPlaylistManager::appendTracksToItem(const std::vector<std::string> &files, int index)
{
	if(index>=entries.size() || index<0)
	{
		printf("gfcPlaylistManager::appendTracksToItem: requested item out of bounds %i\n",index);
	}
	else
	{
		entries[index].appendTracks(files);
	}
	plw.scheduleWindowUpdate();
	networkManager.sendPlaylist(this->getPlaylistAsString());
}

void gfcPlaylistManager::refreshSelectedItem()
{
	for (int i=0; i<entries.size(); i++)
	{
		if (entries[i].selected==1)
		{
			selectedItem=i;
			return;
		}
	}
}

gfcPlaylistItem gfcPlaylistManager::createPlaylistItemFrom(std::vector<std::string> filenames)
{
	gfcPlaylistItem result;
	for (int i=0;i<filenames.size() && i<gfcTrackManager::MAX_SEQUENCES;i++)
	{
		result.loadParams.push_back(gfcSequence::getDefaultLoadParamsFor(getFirstSequenceInDirectory(filenames[i])));
	}
	result.fxstacks=plateManager.getPlateFXStacks();

	return result;
}

void gfcPlaylistManager::setSelectedItem(int index)
{
	if(index>=entries.size() || index<0)
	{
		printf("gfcPlaylistManager::getItem: requested item out of bounds %i\n",index);
		for (int i=0;i<entries.size();i++)
		{
			{
				entries[i].selected=0;
			}
		}
		selectedItem=-1;
		
	}
	else
	{
		for (int i=0;i<entries.size();i++)
		{
			if (i==index)
			{
				entries[i].selected=1;
			}
			else
			{
				entries[i].selected=0;
			}
		}
		selectedItem=index;

		gfcNetPlaylistEvent tmpEvent;
		tmpEvent.selectedItem=selectedItem;
		networkManager.sendPlaylistEvent(tmpEvent);

		
	}

	plw.scheduleWindowUpdate();
}

int gfcPlaylistManager::addItemlist(gfcPlaylistItem theItem, int noRepeat)
{

	if (noRepeat==1)
	{
		//check if the item already exists here, if it exists return, otherwise continue
		/*std::vector<gfcPlaylistItem>::iterator start=entries.begin, end=entries.end();
		for (entries.begin(),entries.end())
		{
		}*/
		
		std::vector<gfcPlaylistItem>::iterator exists=entries.end();
		exists=std::find(entries.begin(),entries.end(),theItem);
		if (exists!=entries.end())
		{
			printf("Item already exists:\n*****%s*******\n",theItem.asString().c_str());
			return entries.size()-1; //returns the index of the last inserted
		}
		
	}

	theItem.fixWindowsPaths();
	entries.push_back(theItem);
	networkManager.sendPlaylist(this->getPlaylistAsString());
	return entries.size()-1; //returns the index of the last inserted
}

gfcPlaylistItem gfcPlaylistManager::getItem(int index)
{
	if(index>=entries.size() || index<0)
	{
		printf("gfcPlaylistManager::getItem: requested item out of bounds %i\n",index);
		gfcPlaylistItem dummy;
		return dummy;
	}
	else
	{
#ifdef WIN32
		gfcPlaylistItem tmpResult=entries[index];
		tmpResult.fixWindowsPaths();
		return tmpResult;
#else
		return entries[index];
#endif
	}
}


std::vector<gfcPlaylistItem> * gfcPlaylistManager::getPlaylist()
{
	return &entries;
}

void gfcPlaylistManager::addPLIGUIInfo(PlaylistParamInfo info, Fl_Widget* o)
{
	this->guiToPlaylistItem[o]=info;
}

void gfcPlaylistManager::handlePLIGUICB(Fl_Widget*o, void* data)
{
	PlaylistParamInfo info = guiToPlaylistItem[o];

	switch(info.type)
	{
	case PL_GUI_LOAD:
		{
			//printf("Here we load this playlist item %i!\n",info.plIndex);
			plw.scheduleWindowUpdate();
			//here we do something with the track manager I guess.
		}
		break;

	case PL_GUI_DELETE:
		{
			//printf("Here we delete this playlist item %i!\n",info.plIndex);
			this->removePlaylistItem(info.plIndex);
			plw.scheduleWindowUpdate();
		}
		break;

	case PL_GUI_MOVEDOWN:
		{
			//printf("Here we move this playlist item down %i!\n",info.plIndex);
			movePlaylistItem(info.plIndex,-1);
			plw.scheduleWindowUpdate();
		}
		break;

	case PL_GUI_MOVEUP:
		{
			//printf("Here we move this playlist item up %i!\n",info.plIndex);
			movePlaylistItem(info.plIndex,1);
			plw.scheduleWindowUpdate();
		}
		break;

	}
}

void gfcPlaylistManager::moveSelection(int direction)
{
	if (selectedItem==0 && direction==1)
	{
		//special case to loop around to the last one
		setSelectedItem((entries.size())-1);
	}
	else{
		if(entries.size()){
		setSelectedItem((selectedItem-direction)%(entries.size()));
		}
	}
	return;
	/*
	if(selectedItem>=entries.size() || selectedItem<0)
	{
		printf("gfcPlaylistManager::moveSelection: requested item out of bounds %i\n",index);
		return;
	}
	else
	{	
		switch(direction)
		{
		case 1:
			{
				if (selectedItem>0)
				{
					//"inefficient" swap but who cares, it's a gui operation
					entries[selectedItem-1].selected=true;
					entries[]
				}
			}
			break;
		case -1:
			{
				if (index<entries.size()-1) //there is room to move down.
				{
					gfcPlaylistItem tmp=entries[index];
					entries[index]=entries[index+1];
					entries[index+1]=tmp;
				}
			}
			break;
		}
		refreshSelectedItem();
	}*/
}

void gfcPlaylistManager::movePlaylistItem(int index, int direction)
{
	if(index>=entries.size() || index<0)
	{
		printf("gfcPlaylistManager::movePlaylistItem: requested item out of bounds %i\n",index);
		return;
	}
	else
	{	
		switch(direction)
		{
		case 1:
			{
				if (index>0)
				{
					//"inefficient" swap but who cares, it's a gui operation
					gfcPlaylistItem tmp=entries[index];
					entries[index]=entries[index-1];
					entries[index-1]=tmp;
				}
			}
			break;
		case -1:
			{
				if (index<entries.size()-1) //there is room to move down.
				{
					gfcPlaylistItem tmp=entries[index];
					entries[index]=entries[index+1];
					entries[index+1]=tmp;
				}
			}
			break;
		}
		refreshSelectedItem();
	}
	networkManager.sendPlaylist(this->getPlaylistAsString());
	
}

void gfcPlaylistManager::clearPlaylist(int notifyNetwork)
{
	entries.clear();
	plw.scheduleWindowUpdate();
	if(notifyNetwork){
		networkManager.sendPlaylist(this->getPlaylistAsString());
	}
}

void gfcPlaylistManager::removePlaylistItem(int index)
{
	if(index>=entries.size() || index<0)
	{
		printf("gfcPlaylistManager::removePlaylistItem: requested item out of bounds %i\n",index);
		return;
	}
	else
	{
		entries.erase(entries.begin()+index);
		refreshSelectedItem();
	}
	
	networkManager.sendPlaylist(this->getPlaylistAsString());
}

void gfcPlaylistManager::savePlaylistParameters(XMLNode &plNode)
{

	//TODO: This is fucked up right here, each playlist item needs to be responsible for it's own info.
	//this is already done in the playlistitem! don't do it again!, but, the playlist item also needs to save the program state!
	int count=entries.size();
	for (int i=0;i<entries.size();i++)
	{
		XMLNode entryNode=plNode.addChild("e");

		entries[i].savePlaylistItemParameters(entryNode);
		/*
		gfcPlaylistItem tmp=entries[i];
		for (int j=0;j<tmp.loadParams.size();j++)
		{
			
			gfcLoadParams tmplp=tmp.loadParams[j];
			
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
		for (int k=0;k<tmp.fxstacks.size();k++)
		{
			tmp.fxstacks[k].saveStackToNode(entryNode);
		}
		*/
	}
}

std::string gfcPlaylistManager::getPlaylistAsString(){
	//XMLNode plnode =XMLNode::createXMLTopNode("pl");

	XMLNode xMainNode=XMLNode::createXMLTopNode ( "xml",TRUE );
	xMainNode.addAttribute ( "version","1.0" );
	XMLNode xRootNode=xMainNode.addChild ( "root" );
	xRootNode.addAttribute ( "comment", "JefeCheck Playlist File" );

	XMLNode entriesNode = xRootNode.addChild("entries");
	this->savePlaylistParameters(entriesNode);
	
	return xMainNode.createXMLString();
}

void gfcPlaylistManager::setPlaylistFromString(std::string s, int replace){
	XMLNode rootNode=XMLNode::parseString(s.c_str());
	XMLNode realRoot = rootNode.getChildNode("root");
	XMLNode entriesNode = realRoot.getChildNode("entries");
	this->loadPlaylistParameters(entriesNode, replace);

}

void gfcPlaylistManager::mergePlaylist(std::string s)
{
	//for now, just append the old playlist to the new one.
	XMLNode xmlDoc=XMLNode::parseString(s.c_str());
	XMLNode realRoot = xmlDoc.getChildNode("root");
	XMLNode entriesNode = realRoot.getChildNode("entries");

	this->loadPlaylistParameters(entriesNode, 0, 1); //we send 0 and 1 to not replace and avoid repeats
	printf("Our new playlist has %i entries\n",this->entries.size());
}

void gfcPlaylistManager::savePlaylist(std::string filename)
{
	if ( filename.empty() ) {
		printf ( "SavePlaylist Error: Empty filename\n" );
		return;
	}

	filename=AppendExtensionToFilename(filename,".jpl");

	//printf ( "Saving Session to %s\n",filename.c_str() );

	XMLNode tmpDoc = XMLNode::parseString(this->getPlaylistAsString().c_str());

	XMLError writeError=tmpDoc.writeToFile ( filename.c_str() );

	if ( writeError!=eXMLErrorNone ) {
		printf ( "Error writing JPL file!\n" );
	} else {
		printf ( "Session saved to %s\n",filename.c_str() );
	}

}

void gfcPlaylistManager::loadPlaylist(std::string filename)
{
	if ( filename.empty() ) {
		printf ( "LoadPlaylist: Error, filename is empty\n" );
		return;
	}

	XMLNode xMainNode=XMLNode::openFileHelper ( filename.c_str() );
	if(!xMainNode.loaded)
	{
		printf("LoadPlaylist: Error opening playlist file\n");
		return;
	}
	XMLNode xRootNode=xMainNode.getChildNode ( "root" );
	XMLNode entriesNode=xRootNode.getChildNode ( "entries" );

	this->loadPlaylistParameters(entriesNode);
}

void gfcPlaylistManager::loadPlaylistParameters(XMLNode &plNode, int replace, int noRepeats)
{
	//1. Iterate through all the entries
	int esize=plNode.nChildNode("e");
	//printf("number of entries: %i",esize);
	int eiterator=0;

	if (replace)
	{
		//clear the previous items first
		this->clearPlaylist(0);
	}

	for (int i=0;i<esize;i++)
	{
		gfcPlaylistItem item;
		XMLNode entryNode=plNode.getChildNode("e",&eiterator);
		item.loadPlaylistItemParameters(entryNode);
		this->addItemlist(item,noRepeats);
	}

	plw.scheduleWindowUpdate();

	networkManager.sendPlaylist(this->getPlaylistAsString());
	
}

void gfcPlaylistManager::handlePlaylistEventOther(gfcNetPlaylistEvent theEvent)
{
	setSelectedItem(theEvent.selectedItem);
}