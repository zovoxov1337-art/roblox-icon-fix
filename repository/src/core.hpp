#pragma once
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace rif {
inline constexpr const char* domain = "tr.rbxcdn.com";
inline std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
inline bool publicIPv4(const std::string& s) {
    unsigned a[4]{}; size_t p=0;
    for(int n=0;n<4;n++) {
        size_t start=p;
        while(p<s.size() && s[p]>='0' && s[p]<='9') { a[n]=a[n]*10+(s[p++]-'0'); if(a[n]>255) return false; }
        if(p==start || p-start>3 || (p-start>1 && s[start]=='0')) return false;
        if(n<3 && (p>=s.size() || s[p++]!='.')) return false;
    }
    if(p!=s.size()) return false;
    return a[0]!=0 && a[0]!=10 && a[0]!=127 && a[0]<224
        && !(a[0]==169 && a[1]==254) && !(a[0]==172 && a[1]>=16 && a[1]<=31)
        && !(a[0]==192 && (a[1]==168 || (a[1]==0 && (a[2]==0 || a[2]==2))))
        && !(a[0]==100 && a[1]>=64 && a[1]<=127)
        && !(a[0]==198 && (a[1]==18 || a[1]==19 || (a[1]==51 && a[2]==100)))
        && !(a[0]==203 && a[1]==0 && a[2]==113);
}
inline std::vector<std::string> lines(const std::string& s) {
    std::vector<std::string> out;
    for(size_t p=0;p<s.size();) { auto e=s.find('\n',p); if(e==std::string::npos) e=s.size()-1; out.push_back(s.substr(p,e-p+1)); p=e+1; }
    return out;
}
inline std::string bare(std::string s) {
    if(!s.empty() && s.back()=='\n') s.pop_back();
    if(!s.empty() && s.back()=='\r') s.pop_back();
    return s;
}
inline std::string hex(const std::string& s) {
    static const char* h="0123456789abcdef"; std::string o;
    for(unsigned char c:s) {o+=h[c>>4];o+=h[c&15];} return o;
}
inline std::string unhex(const std::string& s) {
    if(s.size()%2) throw std::runtime_error("Invalid saved hosts line.");
    auto val=[](char c)->int { if(c>='0'&&c<='9')return c-'0'; if(c>='a'&&c<='f')return c-'a'+10; throw std::runtime_error("Invalid saved hosts line."); };
    std::string o; for(size_t i=0;i<s.size();i+=2) o+=static_cast<char>(val(s[i])*16+val(s[i+1]));
    return o;
}
struct Scrubbed { bool found=false; std::string text; };
inline Scrubbed scrubLine(const std::string& raw) {
    Scrubbed r; r.text=raw;
    auto end=raw.find('#'); if(end==std::string::npos)end=raw.size();
    std::vector<std::pair<size_t,size_t>> tokens;
    for(size_t p=0;p<end;) {
        while(p<end && std::isspace(static_cast<unsigned char>(raw[p])))p++;
        size_t start=p; while(p<end && !std::isspace(static_cast<unsigned char>(raw[p])))p++;
        if(start<p)tokens.emplace_back(start,p);
    }
    size_t keep=0;
    for(size_t i=tokens.size();i>1;--i) {
        auto [a,b]=tokens[i-1]; std::string t=lower(raw.substr(a,b-a));
        if(!t.empty() && t.back()=='.')t.pop_back();
        if(t==domain) {r.found=true;r.text.erase(a,b-a);} else keep++;
    }
    if(r.found && !keep)r.text.clear();
    return r;
}
inline std::string withNewline(std::string s) {
    if(!s.empty() && s.back()!='\n')s+="\r\n";
    return s;
}
inline void validateHosts(const std::string& s) {
    if(s.size()>1024*1024)throw std::runtime_error("Hosts file is larger than 1 MiB.");
    if(s.find('\0')!=std::string::npos || s.compare(0,2,"\xff\xfe")==0 || s.compare(0,2,"\xfe\xff")==0)
        throw std::runtime_error("UTF-16 or binary hosts file is unsupported. No changes made.");
}
inline std::string undo(const std::string& input, bool* changed=nullptr) {
    validateHosts(input);
    const bool bom=input.compare(0,3,"\xef\xbb\xbf")==0;
    auto ls=lines(bom?input.substr(3):input);
    std::string out=bom?"\xef\xbb\xbf":""; bool did=false; int blocks=0;
    for(size_t i=0;i<ls.size();) {
        const auto line=bare(ls[i]);
        if(line=="# RIF-SAVED-BEGIN v1") {
            if(i+2>=ls.size() || bare(ls[i+1]).rfind("# RIF-ORIGINAL ",0)!=0)throw std::runtime_error("Saved hosts block is damaged.");
            auto original=unhex(bare(ls[i+1]).substr(15));
            if(original.empty() || lines(original).size()!=1)throw std::runtime_error("Invalid saved hosts line.");
            auto scrub=scrubLine(original);
            if(!scrub.found)throw std::runtime_error("Saved hosts block has an unexpected domain.");
            size_t j=i+2; std::string actual;
            while(j<ls.size() && bare(ls[j])!="# RIF-SAVED-END v1") actual+=ls[j++];
            if(j==ls.size() || actual!=withNewline(scrub.text))throw std::runtime_error("Saved hosts block was edited. Refusing to discard your changes.");
            out+=original; i=j+1; did=true;
        } else if(line.rfind("# RIF-BEGIN v1 pad=",0)==0) {
            if(++blocks>1 || (line!="# RIF-BEGIN v1 pad=0" && line!="# RIF-BEGIN v1 pad=2") || i+2>=ls.size() || bare(ls[i+2])!="# RIF-END v1")throw std::runtime_error("Managed hosts block is damaged.");
            std::istringstream in(bare(ls[i+1]));std::string ip,host,extra;in>>ip>>host;
            if(!publicIPv4(ip)||host!=domain||(in>>extra))throw std::runtime_error("Managed hosts block was edited.");
            if(line.back()=='2') {
                if(out.size()<2 || out.substr(out.size()-2)!="\r\n")throw std::runtime_error("Managed hosts separator was edited.");
                out.resize(out.size()-2);
            }
            i+=3; did=true;
        } else if(line.rfind("# RIF-",0)==0) {
            throw std::runtime_error("Unrecognized or incomplete managed hosts block.");
        } else {out+=ls[i++];}
    }
    if(changed)*changed=did;
    return out;
}
inline std::string apply(const std::string& input,const std::string& ip) {
    if(!publicIPv4(ip))throw std::runtime_error("Non-public IPv4 address rejected.");
    auto clean=undo(input);
    const bool bom=clean.compare(0,3,"\xef\xbb\xbf")==0;
    std::string out=bom?"\xef\xbb\xbf":"";
    for(const auto& line:lines(bom?clean.substr(3):clean)) {
        auto s=scrubLine(line);
        if(!s.found)out+=line;
        else out+="# RIF-SAVED-BEGIN v1\r\n# RIF-ORIGINAL "+hex(line)+"\r\n"+withNewline(s.text)+"# RIF-SAVED-END v1\r\n";
    }
    const int pad=(!out.empty() && out.back()!='\n')?2:0;
    if(pad)out+="\r\n";
    out+="# RIF-BEGIN v1 pad="+std::to_string(pad)+"\r\n"+ip+" "+domain+"\r\n# RIF-END v1\r\n";
    validateHosts(out); return out;
}

// Small strict JSON reader for DNS and the public thumbnail API. No external code.
struct Json {
    enum Kind { Null,Bool,Number,String,Array,Object } kind=Null;
    std::string value; std::vector<Json> list; std::map<std::string,Json> fields;
    const Json& at(const std::string& k)const {static const Json empty;auto it=fields.find(k);return it==fields.end()?empty:it->second;}
};
class JsonParser {
    const std::string& s; size_t p=0;
    [[noreturn]] void fail()const {throw std::runtime_error("Invalid JSON response.");}
    void ws(){while(p<s.size() && (s[p]==' '||s[p]=='\r'||s[p]=='\n'||s[p]=='\t'))p++;}
    bool take(char c){ws();if(p<s.size()&&s[p]==c){p++;return true;}return false;}
    unsigned unicode(){unsigned u=0;for(int i=0;i<4;i++){if(p>=s.size())fail();char c=s[p++];u*=16;if(c>='0'&&c<='9')u+=c-'0';else if(c>='a'&&c<='f')u+=c-'a'+10;else if(c>='A'&&c<='F')u+=c-'A'+10;else fail();}return u;}
    std::string str(){
        if(!take('"'))fail();
        std::string out;
        while(p<s.size()) {
            unsigned char c=s[p++];if(c=='"')return out;if(c<32)fail();
            if(c!='\\'){out+=static_cast<char>(c);continue;}
            if(p==s.size())fail();
            c=s[p++];
            switch(c){
                case '"':case '\\':case '/':out+=static_cast<char>(c);break;
                case 'b':out+='\b';break;case 'f':out+='\f';break;case 'n':out+='\n';break;case 'r':out+='\r';break;case 't':out+='\t';break;
                case 'u':{unsigned u=unicode();if(u>=0xd800&&u<=0xdbff){if(p+2>s.size()||s[p++]!='\\'||s[p++]!='u')fail();unsigned l=unicode();if(l<0xdc00||l>0xdfff)fail();u=0x10000+((u-0xd800)<<10)+(l-0xdc00);}else if(u>=0xdc00&&u<=0xdfff)fail();
                    if(u<128)out+=static_cast<char>(u);else if(u<2048){out+=static_cast<char>(0xc0|(u>>6));out+=static_cast<char>(0x80|(u&63));}else if(u<65536){out+=static_cast<char>(0xe0|(u>>12));out+=static_cast<char>(0x80|((u>>6)&63));out+=static_cast<char>(0x80|(u&63));}else{out+=static_cast<char>(0xf0|(u>>18));out+=static_cast<char>(0x80|((u>>12)&63));out+=static_cast<char>(0x80|((u>>6)&63));out+=static_cast<char>(0x80|(u&63));}break;}
                default:fail();
            }
        }fail();
    }
    Json item(int depth){
        if(depth>32)fail();
        ws();if(p>=s.size())fail();Json j;
        if(s[p]=='"'){j.kind=Json::String;j.value=str();}
        else if(take('{')){j.kind=Json::Object;if(!take('}'))do{auto k=str();if(!take(':')||j.fields.count(k))fail();j.fields.emplace(k,item(depth+1));if(take('}'))return j;}while(take(','));else return j;fail();}
        else if(take('[')){j.kind=Json::Array;if(!take(']'))do{j.list.push_back(item(depth+1));if(take(']'))return j;}while(take(','));else return j;fail();}
        else if(s.compare(p,4,"null")==0){p+=4;}
        else if(s.compare(p,4,"true")==0){j.kind=Json::Bool;j.value="true";p+=4;}
        else if(s.compare(p,5,"false")==0){j.kind=Json::Bool;j.value="false";p+=5;}
        else {j.kind=Json::Number;size_t a=p;if(s[p]=='-')p++;if(p>=s.size())fail();if(s[p]=='0')p++;else{if(s[p]<'1'||s[p]>'9')fail();while(p<s.size()&&isdigit(static_cast<unsigned char>(s[p])))p++;}if(p<s.size()&&s[p]=='.'){p++;size_t t=p;while(p<s.size()&&isdigit(static_cast<unsigned char>(s[p])))p++;if(p==t)fail();}if(p<s.size()&&(s[p]=='e'||s[p]=='E')){p++;if(p<s.size()&&(s[p]=='+'||s[p]=='-'))p++;size_t t=p;while(p<s.size()&&isdigit(static_cast<unsigned char>(s[p])))p++;if(p==t)fail();}j.value=s.substr(a,p-a);}
        return j;
    }
public:
    explicit JsonParser(const std::string& in):s(in){if(s.size()>512*1024)fail();}
    Json parse(){auto j=item(0);ws();if(p!=s.size())fail();return j;}
};
inline std::vector<std::string> dnsAddresses(const std::string& input) {
    auto j=JsonParser(input).parse(); std::vector<std::string> out;
    if(j.at("Status").kind!=Json::Number || j.at("Status").value!="0")return out;
    for(const auto& a:j.at("Answer").list) {
        auto ip=a.at("data").value;
        if(a.at("type").kind==Json::Number && a.at("type").value=="1" && a.at("data").kind==Json::String && publicIPv4(ip)
            && std::find(out.begin(),out.end(),ip)==out.end())out.push_back(ip);
    }return out;
}
inline bool validImageURL(const std::string& s) {
    const std::string prefix="https://tr.rbxcdn.com/";
    if(s.rfind(prefix,0)!=0 || s.size()>2048 || s.size()<=prefix.size())return false;
    for(unsigned char c:s)if(c<=32||c>=127||c=='\\'||c=='"'||c=='#')return false;
    return true;
}
inline std::string imageURL(const std::string& input) {
    auto j=JsonParser(input).parse();
    for(const auto& a:j.at("data").list) {auto s=a.at("imageUrl").value;if(a.at("state").value=="Completed" && validImageURL(s))return s;}
    return {};
}
inline bool imageMagic(const std::string& s) {
    if(s.size()<16)return false;
    return s.compare(0,8,"\x89PNG\r\n\x1a\n")==0 || s.compare(0,3,"\xff\xd8\xff")==0
        || s.compare(0,6,"GIF87a")==0 || s.compare(0,6,"GIF89a")==0
        || (s.compare(0,4,"RIFF")==0 && s.compare(8,4,"WEBP")==0);
}
} // namespace rif
