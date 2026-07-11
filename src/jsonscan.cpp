#include "jsonscan.hpp"

#include <cstring>

std::string pkgi_json_str(const char* data, size_t len, const char* key)
{
    std::string pat = "\"";
    pat += key;
    pat += "\":\"";

    const char* found = nullptr;
    for (size_t i = 0; i + pat.size() <= len; ++i)
    {
        if (memcmp(data + i, pat.c_str(), pat.size()) == 0)
        {
            found = data + i + pat.size();
            break;
        }
    }
    if (!found)
        return "";

    const char* end = found;
    const char* limit = data + len;
    while (end < limit && *end != '"' && *end != '\0')
    {
        if (*end == '\\')
            end++;
        end++;
    }
    return std::string(found, end);
}

const char* pkgi_find_object_end(const char* ptr, const char* limit)
{
    if (!ptr || *ptr != '{')
        return nullptr;
    int depth = 0;
    bool in_string = false;
    while (ptr < limit)
    {
        char c = *ptr;
        if (in_string)
        {
            if (c == '\\')
                ptr++;
            else if (c == '"')
                in_string = false;
        }
        else
        {
            if (c == '"')
                in_string = true;
            else if (c == '{')
                depth++;
            else if (c == '}')
            {
                depth--;
                if (depth == 0)
                    return ptr;
            }
        }
        ptr++;
    }
    return nullptr;
}

std::string pkgi_json_unescape(const std::string& s)
{
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i)
    {
        if (s[i] == '\\' && i + 1 < s.size())
        {
            const char n = s[i + 1];
            if (n == '/' || n == '\\' || n == '"')
            {
                out += n;
                ++i;
                continue;
            }
        }
        out += s[i];
    }
    return out;
}

std::string pkgi_url_encode_path(const std::string& path)
{
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    out.reserve(path.size() * 2);
    for (unsigned char c : path)
    {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' ||
            c == '~' || c == '/')
        {
            out += static_cast<char>(c);
        }
        else
        {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 0x0F];
        }
    }
    return out;
}
