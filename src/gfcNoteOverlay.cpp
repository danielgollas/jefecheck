#include <glad/glad.h>

#include "gfcNoteOverlay.h"

#include "gfcTextRenderer.h"
#include "gfcnotetext.h"

#include <cmath>
#include <cstdio>
#include <vector>

// ---------------------------------------------------------------------------
// Note overlay
//
// Notes are stored normalised (0..1). draw() maps them onto a rectangle given
// in the caller's current GL coordinates and strokes them with immediate-mode
// lines. It is deliberately dumb about where that rectangle came from: on
// screen it is the plate's transformed quad (so notes track pan and zoom), at
// export time it is the FBO's ortho rect. One mapping, two call sites.
//
// Two things here are load-bearing rather than stylistic:
//
//  * The active shader program is saved and cleared for the duration. The
//    caller reaches us with the super-shader (or an FX pass) still bound, and
//    lines pushed through a colour-correction shader would come out the wrong
//    colour or not at all. GfcTextRenderer::drawLine does exactly this before
//    its own quads; this matches it. ARB entry points only — on macOS
//    GLhandleARB is void*, not GLuint, and mixing the two families breaks.
//
//  * Every note is stroked twice: a dark outline at size + 2, then the note's
//    own colour at size. Without the outline a red note is invisible on a red
//    frame, which is exactly the frame someone is most likely to draw a red
//    circle on.
//
// Antialiasing is left alone. Consistent-width AA needs triangle-strip
// expansion; the spec records that as a known quality limit and puts it out of
// scope.
// ---------------------------------------------------------------------------

namespace
{
	// Arrow head proportions, in the mapped (post-Rect) space so the head
	// stays visually consistent whatever the plate is scaled to.
	const float kArrowHeadFraction = 0.18f;   // of the shaft length
	const float kArrowHeadMin      = 8.0f;    // ...but never a nub
	const float kArrowHeadMax      = 60.0f;   // ...nor a second arrow
	const float kArrowHeadAngle    = 0.4363f; // 25 degrees, radians

	const float kOutlineExtra      = 2.0f;    // outline is size + 2, per spec
	const float kOutlineAlpha      = 0.85f;

	typedef std::vector<gfcNotePoint> Polyline;

	// Clamps a line width to what the driver will actually honour. macOS's GL
	// caps aliased line width well below the "size + 2" a user could ask for,
	// and an out-of-range glLineWidth is a GL_INVALID_VALUE that silently
	// leaves the previous width in place — which would make the outline pass
	// and the colour pass identical, i.e. no outline at all.
	float clampLineWidth(float w)
	{
		GLfloat range[2] = { 1.0f, 1.0f };
		glGetFloatv(GL_ALIASED_LINE_WIDTH_RANGE, range);
		if (range[1] < range[0])
		{
			range[1] = range[0];
		}
		if (w < range[0]) { return range[0]; }
		if (w > range[1]) { return range[1]; }
		return w;
	}

	// Builds the polylines for one note, already mapped into target space.
	// Text notes produce none — they go through gfc_gl_draw instead.
	void buildPolylines(const gfcNote& n,
	                    const gfcNoteOverlay::Rect& target,
	                    std::vector<Polyline>& out)
	{
		const std::vector<gfcNotePoint> pts = n.points();

		switch (n.noteType())
		{
			case GFCNOTE_STROKE:
			{
				if (pts.empty())
				{
					return;
				}
				Polyline line;
				line.reserve(pts.size());
				for (size_t i = 0; i < pts.size(); ++i)
				{
					line.push_back(gfcNoteOverlay::mapPoint(pts[i], target));
				}
				out.push_back(line);
				break;
			}

			case GFCNOTE_BOX:
			{
				if (pts.size() < 2)
				{
					return;
				}
				// points() hands back the two opposite corners; close the
				// rectangle here so one GL_LINE_STRIP draws all four sides.
				const gfcNotePoint a = gfcNoteOverlay::mapPoint(pts[0], target);
				const gfcNotePoint b = gfcNoteOverlay::mapPoint(pts[1], target);
				Polyline line;
				line.reserve(5);
				line.push_back(a);
				line.push_back(gfcNotePoint{ b.x, a.y });
				line.push_back(b);
				line.push_back(gfcNotePoint{ a.x, b.y });
				line.push_back(a);
				out.push_back(line);
				break;
			}

			case GFCNOTE_ARROW:
			{
				if (pts.size() < 2)
				{
					return;
				}
				const gfcNotePoint tail = gfcNoteOverlay::mapPoint(pts[0], target);
				const gfcNotePoint head = gfcNoteOverlay::mapPoint(pts[1], target);

				Polyline shaft;
				shaft.push_back(tail);
				shaft.push_back(head);
				out.push_back(shaft);

				// Head barbs: rotate the head->tail direction by +/- 25
				// degrees. Computed after mapping so a squashed target rect
				// squashes the head the same way it squashes the shaft.
				const float dx = tail.x - head.x;
				const float dy = tail.y - head.y;
				const float len = std::sqrt(dx * dx + dy * dy);
				if (len <= 0.0f)
				{
					return;
				}
				const float ux = dx / len;
				const float uy = dy / len;

				float barb = len * kArrowHeadFraction;
				if (barb < kArrowHeadMin) { barb = kArrowHeadMin; }
				if (barb > kArrowHeadMax) { barb = kArrowHeadMax; }
				if (barb > len)           { barb = len; }

				const float c = std::cos(kArrowHeadAngle);
				const float s = std::sin(kArrowHeadAngle);

				Polyline left;
				left.push_back(head);
				left.push_back(gfcNotePoint{ head.x + barb * (ux * c - uy * s),
				                             head.y + barb * (ux * s + uy * c) });
				out.push_back(left);

				Polyline right;
				right.push_back(head);
				right.push_back(gfcNotePoint{ head.x + barb * (ux * c + uy * s),
				                              head.y + barb * (-ux * s + uy * c) });
				out.push_back(right);
				break;
			}

			case GFCNOTE_TEXT:
			default:
				break;
		}
	}

	// Strokes one pass over the geometry at the given width and colour.
	void strokePass(const std::vector<Polyline>& lines,
	                float width, float r, float g, float b, float a)
	{
		glLineWidth(clampLineWidth(width));
		glPointSize(clampLineWidth(width));
		glColor4f(r, g, b, a);

		for (size_t i = 0; i < lines.size(); ++i)
		{
			const Polyline& line = lines[i];
			if (line.empty())
			{
				continue;
			}
			// A freehand note that is a single click has nothing for
			// GL_LINE_STRIP to connect and would silently vanish; draw the
			// dot instead.
			glBegin(line.size() == 1 ? GL_POINTS : GL_LINE_STRIP);
			for (size_t p = 0; p < line.size(); ++p)
			{
				glVertex2f(line[p].x, line[p].y);
			}
			glEnd();
		}
	}
}  // namespace

gfcNotePoint gfcNoteOverlay::mapPoint(const gfcNotePoint& p, const Rect& target)
{
	gfcNotePoint out;
	out.x = target.x + p.x * target.w;
	out.y = target.y + p.y * target.h;
	return out;
}

void gfcNoteOverlay::draw(const std::vector<const gfcNote*>& notes,
                          int frame, int quadID, const Rect& target)
{
	if (notes.empty())
	{
		return;
	}

	// Collect first, so a list with nothing visible on this frame costs no GL
	// state churn at all.
	std::vector<const gfcNote*> visible;
	for (size_t i = 0; i < notes.size(); ++i)
	{
		const gfcNote* n = notes[i];
		if (n && n->quadID == quadID && n->visibleOnFrame(frame))
		{
			visible.push_back(n);
		}
	}
	if (visible.empty())
	{
		return;
	}

	// --- state save -------------------------------------------------------
	// glPushAttrib(GL_ALL_ATTRIB_BITS) covers everything below: the enable
	// bits (blend, depth test, both texture targets), the blend function, the
	// line and point widths, and the current colour. The shader program is
	// NOT part of the attribute stack, so it is saved by hand.
	glPushAttrib(GL_ALL_ATTRIB_BITS);

	GLhandleARB prevProgram = glGetHandleARB(GL_PROGRAM_OBJECT_ARB);
	if (prevProgram)
	{
		glUseProgramObjectARB(0);
	}

	// The plate leaves a texture bound and enabled from the composite pass;
	// leaving it on would modulate the lines by whatever pixel the texture
	// coordinate happens to land on.
	glDisable(GL_TEXTURE_2D);
	glDisable(GL_TEXTURE_RECTANGLE_ARB);
	glDisable(GL_DEPTH_TEST);
	glEnable(GL_BLEND);
	// Colour: ordinary "over". Alpha: ONE, ONE_MINUS_SRC_ALPHA -- not SRC_ALPHA.
	// With plain glBlendFunc the destination alpha comes out a*a over a
	// transparent buffer, which is wrong for the rasterised notes layer, and
	// 1 - a + a*a over an opaque screen, which quietly punches alpha holes along
	// every outline edge. Separate factors leave the colour identical and make
	// the alpha correct in both places.
	glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA,
	                    GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

	for (size_t i = 0; i < visible.size(); ++i)
	{
		const gfcNote& n = *visible[i];

		float size = (float)n.size;
		if (size < 1.0f)
		{
			size = 1.0f;
		}

		if (n.noteType() == GFCNOTE_TEXT)
		{
			// One text path only: the existing renderer, which already does
			// its own shadow pass for legibility and its own state save. It
			// projects the point we hand it from the current matrices, so the
			// mapped position is what it wants.
			const gfcNoteText* t = dynamic_cast<const gfcNoteText*>(&n);
			if (t && !t->text.empty())
			{
				const gfcNotePoint at = mapPoint(t->anchor, target);
				textRenderer().setColor(n.colorR, n.colorG, n.colorB, 1.0f);
				gfc_gl_draw(t->text.c_str(), at.x, at.y);
			}
			continue;
		}

		std::vector<Polyline> lines;
		buildPolylines(n, target, lines);
		if (lines.empty())
		{
			continue;
		}

		// Pass 1: dark outline, thicker. Pass 2: the note's colour on top.
		strokePass(lines, size + kOutlineExtra, 0.0f, 0.0f, 0.0f, kOutlineAlpha);
		strokePass(lines, size, n.colorR, n.colorG, n.colorB, 1.0f);
	}

	// --- state restore ----------------------------------------------------
	if (prevProgram)
	{
		glUseProgramObjectARB(prevProgram);
	}
	glPopAttrib();
}

bool gfcNoteOverlay::rasterise(const std::vector<const gfcNote*>& notes,
                               int frame, int quadID, int width, int height,
                               std::vector<unsigned char>& rgba, std::string* err)
{
	auto fail = [&](const std::string& why)
	{
		if (err)
		{
			*err = why;
		}
		return false;
	};

	if (width <= 0 || height <= 0)
	{
		return fail("rasterise: image has no size");
	}

	GLint maxRb = 0;
	glGetIntegerv(GL_MAX_RENDERBUFFER_SIZE_EXT, &maxRb);
	if (width > maxRb || height > maxRb)
	{
		return fail("rasterise: " + std::to_string(width) + "x" + std::to_string(height) +
		            " exceeds this GPU's renderbuffer limit of " + std::to_string(maxRb));
	}

	// The viewport renders into QOpenGLWidget's own framebuffer rather than 0,
	// so the binding is saved and put back instead of assumed.
	GLint prevFbo = 0;
	glGetIntegerv(GL_FRAMEBUFFER_BINDING_EXT, &prevFbo);

	GLuint fbo = 0;
	GLuint rb = 0;
	glGenFramebuffersEXT(1, &fbo);
	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, fbo);
	glGenRenderbuffersEXT(1, &rb);
	glBindRenderbufferEXT(GL_RENDERBUFFER_EXT, rb);
	glRenderbufferStorageEXT(GL_RENDERBUFFER_EXT, GL_RGBA8, width, height);
	glFramebufferRenderbufferEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT,
	                             GL_RENDERBUFFER_EXT, rb);

	const GLenum status = glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT);
	if (status != GL_FRAMEBUFFER_COMPLETE_EXT)
	{
		glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, (GLuint)prevFbo);
		glDeleteRenderbuffersEXT(1, &rb);
		glDeleteFramebuffersEXT(1, &fbo);
		return fail("rasterise: offscreen framebuffer incomplete (status " +
		            std::to_string((unsigned)status) + ")");
	}

	glPushAttrib(GL_VIEWPORT_BIT | GL_COLOR_BUFFER_BIT | GL_TRANSFORM_BIT);
	glPushClientAttrib(GL_CLIENT_PIXEL_STORE_BIT);

	glViewport(0, 0, width, height);
	glMatrixMode(GL_PROJECTION);
	glPushMatrix();
	glLoadIdentity();
	glOrtho(0.0, (double)width, 0.0, (double)height, -1.0, 1.0);
	glMatrixMode(GL_MODELVIEW);
	glPushMatrix();
	glLoadIdentity();

	glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
	glClear(GL_COLOR_BUFFER_BIT);

	// Notes are y-DOWN normalised and this ortho is y-UP. An origin on the top
	// edge with a negative height is the flip -- the same convention gfcPlate
	// uses for both the screen and the export composite.
	Rect target;
	target.x = 0.0f;
	target.y = (float)height;
	target.w = (float)width;
	target.h = -(float)height;
	draw(notes, frame, quadID, target);

	rgba.assign((size_t)width * (size_t)height * 4, 0);
	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());

	glMatrixMode(GL_MODELVIEW);
	glPopMatrix();
	glMatrixMode(GL_PROJECTION);
	glPopMatrix();
	glPopClientAttrib();
	glPopAttrib();

	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, (GLuint)prevFbo);
	glDeleteRenderbuffersEXT(1, &rb);
	glDeleteFramebuffersEXT(1, &fbo);

	// GL reads the bottom row first; EXR and the stamp want the top row first.
	const size_t row = (size_t)width * 4;
	for (int y = 0; y < height / 2; ++y)
	{
		unsigned char* top = &rgba[(size_t)y * row];
		unsigned char* bot = &rgba[(size_t)(height - 1 - y) * row];
		for (size_t x = 0; x < row; ++x)
		{
			const unsigned char t = top[x];
			top[x] = bot[x];
			bot[x] = t;
		}
	}
	return true;
}

// ---------------------------------------------------------------------------
// Self-test
//
// Geometry only. There is no headless GL context available here, so this
// checks the one piece that decides where every note lands — mapPoint — and
// leaves pixel proof to the render test, which runs with a real context.
// ---------------------------------------------------------------------------

int noteOverlaySelfTest()
{
	int pass = 0;
	int fail = 0;
	auto check = [&](bool cond, const char* msg)
	{
		if (cond)
		{
			++pass;
		}
		else
		{
			++fail;
			std::fprintf(stderr, "NOTE-OVERLAY FAIL: %s\n", msg);
		}
	};

	gfcNotePoint mapped = gfcNoteOverlay::mapPoint({0.5f, 0.5f}, {0, 0, 100, 200});
	check(mapped.x == 50.0f && mapped.y == 100.0f, "centre maps to the rect centre");

	mapped = gfcNoteOverlay::mapPoint({0.0f, 0.0f}, {10, 20, 100, 200});
	check(mapped.x == 10.0f && mapped.y == 20.0f, "origin maps to the rect origin");

	mapped = gfcNoteOverlay::mapPoint({1.0f, 1.0f}, {10, 20, 100, 200});
	check(mapped.x == 110.0f && mapped.y == 220.0f, "1,1 maps to the far corner");

	// A negative extent is how a caller expresses a flipped plate; the
	// mapping has to stay affine through it rather than clamping.
	mapped = gfcNoteOverlay::mapPoint({0.25f, 0.5f}, {100, 0, -100, 200});
	check(mapped.x == 75.0f && mapped.y == 100.0f, "a negative extent flips the axis");

	// Anisotropic target: x and y scale independently, which is what keeps a
	// note stuck to the image when the plate is not square.
	mapped = gfcNoteOverlay::mapPoint({0.5f, 0.25f}, {0, 0, 1920, 1080});
	check(mapped.x == 960.0f && mapped.y == 270.0f, "the two axes scale independently");

	std::printf("NOTE-OVERLAY: pass=%d fail=%d\n", pass, fail);
	return fail == 0 ? 0 : 1;
}
