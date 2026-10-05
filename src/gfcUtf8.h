#ifndef GFCUTF8_H
#define GFCUTF8_H

#include <cstring>

/**
	@brief @a s past a leading UTF-8 byte-order mark (EF BB BF), or @a s itself
	when it has none. XMLNode::writeToFile writes a BOM (so every saved .jcs
	and .jnotes starts with one) and XMLNode::parseFile skips it, but
	XMLNode::parseString does not: parse any file bytes read into a string
	through this first. @a s must be NUL-terminated.
*/
inline const char* skipUtf8Bom(const char* s)
{
	return std::strncmp(s, "\xEF\xBB\xBF", 3) == 0 ? s + 3 : s;
}

#endif
