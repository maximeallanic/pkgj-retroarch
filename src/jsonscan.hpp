#pragma once

#include <cstddef>
#include <string>

// Value of a string field "key":"<value>" within [data, data+len). Empty if absent.
std::string pkgi_json_str(const char* data, size_t len, const char* key);

// Closing '}' matching an opening '{' at ptr[0]; nullptr on error/unbalanced.
const char* pkgi_find_object_end(const char* ptr, const char* limit);

// Minimal unescape for the escapes archive.org emits: \/  \\  \"
std::string pkgi_json_unescape(const std::string& s);

// Percent-encode a path; '/' kept; unreserved chars pass through.
std::string pkgi_url_encode_path(const std::string& path);
