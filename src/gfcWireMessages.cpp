// jefe::wire message codecs (JEF-23). See gfcWireMessages.h for the codec
// contract; each codec's field sequence cites its legacy BitStream pair.

#include "gfcWireMessages.h"

namespace jefe {
namespace wire {

// ---------------------------------------------------------------------
// PlayPause
// Legacy write: gfcnetworkclient.cpp SendPlayPauseMessage
//   (Write play, WriteCompressed frame, WriteCompressed direction).
// Legacy read: gfcnetworkclient.cpp GFCNETID_PLAYPAUSEMESSAGE handler
//   (Read play, ReadCompressed frame, ReadCompressed direction).
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
// PointerInfo (client -> server)
// Legacy write: gfcnetworkclient.cpp SendPointerInfoMessage
//   (quadID, x, y compressed ints; scale float — no color, no nickname).
// Legacy read: gfcnetworkserver.cpp GFCNETID_POINTERINFOMESSAGE handler
//   (same four fields; server appends nickname + color for the broadcast).
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
// Legacy write: gfcnetworkserver.cpp GFCNETID_POINTERINFOMESSAGE handler
//   (quadID, x, y, scale, nickname, color).
// Legacy read: gfcnetworkclient.cpp GFCNETID_POINTERINFOBROADCASTMESSAGE
//   handler (same order into gfcNetRemotePointerInfo).
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
// Legacy write: gfcnetworkclient.cpp SendTransformations
//   (WriteCompressed count; per element tX, tY, scale, rZ floats).
// Legacy read: gfcnetworkclient.cpp GFCNETID_TRANSFORMATIONMESSAGE handler
//   (ReadCompressed count; per element same four floats).
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
// Legacy write: gfcnetworkclient.cpp SendColorCorrections
//   (count; per element quadID, lutName, gamma, exposure, brightness,
//    contrast, saturation).
// Legacy read: gfcnetworkclient.cpp GFCNETID_COLORCORRECTIONMESSAGE handler
//   (same order).
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
// Legacy write: gfcnetworkclient.cpp SendOtherStatesMessage
//   (playbackInfo.from, .to, .targetFPS, .playbackMode, .loopPriority,
//    .inPoint, .outPoint; layout; plate vector count then per element
//    track (u8), quadID, flip, flop, a, r, g, b, aspect, crop; track
//    vector count then per element frameOffset, holdMode, holdFrame).
// Legacy read: gfcnetworkclient.cpp GFCNETID_OTHERSTATESMESSAGE handler
//   (same order).
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
// Legacy write: gfcnetworkclient.cpp SendFXAddMessage (quadID, hash).
// Legacy read: gfcnetworkclient.cpp GFCNETID_FXADDMESSAGE handler (same).
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
// Legacy write: gfcnetworkclient.cpp SendFXCommonMessage
//   (id.index, id.quadID, onOff, upDown, reset, remove).
// Legacy read: gfcnetworkclient.cpp GFCNETID_FXCOMMONMESSAGE handler (same).
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
// FXAttrib
// Legacy write: gfcnetworkclient.cpp SendFXAttribMessage
//   (id.index, id.quadID, attribType (u8), theInt, theFloat, lutOrCube,
//    groupName, variableName — note lutOrCube comes FIRST of the strings).
// Legacy read: gfcnetworkclient.cpp GFCNETID_FXATTRIBMESSAGE handler (same).
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
// FXStack
// Legacy write: gfcnetworkclient.cpp SendFXStackMessage
//   (quadID; explicit length+5; compressed stack string).
// Legacy read: gfcnetworkclient.cpp GFCNETID_FXSTACKMESSAGE handler.
// The explicit length field is dropped — writeString carries the length.
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
// Legacy write: gfcnetworkclient.cpp SendLayerChangeMessage
//   (quadID, layerName).
// Legacy read: gfcnetworkclient.cpp GFCNETID_LAYERCHANGEMESSAGE handler
//   (same).
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
// Legacy write: gfcnetworkserver.cpp sendChatMessage
//   (type (u8), time, sender, message, color).
// Legacy read: gfcnetworkclient.cpp GFCNETID_CHATBROADCASTMESSAGE handler
//   (same order into gfcChatLogEntry).
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
// PlaylistMessage
// Legacy write: gfcnetworkclient.cpp SendPlaylistMessage
//   (explicit length+5; compressed playlist string). Explicit length
//   dropped — writeString carries it.
// Legacy read: gfcnetworkclient.cpp GFCNETID_SENDPLAYLIST handler.
// ---------------------------------------------------------------------
void encodePlaylistMessage(Writer& w, const gfcNetPlaylistMessage& msg) {
    w.writeString(msg.thePlaylist);
}

bool decodePlaylistMessage(Reader& r, gfcNetPlaylistMessage& msg) {
    return r.readString(msg.thePlaylist);
}

// ---------------------------------------------------------------------
// PlaylistItem
// Legacy write: gfcnetworkclient.cpp SendPlaylistItem
//   (item.asString() as a single compressed string).
// Legacy read: gfcnetworkclient.cpp GFCNETID_PLAYLISTITEMLOADMESSAGE
//   handler (decodes the string, XML-parses into gfcPlaylistItem).
// ---------------------------------------------------------------------
void encodePlaylistItem(Writer& w, const std::string& itemXml) {
    w.writeString(itemXml);
}

bool decodePlaylistItem(Reader& r, std::string& itemXml) {
    return r.readString(itemXml);
}

// ---------------------------------------------------------------------
// PlaylistEvent
// Legacy write: gfcnetworkclient.cpp sendPlaylistEvent (selectedItem).
// Legacy read: gfcnetworkclient.cpp GFCNETID_PLAYLISTEVENTOTHER handler
//   (same).
// ---------------------------------------------------------------------
void encodePlaylistEvent(Writer& w, const gfcNetPlaylistEvent& ev) {
    w.writeI32(ev.selectedItem);
}

bool decodePlaylistEvent(Reader& r, gfcNetPlaylistEvent& ev) {
    return r.readI32(ev.selectedItem);
}

}  // namespace wire
}  // namespace jefe
