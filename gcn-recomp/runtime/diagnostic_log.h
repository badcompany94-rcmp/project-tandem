#pragma once
#include <cstdarg>
#include <cstdio>
#include <vector>

// MinGW's formatted writer can issue a separate pipe write for every character
// on unbuffered stderr. PowerShell's normal process redirection turns a timing
// summary into tens of milliseconds of blocking work. Format in memory, then
// let stdio emit and lock the whole record with one fwrite. Logs remain immediate
// and keep their normal destination; no background queue can lose a fatal tail.
inline int diagnostic_log(const char* format,...) {
    char local[1024];
    va_list args,copy;va_start(args,format);va_copy(copy,args);
    const int size=std::vsnprintf(local,sizeof(local),format,copy);va_end(copy);
    if(size<0){va_end(args);return size;}
    if(size<int(sizeof(local))) {
        va_end(args);
        return std::fwrite(local,1,size,stderr)==size_t(size)?size:-1;
    }
    std::vector<char> large(size_t(size)+1);
    const int written=std::vsnprintf(large.data(),large.size(),format,args);va_end(args);
    if(written!=size)return -1;
    return std::fwrite(large.data(),1,size,stderr)==size_t(size)?size:-1;
}
