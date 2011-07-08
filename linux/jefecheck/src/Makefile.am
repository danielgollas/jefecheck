bin_PROGRAMS = jefecheck
jefecheck_SOURCES = AsynchronousFileIO.cpp AutoRPC.cpp BigInt.cpp BitStream.cpp \
	BitStream_NoTemplate.cpp CheckSum.cpp CommandParserInterface.cpp ConnectionGraph.cpp \
	ConsoleServer.cpp DS_BytePool.cpp DS_ByteQueue.cpp DS_HuffmanEncodingTree.cpp \
	DS_Table.cpp DataBlockEncryptor.cpp DataCompressor.cpp DirectoryDeltaTransfer.cpp \
	EmailSender.cpp EncodeClassName.cpp EpochTimeToString.cpp ExtendedOverlappedPool.cpp \
	FileList.cpp FileListTransfer.cpp FileOperations.cpp Fl_Button_RGBA_gfc.cpp \
	Fl_Button_gfc.cpp Fl_Choice_gfc.cpp Fl_DragBar.cxx Fl_Input_Choice_gfc.cpp \
	Fl_Slider_Timeline_gfc.cpp Fl_Spinner_gfc.cpp FormatString.cpp FullyConnectedMesh.cpp \
	FunctionThread.cpp Gen_RPC8.cpp GetTime.cpp GlViewport.cpp GridSectorizer.cpp \
	HTTPConnection.cpp IncrementalReadInterface.cpp InlineFunctor.cpp Itoa.cpp \
	LightweightDatabaseClient.cpp LightweightDatabaseCommon.cpp LightweightDatabaseServer.cpp \
	LinuxStrings.cpp LogCommandParser.cpp MessageFilter.cpp NatPunchthrough.cpp \
	NetworkIDManager.cpp NetworkIDObject.cpp PacketConsoleLogger.cpp PacketFileLogger.cpp \
	PacketLogger.cpp PluginInterface.cpp RPCMap.cpp RSACrypt.cpp RakMemoryOverride.cpp \
	RakNetCommandParser.cpp RakNetStatistics.cpp RakNetTransport.cpp RakNetTypes.cpp \
	RakNetworkFactory.cpp RakPeer.cpp RakSleep.cpp RakString.cpp RakThread.cpp Rand.cpp \
	ReadyEvent.cpp ReliabilityLayer.cpp ReplicaManager.cpp ReplicaManager2.cpp Router.cpp \
	SHA1.cpp SimpleMutex.cpp SocketLayer.cpp StringCompressor.cpp StringTable.cpp \
	SuperFastHash.cpp SystemAddressList.cpp TCPInterface.cpp TableSerializer.cpp \
	TelnetTransport.cpp ThreadsafePacketLogger.cpp UICallbacks.cpp WSAStartupSingleton.cpp \
	_FindFirst.cpp aboutWindow.cxx activatorCallbacks.cpp activatorWindow.cxx \
	checkmateResoucesWindow.cxx common.cxx dpxslice.cpp drawingToolsWindow.cxx exrWindow.cxx \
	fxWindow.cxx fxcontrolwindow.cpp gammaWindow.cxx gfcFXControlBase.cpp \
	gfcNetworkStructures.cpp gfcPlate.cpp gfcSequence.cpp gfcSliderInput.cpp gfcStructures.cpp \
	gfcframe.cpp gfcfx.cpp gfcfxmanager.cpp gfcfxstack.cpp gfcglframeinfo.cpp \
	gfcglsubwindow.cpp gfchistogram.cpp gfchistogramglwindow.cpp gfcimageloader.cpp \
	gfcimageloaderdpx.cpp gfcimageloaderexr.cpp gfcimageloaderfil.cpp gfcimageloadergfl.cpp \
	gfcimageprocessor.cpp gfcimagesaver.cpp gfcimagesaver_exr.cpp gfcimagesaver_gfl.cpp \
	gfcloadparams.cpp gfclutmanager.cpp gfcmemorymanager.cpp gfcnetworkclient.cpp \
	gfcnetworkclientgui.cpp gfcnetworkclientgui_fltk.cpp gfcnetworkeventnotification.cpp \
	gfcnetworklog.cpp gfcnetworkmanager.cpp gfcnetworkserver.cpp gfcnetworkservergui.cpp \
	gfcnetworkservergui_fltk.cpp gfcnote.cpp gfcnotetext.cpp gfcpeekinfo.cpp gfcpickdrawee.cpp \
	gfcpickmanager.cpp gfcpicknotifee.cpp gfcpickobject.cpp gfcplatedrawparams.cpp \
	gfcplategui.cpp gfcplategui_fltk.cpp gfcplatemanager.cpp gfcplaybackgui.cpp \
	gfcplaybackgui_fltk.cpp gfcplaybackmanager.cpp gfcplaylistitem.cpp gfcplaylistmanager.cpp \
	gfcplaylistwindowwindow.cpp gfcpointerstorage.cpp gfcrectang.cpp gfcrenderparams.cpp gfcreview.cpp \
	gfcrevision.cpp gfcsequencegui.cpp gfcsequencegui_fltk.cpp gfcsessionmanager.cpp \
	gfctrackmanager.cpp gflCBitmap.cpp gflCFileInformation.cpp gflCFormat.cpp loadWindow.cxx \
	lut1d.cpp lutWindow.cxx main.cpp mainWindow.cxx minSpecsWindow.cxx \
	moreOptionWindow.cxx mtpoly.cpp platefxparams.cpp playlistwindow.cpp preferencesWindow.cxx \
	remoteWindow.cxx renderWindow.cxx rijndael.cpp splashWindow.cxx trackwidget.cpp \
	trilerp.cpp vec3d.cpp xmlParser.cpp

# set the include path found by configure
INCLUDES = $(all_includes) -I/usr/local/include/

# the library search path.
jefecheck_LDFLAGS = $(all_libraries) $(boostLibPath)/$(boostFileSystemLibs) \
	$(boostLibPath)/$(boostProgramOptionsLibs) $(boostLibPath)/$(boostSystemLibs) $(boostLibPath)/$(boostThreadLibs) \
	$(botanLibs) $(curlLibs) $(exrLibs) $(glutLibCommand) $(ssllibPath) $(gflLibs) \
	$(filLibs) -lGLEW $(fltkLibs) $(fluFlags)
noinst_HEADERS = AsynchronousFileIO.h AutoRPC.h AutopatcherPatchContext.h \
	AutopatcherRepositoryInterface.h BigInt.h BigTypes.h BitStream.h BitStream_NoTemplate.h \
	CheckMateBetaDemoVersionWatermark.h CheckSum.h ClientContextStruct.h CommandParserInterface.h \
	ConnectionGraph.h ConsoleServer.h DS_BPlusTree.h DS_BinarySearchTree.h DS_BytePool.h \
	DS_ByteQueue.h DS_Heap.h DS_HuffmanEncodingTree.h DS_HuffmanEncodingTreeFactory.h \
	DS_HuffmanEncodingTreeNode.h DS_LinkedList.h DS_List.h DS_Map.h DS_MemoryPool.h \
	DS_OrderedChannelHeap.h DS_OrderedList.h DS_Queue.h DS_QueueLinkedList.h DS_RangeList.h \
	DS_Table.h DS_Tree.h DS_WeightedGraph.h DataBlockEncryptor.h DataCompressor.h \
	DirectoryDeltaTransfer.h EmailSender.h EpochTimeToString.h Export.h ExtendedOverlappedPool.h \
	FileList.h FileListNodeContext.h FileListTransfer.h FileListTransferCBInterface.h \
	FileOperations.h Fl_Button_RGBA_gfc.h Fl_Button_gfc.h Fl_Choice_gfc.h Fl_DragBar.H \
	Fl_Input_Choice_gfc.h Fl_Slider_Timeline_gfc.h Fl_Spinner_gfc.h FormatString.h \
	FullyConnectedMesh.h FunctionThread.h Gen_RPC8.h GetTime.h GlViewport.h GridSectorizer.h \
	HTTPConnection.h IncrementalReadInterface.h InlineFunctor.h InternalPacket.h Itoa.h \
	JefeCorp_JefeCheck1LicenseDSA_public.h JefeCorp_JefeCheck1_ActivatorRSA_public.h \
	JefeCorp_JefeCheck_LicenseRSA_public.h Kbhit.h LightweightDatabaseClient.h LightweightDatabaseCommon.h \
	LightweightDatabaseServer.h LinuxStrings.h LobbyClientInterface.h LogCommandParser.h MTUSize.h \
	MessageFilter.h MessageIdentifiers.h NatPunchthrough.h NativeTypes.h NetworkIDManager.h \
	NetworkIDObject.h PacketConsoleLogger.h PacketFileLogger.h PacketLogger.h PacketPool.h \
	PacketPriority.h Platform.h PluginInterface.h RPCMap.h RPCNode.h RSACrypt.h RakAssert.h \
	RakMemoryOverride.h RakNetCommandParser.h RakNetDefines.h RakNetStatistics.h RakNetTime.h \
	RakNetTransport.h RakNetTypes.h RakNetVersion.h RakNetworkFactory.h RakPeer.h \
	RakPeerInterface.h RakSleep.h RakString.h RakThread.h Rand.h ReadyEvent.h RefCountedObj.h \
	ReliabilityLayer.h Replica.h ReplicaEnums.h ReplicaManager.h ReplicaManager2.h \
	Rijndael-Boxes.h Rijndael.h Router.h RouterInterface.h SHA1.h SimpleMutex.h \
	SimpleTCPServer.h SingleProducerConsumer.h SocketLayer.h StringCompressor.h StringTable.h \
	SuperFastHash.h SystemAddressList.h TCPInterface.h TableSerializer.h TelnetTransport.h \
	ThreadPool.h ThreadsafePacketLogger.h TransportInterface.h Types.h UICallbacks.h \
	UIConstants.h WSAStartupSingleton.h _FindFirst.h aboutWindow.h activatorCallbacks.h \
	activatorWindow.h checkmateResoucesWindow.h demoversion.h \
	dpx_image_information_interface.h dpxslice.h drawingToolsWindow.h exrWindow.h fxWindow.h \
	fxcontrolwindow.h gammaWindow.h gfcFXControlBase.h gfcNetworkStructures.h gfcPlate.h \
	gfcSequence.h gfcSliderInput.h gfcStructures.h gfcdpxstructures.h gfcframe.h gfcfx.h \
	gfcfxmanager.h gfcfxstack.h gfcglframeinfo.h gfcglsubwindow.h gfchistogram.h \
	gfchistogramglwindow.h gfcimageloader.h gfcimageloaderdpx.h gfcimageloaderexr.h \
	gfcimageloaderfil.h gfcimageloadergfl.h gfcimageprocessor.h gfcimagesaver.h \
	gfcimagesaver_exr.h gfcimagesaver_gfl.h gfcloadparams.h gfclutmanager.h gfcmemorymanager.h \
	gfcnetremotepointerinfo.h gfcnetworkclient.h gfcnetworkclientgui.h gfcnetworkclientgui_fltk.h \
	gfcnetworkeventnotification.h gfcnetworklog.h gfcnetworkmanager.h gfcnetworkserver.h \
	gfcnetworkservergui.h gfcnetworkservergui_fltk.h gfcnote.h gfcnotetext.h gfcpeekinfo.h \
	gfcpickdrawee.h gfcpickmanager.h gfcpicknotifee.h gfcpickobject.h gfcplatedrawparams.h \
	gfcplategui.h gfcplategui_fltk.h gfcplatemanager.h gfcplaybackgui.h \
	gfcplaybackgui_fltk.h gfcplaybackmanager.h gfcplaylistitem.h gfcplaylistmanager.h \
	gfcplaylistwindowwindow.h gfcpointerstorage.h gfcrectang.h gfcrenderparams.h gfcreview.h \
	gfcrevision.h gfcsequencegui.h gfcsequencegui_fltk.h gfcsessionmanager.h \
	gfctrackmanager.h gflC.h gflCBitmap.h gflCColor.h gflCColormap.h gflCException.h \
	gflCFileInformation.h gflCFormat.h gflCLibrary.h gflCLoadParams.h gflCSaveParams.h glew.h \
	glext.h glxew.h loadWindow.h lut1d.h lutWindow.h mainWindow.h mainWindowTest.h \
	minSpecsWindow.h moreOptionWindow.h mtpoly.h osxGetPrimaryMacAddress.h platefxparams.h \
	playlistwindow.h preferencesWindow.h publicKey.h remoteWindow.h renderWindow.h \
	resource.h resource1.h splashWindow.h trackwidget.h trilerp.h vec3d.h wglew.h \
	wglext.h xmlParser.h
AM_CXXFLAGS = $(gflIncludes) $(boostIncludes) $(botanIncludes) $(exrIncludes) \
	$(fltkLibs) $(fluFlags)
