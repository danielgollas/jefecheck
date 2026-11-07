// SPDX-License-Identifier: GPL-2.0-or-later
// jefe::wire message codecs mirroring the legacy RakNet::BitStream field order. Encoders
// append payload only (callers write the header); a decoder returns false on a bad buffer.

#pragma once

#include <string>
#include <vector>

#include "gfcWire.h"
#include "gfcNetworkStructures.h"

namespace jefe {
namespace wire {

// GFCNETID_PLAYPAUSEMESSAGE — fields: play, frame, direction.
void encodePlayPause(Writer& w, const gfcNetPlayPauseInfo& info);
bool decodePlayPause(Reader& r, gfcNetPlayPauseInfo& info);

// GFCNETID_POINTERINFOMESSAGE (client -> server) — fields: quadID, x, y,
// scale. Legacy never put gfcNetPointerInfo::color on this wire message
// (the server assigns the broadcast color from its colorAddressMap), so
// decode leaves info.color untouched.
void encodePointerInfo(Writer& w, const gfcNetPointerInfo& info);
bool decodePointerInfo(Reader& r, gfcNetPointerInfo& info);

// GFCNETID_POINTERINFOBROADCASTMESSAGE (server -> clients) — fields:
// quadID, x, y, scale, name, color. fadeCounter/headFadeCounter are local
// receiver state, never on the wire (decode leaves them untouched).
void encodeRemotePointerInfo(Writer& w, const gfcNetRemotePointerInfo& info);
bool decodeRemotePointerInfo(Reader& r, gfcNetRemotePointerInfo& info);

// GFCNETID_TRANSFORMATIONMESSAGE — u32 count, then per element:
// tX, tY, scale, rZ.
void encodeTransformations(Writer& w,
                           const std::vector<gfcNetTransformationInfo>& v);
bool decodeTransformations(Reader& r,
                           std::vector<gfcNetTransformationInfo>& v);

// GFCNETID_COLORCORRECTIONMESSAGE — u32 count, then per element:
// quadID, lutName, gamma, exposure, brightness, contrast, saturation.
void encodeColorCorrections(
    Writer& w, const std::vector<gfcNetPlateColorCorrectionInfo>& v);
bool decodeColorCorrections(Reader& r,
                            std::vector<gfcNetPlateColorCorrectionInfo>& v);

// GFCNETID_OTHERSTATESMESSAGE — playbackInfo (from, to, targetFPS,
// playbackMode, loopPriority, inPoint, outPoint), layout, plate vector
// (per element: track, quadID, flip, flop, a, r, g, b, aspect, crop),
// track vector (per element: frameOffset, holdMode, holdFrame).
void encodeOtherStates(Writer& w, const gfcNetOtherStatesInfo& info);
bool decodeOtherStates(Reader& r, gfcNetOtherStatesInfo& info);

// GFCNETID_FXADDMESSAGE — fields: id.quadID, id.hash.
void encodeFXAdd(Writer& w, const gfcNetFXAddInfo& info);
bool decodeFXAdd(Reader& r, gfcNetFXAddInfo& info);

// GFCNETID_FXCOMMONMESSAGE — fields: id.index, id.quadID, onOff, upDown,
// reset, remove.
void encodeFXCommon(Writer& w, const gfcNetFXCommonInfo& info);
bool decodeFXCommon(Reader& r, gfcNetFXCommonInfo& info);

// GFCNETID_FXATTRIBMESSAGE — fields: id.index, id.quadID, attribType,
// theInt, theFloat, lutOrCube, groupName, variableName (legacy order:
// lutOrCube BEFORE groupName/variableName).
void encodeFXAttrib(Writer& w, const gfcNetFXAttribInfo& info);
bool decodeFXAttrib(Reader& r, gfcNetFXAttribInfo& info);

// GFCNETID_FXSTACKMESSAGE — fields: quadID, theStack.
void encodeFXStack(Writer& w, const gfcNetFXStackMessage& msg);
bool decodeFXStack(Reader& r, gfcNetFXStackMessage& msg);

// GFCNETID_LAYERCHANGEMESSAGE — fields: quadID, layerName.
void encodeLayerChange(Writer& w, int quadID, const std::string& layerName);
bool decodeLayerChange(Reader& r, int& quadID, std::string& layerName);

// GFCNETID_CHATBROADCASTMESSAGE — fields: type, time, sender, message,
// color (exactly the legacy sendChatMessage / CHATBROADCAST handler pair).
void encodeChatEntry(Writer& w, const gfcChatLogEntry& entry);
bool decodeChatEntry(Reader& r, gfcChatLogEntry& entry);

// GFCNETID_SENDPLAYLIST — field: thePlaylist (playlist XML as string).
void encodePlaylistMessage(Writer& w, const gfcNetPlaylistMessage& msg);
bool decodePlaylistMessage(Reader& r, gfcNetPlaylistMessage& msg);

// GFCNETID_PLAYLISTITEMLOADMESSAGE — field: the playlist item serialized
// as an XML string (gfcPlaylistItem::asString() on the send side; parsed
// back into a gfcPlaylistItem by the receive handler).
void encodePlaylistItem(Writer& w, const std::string& itemXml);
bool decodePlaylistItem(Reader& r, std::string& itemXml);

// GFCNETID_PLAYLISTEVENTOTHER — field: selectedItem.
void encodePlaylistEvent(Writer& w, const gfcNetPlaylistEvent& ev);
bool decodePlaylistEvent(Reader& r, gfcNetPlaylistEvent& ev);

}  // namespace wire
}  // namespace jefe
