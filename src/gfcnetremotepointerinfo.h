#ifndef GFCNETREMOTEPOINTERINFO_H
#define GFCNETREMOTEPOINTERINFO_H
/*
Stores info on the remote pointer of the other peers in the network. A map stores the info for each client using their nick as map key. Every time the pointer is drawn, the
fade counter is diminished, if it reaches 0, it is removed from the map. The fadeCounter is replentished whenever a new pointer info message for that client is received.
*/
struct gfcNetRemotePointerInfo{
	std::string name;
	int quadID;
	int x;
	int y;
	float fadeCounter;
	float headFadeCounter; //the head needs to live a little longer, therefore it has it's own variable.
	float scale;
	int color;
};
#endif
