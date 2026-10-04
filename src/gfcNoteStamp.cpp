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

	// When a spec has per-channel formats, that array must be exactly nchannels
	// long. Raising nchannels without extending it made the writer read formats
	// past the end of the array and misalign the conversion of every channel:
	// on a real CG render (mixed half RGBA + float Z) R and G came back as NaN
	// and 1.748, and the notes layer as 3.4e38. Keep each source channel's own
	// storage type, and store the notes channels in the source's primary format.
	if (!src.channelformats.empty())
	{
		outSpec.channelformats = src.channelformats;
		outSpec.channelformats.resize((size_t)srcChannels, src.format);
		if (wantLayer)
		{
			for (int c = 0; c < 4; ++c)
			{
				outSpec.channelformats.push_back(src.format);
			}
		}
	}

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

bool gfcNoteStamp::imageSize(const std::string& path, int* width, int* height)
{
	auto in = OIIO::ImageInput::open(path);
	if (!in) return false;
	if (width)  *width  = in->spec().width;
	if (height) *height = in->spec().height;
	in->close();
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

	// The source is deliberately MIXED-format: R, G, B, A stored as half and Z
	// as float, declared through per-channel channelformats -- the same shape
	// as a real CG render (openexr-images/ScanLines/Blobbies.exr). An earlier
	// version of this test used a uniform half source and passed while the
	// stamp corrupted every mixed-format EXR it touched. Values vary per pixel
	// and per channel so any channel shift or misalignment shows up, and all
	// are exactly representable in half, so the comparison can be tight.
	const int w = 8, h = 4;
	const int srcCh = 5;
	auto srcValue = [](int x, int y, int c) -> float
	{
		switch (c)
		{
			case 0: return 0.125f * (float)(x % 4);        // R
			case 1: return 0.5f;                            // G
			case 2: return 0.25f + 0.125f * (float)(y % 2); // B
			case 3: return 1.0f;                            // A
			default: return 10.0f + (float)y;               // Z, float
		}
	};
	{
		OIIO::ImageSpec s(w, h, srcCh, OIIO::TypeDesc::HALF);
		s.channelnames = { "R", "G", "B", "A", "Z" };
		s.channelformats = { OIIO::TypeDesc::HALF, OIIO::TypeDesc::HALF,
		                     OIIO::TypeDesc::HALF, OIIO::TypeDesc::HALF,
		                     OIIO::TypeDesc::FLOAT };
		s.alpha_channel = 3;
		s.z_channel = 4;
		std::vector<float> px((size_t)w * h * srcCh);
		for (int y = 0; y < h; ++y)
			for (int x = 0; x < w; ++x)
				for (int c = 0; c < srcCh; ++c)
					px[((size_t)y * w + x) * srcCh + c] = srcValue(x, y, c);
		auto o = OIIO::ImageOutput::create(srcPath);
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

	// One marked texel with four DIFFERENT channel values, so a swapped or
	// shifted notes channel cannot pass for a correct one.
	const int markX = 2, markY = 1;
	gfcNoteStamp::Options opt;
	opt.writeHeader = true;
	opt.layerRGBA.assign((size_t)w * h * 4, 0);
	{
		const size_t m = ((size_t)markY * w + markX) * 4;
		opt.layerRGBA[m + 0] = 255;
		opt.layerRGBA[m + 1] = 128;
		opt.layerRGBA[m + 2] = 64;
		opt.layerRGBA[m + 3] = 255;
	}
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

	// Every channel is looked up BY NAME. The EXR reader is free to reorder
	// channels, so a positional check can pass against the wrong channel.
	{
		auto in = OIIO::ImageInput::open(outPath);
		check(in != nullptr, "the stamped file opens");
		if (in)
		{
			const OIIO::ImageSpec& s = in->spec();
			check(s.nchannels == srcCh + 4, "5 source channels + 4 notes channels");

			auto idx = [&](const char* name) -> int
			{
				for (int c = 0; c < (int)s.channelnames.size(); ++c)
					if (s.channelnames[c] == name) return c;
				return -1;
			};
			const int srcIdx[5] = { idx("R"), idx("G"), idx("B"), idx("A"), idx("Z") };
			const int noteIdx[4] = { idx("notes.R"), idx("notes.G"), idx("notes.B"), idx("notes.A") };
			bool allNamed = true;
			for (int c = 0; c < 5; ++c) allNamed = allNamed && srcIdx[c] >= 0;
			for (int c = 0; c < 4; ++c) allNamed = allNamed && noteIdx[c] >= 0;
			check(allNamed, "every source channel and every notes channel is present by name");

			std::vector<float> px((size_t)w * h * s.nchannels);
			in->read_image(0, 0, 0, s.nchannels, OIIO::TypeDesc::FLOAT, px.data());
			in->close();

			if (allNamed)
			{
				bool beautyExact = true;
				bool layerSane = true;
				for (int y = 0; y < h; ++y)
				{
					for (int x = 0; x < w; ++x)
					{
						const float* p = &px[((size_t)y * w + x) * s.nchannels];
						for (int c = 0; c < 5; ++c)
						{
							const float d = p[srcIdx[c]] - srcValue(x, y, c);
							if (!(d > -1e-4f && d < 1e-4f)) beautyExact = false;
						}
						for (int c = 0; c < 4; ++c)
						{
							const float v = p[noteIdx[c]];
							if (!(v >= 0.0f && v <= 1.0f)) layerSane = false;
						}
					}
				}
				check(beautyExact, "every source channel of every pixel is unchanged (mixed half/float)");
				check(layerSane, "every notes value is finite and within 0..1");

				const float* m = &px[((size_t)markY * w + markX) * s.nchannels];
				auto near = [](float a, float b) { return a - b > -0.01f && a - b < 0.01f; };
				check(near(m[noteIdx[0]], 1.0f) && near(m[noteIdx[1]], 128.0f / 255.0f) &&
				      near(m[noteIdx[2]], 64.0f / 255.0f) && near(m[noteIdx[3]], 1.0f),
				      "the marked texel carries R, G, B and A in the right channels");
			}
		}
	}

	std::string e2;
	check(!gfcNoteStamp::stamp(srcPath, srcPath, review, opt, &e2),
	      "refuses to stamp over its own source");

	std::printf("NOTE-STAMP: pass=%d fail=%d\n", g_pass, g_fail);
	fflush(stdout);
	return g_fail == 0 ? 0 : 1;
}
