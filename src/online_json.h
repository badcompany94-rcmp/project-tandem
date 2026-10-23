// Bounded JSON values for the local room protocol (integer numbers only).
#pragma once
#include <cstdint>
#include <map>
#include <vector>
#include <string>
#include <stdexcept>
#include <limits>

namespace mkdd::online {
struct Json {
    enum Kind { Null, Boolean, Number, String, Array, Object } kind=Null;
    bool flag=false;
    int64_t number=0;
    std::string string;
    std::vector<Json> array;
    std::map<std::string,Json> object;
    Json()=default;
    Json(bool b):kind(Boolean),flag(b) {}
    Json(int n):kind(Number),number(n) {}
    Json(int64_t n):kind(Number),number(n) {}
    Json(const char* s):kind(String),string(s) {}
    Json(std::string s):kind(String),string(std::move(s)) {}
    static Json dict(std::initializer_list<std::pair<const std::string,Json>> entries) {
        Json j;j.kind=Object;j.object=entries;return j;
    }
    const Json& at(const std::string& key) const {
        if (kind!=Object || !object.count(key)) throw std::runtime_error("invalid-room-response");
        return object.at(key);
    }
    bool has(const std::string& key) const { return kind==Object && object.count(key); }
    std::string str() const { if(kind!=String) throw std::runtime_error("invalid-room-response");return string; }
    int64_t integer(int64_t low,int64_t high) const {
        if(kind!=Number || number<low || number>high) throw std::runtime_error("invalid-room-response");return number;
    }
    bool boolean() const { if(kind!=Boolean) throw std::runtime_error("invalid-room-response");return flag; }
    const std::vector<Json>& list() const { if(kind!=Array) throw std::runtime_error("invalid-room-response");return array; }
    static std::string quote(const std::string& s) {
        std::string out="\"";
        const char* hex="0123456789abcdef";
        for (unsigned char c:s) {
            if (c=='"' || c=='\\') { out+='\\';out+=(char)c; }
            else if(c<32) { out+="\\u00";out+=hex[c>>4];out+=hex[c&15]; }
            else out+=(char)c;
        }
        return out+'"';
    }
    std::string dump() const {
        if(kind==Null) return "null";
        if(kind==Boolean) return flag?"true":"false";
        if(kind==Number) return std::to_string(number);
        if(kind==String) return quote(string);
        std::string out=kind==Array?"[":"{";bool first=true;
        if(kind==Array) for(const auto& value:array) { if(!first)out+=',';first=false;out+=value.dump(); }
        else for(const auto& [key,value]:object) { if(!first)out+=',';first=false;out+=quote(key)+':'+value.dump(); }
        return out+(kind==Array?"]":"}");
    }
    static Json parse(const std::string& input) {
        struct Reader {
            const std::string& s;size_t at=0;unsigned nodes=0;
            [[noreturn]] void fail() { throw std::runtime_error("invalid-room-response"); }
            void space() { while(at<s.size() && (s[at]==' ' || s[at]=='\r' || s[at]=='\n' || s[at]=='\t')) ++at; }
            bool take(char c) { space();if(at<s.size() && s[at]==c){++at;return true;}return false; }
            unsigned hex4() {
                unsigned n=0;
                for(unsigned i=0;i<4;++i) {
                    if(at==s.size())fail();unsigned c=(unsigned char)s[at++];
                    unsigned d=c>='0'&&c<='9'?c-'0':c>='A'&&c<='F'?c-'A'+10:c>='a'&&c<='f'?c-'a'+10:16;
                    if(d==16)fail();n=(n<<4)|d;
                }
                return n;
            }
            std::string text() {
                if(!take('"'))fail();std::string out;
                while(at<s.size()) {
                    unsigned c=(unsigned char)s[at++];if(c=='"')return out;if(c<32)fail();
                    if(c=='\\') {
                        if(at==s.size())fail();c=(unsigned char)s[at++];
                        if(c=='u') {
                            c=hex4();
                            if(c>=0xd800 && c<=0xdbff) {
                                if(at+2>s.size() || s[at]!='\\' || s[at+1]!='u')fail();at+=2;
                                unsigned low=hex4();if(low<0xdc00 || low>0xdfff)fail();c=0x10000+((c-0xd800)<<10)+(low-0xdc00);
                            } else if(c>=0xdc00 && c<=0xdfff)fail();
                            if(c<0x80)out+=(char)c;
                            else if(c<0x800){out+=(char)(0xc0|(c>>6));out+=(char)(0x80|(c&63));}
                            else if(c<0x10000){out+=(char)(0xe0|(c>>12));out+=(char)(0x80|((c>>6)&63));out+=(char)(0x80|(c&63));}
                            else{out+=(char)(0xf0|(c>>18));out+=(char)(0x80|((c>>12)&63));out+=(char)(0x80|((c>>6)&63));out+=(char)(0x80|(c&63));}
                            if(out.size()>4096)fail();continue;
                        }
                        if(c=='b')c='\b';else if(c=='f')c='\f';else if(c=='n')c='\n';else if(c=='r')c='\r';else if(c=='t')c='\t';
                        else if(c!='"' && c!='\\' && c!='/')fail();
                    }
                    out+=(char)c;if(out.size()>4096)fail();
                }
                fail();
            }
            Json value(unsigned depth=0) {
                if(++nodes>4096 || depth>24)fail();space();if(at==s.size())fail();
                Json j;
                if(s[at]=='"')return Json(text());
                if(take('{')) {
                    j.kind=Object;if(take('}'))return j;
                    do { std::string key=text();if(!take(':') || j.object.count(key))fail();j.object.emplace(key,value(depth+1)); } while(take(','));
                    if(!take('}'))fail();return j;
                }
                if(take('[')) {
                    j.kind=Array;if(take(']'))return j;
                    do{j.array.push_back(value(depth+1));}while(take(','));if(!take(']'))fail();return j;
                }
                for(const auto& literal:{std::string("true"),std::string("false"),std::string("null")}) {
                    if(s.compare(at,literal.size(),literal)==0) { at+=literal.size();return literal=="null"?Json():Json(literal=="true"); }
                }
                const size_t begin=at;const bool negative=s[at]=='-';if(negative)++at;
                if(at==s.size() || s[at]<'0' || s[at]>'9')fail();
                uint64_t n=0,limit=uint64_t(INT64_MAX)+(negative?1:0);
                bool zero=s[at]=='0';
                do { unsigned digit=s[at++]-'0';if(n>(limit-digit)/10)fail();n=n*10+digit; } while(at<s.size() && s[at]>='0' && s[at]<='9');
                if(zero && at-begin>(negative?2u:1u))fail();
                j.kind=Number;j.number=negative?(n==uint64_t(INT64_MAX)+1?INT64_MIN:-(int64_t)n):(int64_t)n;return j;
            }
        };
        if(input.size()>65536)throw std::runtime_error("invalid-room-response");
        Reader reader{input};Json result=reader.value();reader.space();if(reader.at!=input.size())reader.fail();return result;
    }
};
}
