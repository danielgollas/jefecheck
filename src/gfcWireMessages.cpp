// jefe::wire message codecs. See gfcWireMessages.h for the codec contract;
// each codec's field sequence matches its legacy BitStream pair.

#include "gfcWireMessages.h"

namespace jefe {
namespace wire {

// ---------------------------------------------------------------------
// PlayPause
// Legacy pair: gfcnetworkclient.cpp SendPlayPauseMessage / GFCNETID_PLAYPAUSEMESSAGE.
// ---------------------------------------------------------------------
void encodePlayPause(Writer& w, const gfcNetPlayPauseInfo& info) {
    w.writeBool(info.play);
    w.writeI32(info.frame);
    w.writeI32(info.direction);
}

bool decodePlayPause(Reader& r, gfcNetPlayPauseInfo& info) {
    return r.readBool(info.play) && r.readI32(info.frame) &&
           r.readI32(info.direction);
}

// ---------------------------------------------------------------------
// PointerInfo (client -> server; no color, no nickname)
// Legacy pair: gfcnetworkclient.cpp SendPointerInfoMessage / gfcnetworkserver.cpp GFCNETID_POINTERINFOMESSAGE.
// ---------------------------------------------------------------------
void encodePointerInfo(Writer& w, const gfcNetPointerInfo& info) {
    w.writeI32(info.quadID);
    w.writeI32(info.x);
    w.writeI32(info.y);
    w.writeF32(info.scale);
}

bool decodePointerInfo(Reader& r, gfcNetPointerInfo& info) {
    return r.readI32(info.quadID) && r.readI32(info.x) && r.readI32(info.y) &&
           r.readF32(info.scale);
}

// ---------------------------------------------------------------------
// RemotePointerInfo (server -> clients broadcast)
// Legacy pair: gfcnetworkserver.cpp GFCNETID_POINTERINFOMESSAGE / GFCNETID_POINTERINFOBROADCASTMESSAGE.
// ---------------------------------------------------------------------
void encodeRemotePointerInfo(Writer& w, const gfcNetRemotePointerInfo& info) {
    w.writeI32(info.quadID);
    w.writeI32(info.x);
    w.writeI32(info.y);
    w.writeF32(info.scale);
    w.writeString(info.name);
    w.writeI32(info.color);
}

bool decodeRemotePointerInfo(Reader& r, gfcNetRemotePointerInfo& info) {
    return r.readI32(info.quadID) && r.readI32(info.x) && r.readI32(info.y) &&
           r.readF32(info.scale) && r.readString(info.name) &&
           r.readI32(info.color);
}

// ---------------------------------------------------------------------
// Transformations
// Legacy pair: gfcnetworkclient.cpp SendTransformations / GFCNETID_TRANSFORMATIONMESSAGE.
// ---------------------------------------------------------------------
void encodeTransformations(Writer& w,
                           const std::vector<gfcNetTransformationInfo>& v) {
    w.writeU32(static_cast<uint32_t>(v.size()));
    for (const gfcNetTransformationInfo& t : v) {
        w.writeF32(t.tX);
        w.writeF32(t.tY);
        w.writeF32(t.scale);
        w.writeF32(t.rZ);
    }
}

bool decodeTransformations(Reader& r,
                           std::vector<gfcNetTransformationInfo>& v) {
    v.clear();
    uint32_t count = 0;
    if (!r.readU32(count)) return false;
    for (uint32_t i = 0; i < count; ++i) {
        gfcNetTransformationInfo t;
        if (!(r.readF32(t.tX) && r.readF32(t.tY) && r.readF32(t.scale) &&
              r.readF32(t.rZ)))
            return false;
        v.push_back(t);
    }
    return true;
}

// ---------------------------------------------------------------------
// ColorCorrections
// Legacy pair: gfcnetworkclient.cpp SendColorCorrections / GFCNETID_COLORCORRECTIONMESSAGE.
// ---------------------------------------------------------------------
void encodeColorCorrections(
    Writer& w, const std::vector<gfcNetPlateColorCorrectionInfo>& v) {
    w.writeU32(static_cast<uint32_t>(v.size()));
    for (const gfcNetPlateColorCorrectionInfo& c : v) {
        w.writeI32(c.quadID);
        w.writeString(c.lutName);
        w.writeF32(c.gamma);
        w.writeF32(c.exposure);
        w.writeF32(c.brightness);
        w.writeF32(c.contrast);
        w.writeF32(c.saturation);
    }
}

bool decodeColorCorrections(Reader& r,
                            std::vector<gfcNetPlateColorCorrectionInfo>& v) {
    v.clear();
    uint32_t count = 0;
    if (!r.readU32(count)) return false;
    for (uint32_t i = 0; i < count; ++i) {
        gfcNetPlateColorCorrectionInfo c;
        if (!(r.readI32(c.quadID) && r.readString(c.lutName) &&
              r.readF32(c.gamma) && r.readF32(c.exposure) &&
              r.readF32(c.brightness) && r.readF32(c.contrast) &&
              r.readF32(c.saturation)))
            return false;
        v.push_back(c);
    }
    return true;
}

// ---------------------------------------------------------------------
// OtherStates
// Legacy pair: gfcnetworkclient.cpp SendOtherStatesMessage / GFCNETID_OTHERSTATESMESSAGE.
// ---------------------------------------------------------------------
void encodeOtherStates(Writer& w, const gfcNetOtherStatesInfo& info) {
    w.writeI32(info.playbackInfo.from);
    w.writeI32(info.playbackInfo.to);
    w.writeF32(info.playbackInfo.targetFPS);
    w.writeI32(info.playbackInfo.playbackMode);
    w.writeI32(info.playbackInfo.loopPriority);
    w.writeI32(info.playbackInfo.inPoint);
    w.writeI32(info.playbackInfo.outPoint);

    w.writeI32(info.layout);
    w.writeU32(static_cast<uint32_t>(info.plateStateInfo.size()));
    for (const gfcNetPlateStateInfo& p : info.plateStateInfo) {
        w.writeU8(p.track);
        w.writeI32(p.quadID);
        w.writeBool(p.flip);
        w.writeBool(p.flop);
        w.writeBool(p.a);
        w.writeBool(p.r);
        w.writeBool(p.g);
        w.writeBool(p.b);
        w.writeString(p.aspect);
        w.writeBool(p.crop);
    }

    w.writeU32(static_cast<uint32_t>(info.trackStateInfo.size()));
    for (const gfcNetTrackStateInfo& t : info.trackStateInfo) {
        w.writeI32(t.frameOffset);
        w.writeI32(t.holdMode);
        w.writeI32(t.holdFrame);
    }
}

bool decodeOtherStates(Reader& r, gfcNetOtherStatesInfo& info) {
    if (!(r.readI32(info.playbackInfo.from) &&
          r.readI32(info.playbackInfo.to) &&
          r.readF32(info.playbackInfo.targetFPS) &&
          r.readI32(info.playbackInfo.playbackMode) &&
          r.readI32(info.playbackInfo.loopPriority) &&
          r.readI32(info.playbackInfo.inPoint) &&
          r.readI32(info.playbackInfo.outPoint)))
        return false;

    if (!r.readI32(info.layout)) return false;

    info.plateStateInfo.clear();
    uint32_t plateCount = 0;
    if (!r.readU32(plateCount)) return false;
    for (uint32_t i = 0; i < plateCount; ++i) {
        gfcNetPlateStateInfo p;
        if (!(r.readU8(p.track) && r.readI32(p.quadID) && r.readBool(p.flip) &&
              r.readBool(p.flop) && r.readBool(p.a) && r.readBool(p.r) &&
              r.readBool(p.g) && r.readBool(p.b) && r.readString(p.aspect) &&
              r.readBool(p.crop)))
            return false;
        info.plateStateInfo.push_back(p);
    }

    info.trackStateInfo.clear();
    uint32_t trackCount = 0;
    if (!r.readU32(trackCount)) return false;
    for (uint32_t i = 0; i < trackCount; ++i) {
        gfcNetTrackStateInfo t;
        if (!(r.readI32(t.frameOffset) && r.readI32(t.holdMode) &&
              r.readI32(t.holdFrame)))
            return false;
        info.trackStateInfo.push_back(t);
    }
    return true;
}

// ---------------------------------------------------------------------
// FXAdd
// Legacy pair: gfcnetworkclient.cpp SendFXAddMessage / GFCNETID_FXADDMESSAGE.
// ---------------------------------------------------------------------
void encodeFXAdd(Writer& w, const gfcNetFXAddInfo& info) {
    w.writeI32(info.id.quadID);
    w.writeString(info.id.hash);
}

bool decodeFXAdd(Reader& r, gfcNetFXAddInfo& info) {
    return r.readI32(info.id.quadID) && r.readString(info.id.hash);
}

// ---------------------------------------------------------------------
// FXCommon
// Legacy pair: gfcnetworkclient.cpp SendFXCommonMessage / GFCNETID_FXCOMMONMESSAGE.
// ---------------------------------------------------------------------
void encodeFXCommon(Writer& w, const gfcNetFXCommonInfo& info) {
    w.writeI32(info.id.index);
    w.writeI32(info.id.quadID);
    w.writeI32(info.onOff);
    w.writeI32(info.upDown);
    w.writeBool(info.reset);
    w.writeBool(info.remove);
}

bool decodeFXCommon(Reader& r, gfcNetFXCommonInfo& info) {
    return r.readI32(info.id.index) && r.readI32(info.id.quadID) &&
           r.readI32(info.onOff) && r.readI32(info.upDown) &&
           r.readBool(info.reset) && r.readBool(info.remove);
}

// ---------------------------------------------------------------------
// FXAttrib (lutOrCube comes FIRST of the strings)
// Legacy pair: gfcnetworkclient.cpp SendFXAttribMessage / GFCNETID_FXATTRIBMESSAGE.
// ---------------------------------------------------------------------
void encodeFXAttrib(Writer& w, const gfcNetFXAttribInfo& info) {
    w.writeI32(info.id.index);
    w.writeI32(info.id.quadID);
    w.writeU8(info.attribType);
    w.writeI32(info.theInt);
    w.writeF32(info.theFloat);
    w.writeString(info.lutOrCube);
    w.writeString(info.groupName);
    w.writeString(info.variableName);
}

bool decodeFXAttrib(Reader& r, gfcNetFXAttribInfo& info) {
    return r.readI32(info.id.index) && r.readI32(info.id.quadID) &&
           r.readU8(info.attribType) && r.readI32(info.theInt) &&
           r.readF32(info.theFloat) && r.readString(info.lutOrCube) &&
           r.readString(info.groupName) && r.readString(info.variableName);
}

// ---------------------------------------------------------------------
// FXStack (the explicit legacy length field is dropped; writeString carries it)
// Legacy pair: gfcnetworkclient.cpp SendFXStackMessage / GFCNETID_FXSTACKMESSAGE.
// ---------------------------------------------------------------------
void encodeFXStack(Writer& w, const gfcNetFXStackMessage& msg) {
    w.writeI32(msg.quadID);
    w.writeString(msg.theStack);
}

bool decodeFXStack(Reader& r, gfcNetFXStackMessage& msg) {
    return r.readI32(msg.quadID) && r.readString(msg.theStack);
}

// ---------------------------------------------------------------------
// LayerChange
// Legacy pair: gfcnetworkclient.cpp SendLayerChangeMessage / GFCNETID_LAYERCHANGEMESSAGE.
// ---------------------------------------------------------------------
void encodeLayerChange(Writer& w, int quadID, const std::string& layerName) {
    w.writeI32(quadID);
    w.writeString(layerName);
}

bool decodeLayerChange(Reader& r, int& quadID, std::string& layerName) {
    int32_t q = 0;
    if (!(r.readI32(q) && r.readString(layerName))) return false;
    quadID = q;
    return true;
}

// ---------------------------------------------------------------------
// ChatEntry
// Legacy pair: gfcnetworkserver.cpp sendChatMessage / GFCNETID_CHATBROADCASTMESSAGE.
// ---------------------------------------------------------------------
void encodeChatEntry(Writer& w, const gfcChatLogEntry& entry) {
    w.writeU8(entry.type);
    w.writeString(entry.time);
    w.writeString(entry.sender);
    w.writeString(entry.message);
    w.writeI32(entry.color);
}

bool decodeChatEntry(Reader& r, gfcChatLogEntry& entry) {
    return r.readU8(entry.type) && r.readString(entry.time) &&
           r.readString(entry.sender) && r.readString(entry.message) &&
           r.readI32(entry.color);
}

// ---------------------------------------------------------------------
// PlaylistMessage (the explicit legacy length field is dropped; writeString carries it)
// Legacy pair: gfcnetworkclient.cpp SendPlaylistMessage / GFCNETID_SENDPLAYLIST.
// ---------------------------------------------------------------------
void encodePlaylistMessage(Writer& w, const gfcNetPlaylistMessage& msg) {
    w.writeString(msg.thePlaylist);
}

bool decodePlaylistMessage(Reader& r, gfcNetPlaylistMessage& msg) {
    return r.readString(msg.thePlaylist);
}

// ---------------------------------------------------------------------
// PlaylistItem (item.asString() as one compressed string)
// Legacy pair: gfcnetworkclient.cpp SendPlaylistItem / GFCNETID_PLAYLISTITEMLOADMESSAGE.
// ---------------------------------------------------------------------
void encodePlaylistItem(Writer& w, const std::string& itemXml) {
    w.writeString(itemXml);
}

bool decodePlaylistItem(Reader& r, std::string& itemXml) {
    return r.readString(itemXml);
}

// ---------------------------------------------------------------------
// PlaylistEvent
// Legacy pair: gfcnetworkclient.cpp sendPlaylistEvent / GFCNETID_PLAYLISTEVENTOTHER.
// ---------------------------------------------------------------------
void encodePlaylistEvent(Writer& w, const gfcNetPlaylistEvent& ev) {
    w.writeI32(ev.selectedItem);
}

bool decodePlaylistEvent(Reader& r, gfcNetPlaylistEvent& ev) {
    return r.readI32(ev.selectedItem);
}

}  // namespace wire
}  // namespace jefe
