#include "gfcNoteStamp.h"

#include "gfcNoteStore.h"
#include "gfcnote.h"
#include "gfcnotestroke.h"
#include "gfcreview.h"
#include "gfcrevision.h"

#include <OpenImageIO/imageio.h>

#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace
{
	const char* kNotesAttr = "jefecheck:notes";

	void setErr(std::string* err, const std::string& msg)
	{
		if (err) *err = msg;
	}
}

bool gfcNoteStamp::stamp(const std::string& inExr, const std::string& outExr,
                         const gfcReview& review, const Options& opt,
                         std::string* err)
{
	if (inExr == outExr)
	{
		// Refused rather than handled. Writing over the source means a failure
		// half way through destroys the only copy, and the whole point of a
		// stamp is that the original pixels survive.
		setErr(err, "stamp: input and output must differ");
		return false;
	}

	auto in = OIIO::ImageInput::open(inExr);
	if (!in)
	{
		setErr(err, "stamp: cannot open " + inExr + ": " + OIIO::geterror());
		return false;
	}

	const OIIO::ImageSpec& src = in->spec();
	const int w = src.width;
	const int h = src.height;
	const int srcChannels = src.nchannels;

	// Read the source as float. EXR is float or half on disk, and going
	// through float keeps a half source lossless rather than quantising it on
	// the way past -- this must not alter the beauty.
	std::vector<float> pixels((size_t)w * h * srcChannels);
	if (!in->read_image(0, 0, 0, srcChannels, OIIO::TypeDesc::FLOAT, pixels.data()))
	{
		setErr(err, "stamp: read failed: " + in->geterror());
		in->close();
		return false;
	}
	in->close();

	const bool wantLayer = !opt.layerRGBA.empty();
	if (wantLayer &&
	    (opt.layerWidth != w || opt.layerHeight != h ||
	     opt.layerRGBA.size() != (size_t)w * h * 4))
	{
		setErr(err, "stamp: notes layer does not match the image size");
		return false;
	}

	const int outChannels = srcChannels + (wantLayer ? 4 : 0);

	OIIO::ImageSpec outSpec = src;          // carries every source attribute through
	outSpec.nchannels = outChannels;

	// Channel names: keep the source's, then append the notes layer. OIIO and
	// OpenEXR both treat a dotted prefix as a layer, which is exactly how
	// gfcImageLoaderOIIO already discovers layers on the way back IN -- so a
	// frame stamped here reads back as a selectable "notes" layer.
	outSpec.channelnames = src.channelnames;
	if ((int)outSpec.channelnames.size() < srcChannels)
	{
		outSpec.channelnames.clear();
		for (int c = 0; c < srcChannels; ++c)
		{
			static const char* kRGBA[4] = {"R", "G", "B", "A"};
			outSpec.channelnames.push_back(c < 4 ? kRGBA[c] : ("ch" + std::to_string(c)));
		}
	}
	if (wantLayer)
	{
		outSpec.channelnames.push_back("notes.R");
		outSpec.channelnames.push_back("notes.G");
		outSpec.channelnames.push_back("notes.B");
		outSpec.channelnames.push_back("notes.A");
		outSpec.alpha_channel = src.alpha_channel;   // still the SOURCE's alpha
	}

	if (opt.writeHeader)
	{
		const std::string json = gfcNoteStore::toJsonString(review);
		if (!json.empty())
		{
			outSpec.attribute(kNotesAttr, json);
		}
	}

	auto out = OIIO::ImageOutput::create(outExr);
	if (!out)
	{
		setErr(err, "stamp: no writer for " + outExr + ": " + OIIO::geterror());
		return false;
	}
	if (!out->open(outExr, outSpec))
	{
		setErr(err, "stamp: cannot open output: " + out->geterror());
		return false;
	}

	const float* writeFrom = pixels.data();
	std::vector<float> merged;
	if (wantLayer)
	{
		merged.resize((size_t)w * h * outChannels);
		for (size_t p = 0; p < (size_t)w * h; ++p)
		{
			float* dst = &merged[p * outChannels];
			std::memcpy(dst, &pixels[p * srcChannels], srcChannels * sizeof(float));
			for (int c = 0; c < 4; ++c)
			{
				// 8-bit markup into float: the layer is UI-coloured, not scene
				// linear, so a straight /255 is the honest conversion.
				dst[srcChannels + c] = opt.layerRGBA[p * 4 + c] / 255.0f;
			}
		}
		writeFrom = merged.data();
	}

	if (!out->write_image(OIIO::TypeDesc::FLOAT, writeFrom))
	{
		setErr(err, "stamp: write failed: " + out->geterror());
		out->close();
		return false;
	}
	out->close();
	return true;
}

std::string gfcNoteStamp::readEmbeddedJson(const std::string& exrPath)
{
	auto in = OIIO::ImageInput::open(exrPath);
	if (!in) return std::string();
	const OIIO::ImageSpec& spec = in->spec();
	const OIIO::ParamValue* p = spec.find_attribute(kNotesAttr, OIIO::TypeDesc::STRING);
	std::string out;
	if (p) out = *(const char**)p->data();
	in->close();
	return out;
}

// ---------------------------------------------------------------------------
// Self-test
// ---------------------------------------------------------------------------

namespace
{
	int g_pass = 0, g_fail = 0;
	void check(bool cond, const char* what)
	{
		if (cond) ++g_pass;
		else { ++g_fail; std::printf("  FAIL: %s\n", what); }
	}
}

int noteStampSelfTest()
{
	g_pass = g_fail = 0;

	const std::string dir = "/tmp/jefecheck_stamptest";
	std::string mk = "mkdir -p " + dir;
	(void)!system(mk.c_str());
	const std::string srcPath = dir + "/src.exr";
	const std::string outPath = dir + "/stamped.exr";
	std::remove(outPath.c_str());

	// A tiny source frame with a known value, so "unchanged" is checkable.
	const int w = 8, h = 4;
	{
		OIIO::ImageSpec s(w, h, 3, OIIO::TypeDesc::HALF);
		auto o = OIIO::ImageOutput::create(srcPath);
		std::vector<float> px((size_t)w * h * 3, 0.25f);
		if (o && o->open(srcPath, s)) { o->write_image(OIIO::TypeDesc::FLOAT, px.data()); o->close(); }
	}

	gfcReview review;
	review.mediaPath = srcPath;
	gfcRevision& rev = review.beginRevision("selftest");
	auto stroke = std::make_unique<gfcNoteStroke>();
	stroke->pts = { {0.1f, 0.2f}, {0.8f, 0.9f} };
	stroke->always = true;
	stroke->author = "selftest";
	rev.addNote(std::move(stroke));

	gfcNoteStamp::Options opt;
	opt.writeHeader = true;
	opt.layerRGBA.assign((size_t)w * h * 4, 0);
	opt.layerRGBA[0] = 255;                     // one opaque red-ish texel
	opt.layerRGBA[3] = 255;
	opt.layerWidth = w;
	opt.layerHeight = h;

	std::string err;
	check(gfcNoteStamp::stamp(srcPath, outPath, review, opt, &err),
	      "stamping an existing EXR succeeds");

	const std::string json = gfcNoteStamp::readEmbeddedJson(outPath);
	check(!json.empty(), "the header attribute reads back");
	check(json.find("\"schema\":\"jefecheck.notes/1\"") != std::string::npos,
	      "the embedded blob declares its schema");
	check(json.find("\"points\"") != std::string::npos,
	      "geometry survives into the header");

	// The beauty must be untouched, and the notes layer must be addressable
	// by name rather than by position.
	{
		auto in = OIIO::ImageInput::open(outPath);
		check(in != nullptr, "the stamped file opens");
		if (in)
		{
			const OIIO::ImageSpec& s = in->spec();
			check(s.nchannels == 7, "3 source channels + 4 notes channels");
			bool named = false;
			for (const std::string& n : s.channelnames)
				if (n == "notes.A") named = true;
			check(named, "the notes layer is named, so a viewer can find it");

			std::vector<float> px((size_t)w * h * s.nchannels);
			in->read_image(0, 0, 0, s.nchannels, OIIO::TypeDesc::FLOAT, px.data());
			check(px[0] > 0.24f && px[0] < 0.26f, "source pixel value is unchanged");
			check(px[3] > 0.99f, "the notes layer carries what was handed in");
			in->close();
		}
	}

	std::string e2;
	check(!gfcNoteStamp::stamp(srcPath, srcPath, review, opt, &e2),
	      "refuses to stamp over its own source");

	std::printf("NOTE-STAMP: pass=%d fail=%d\n", g_pass, g_fail);
	fflush(stdout);
	return g_fail == 0 ? 0 : 1;
}
