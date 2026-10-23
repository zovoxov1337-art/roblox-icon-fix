#include "../src/core.hpp"
#include <cassert>
#include <iostream>
#include <random>

template<class F> void rejects(F f){bool caught=false;try{f();}catch(const std::exception&){caught=true;}assert(caught);}
int main(){
    std::vector<std::string> cases={"", "# hosts\r\n127.0.0.1 localhost\r\n", "127.0.0.1 localhost", "\xef\xbb\xbf", "\xef\xbb\xbf" "127.0.0.1 localhost\n", "0.0.0.0 tr.rbxcdn.com\r\n", "1.2.3.4 tr.rbxcdn.com other.local # do not remove\n", "1.2.3.4 other.local TR.RBXCDN.COM.\tmore.local\r\n", "::1 tr.rbxcdn.com", "# tr.rbxcdn.com\n1.1.1.1 othertr.rbxcdn.com\r\n", "# \xcf\xf0\xe8\xe2\xe5\xf2\r\n1.2.3.4 tr.rbxcdn.com", "1.2.3.4 tr.rbxcdn.com tr.rbxcdn.com\n\n"};
    for(const auto& s:cases){
        auto fixed=rif::apply(s,"18.65.39.105");
        assert(rif::undo(fixed)==s);
        assert(rif::apply(fixed,"18.65.39.105")==fixed);
        assert(rif::apply(fixed,"18.64.211.78")==rif::apply(s,"18.64.211.78"));
        assert(rif::undo(fixed+"9.9.9.9 unrelated.example\r\n")==s+"9.9.9.9 unrelated.example\r\n");
    }
    for(const auto& s:{"127.0.0.1","0.0.0.0","10.0.0.1","169.254.2.3","192.168.1.1","172.16.0.1","172.31.5.6","100.64.2.3","224.0.0.1","255.255.255.255","1.2.3","1.2.3.256","1.2.3.4evil","1.2.3.4\n5.6.7.8","01.2.3.4","192.0.2.1","198.51.100.2","203.0.113.1"}){assert(!rif::publicIPv4(s));rejects([&]{rif::apply("",s);});}
    for(const auto& s:{"1.1.1.1","8.8.8.8","18.65.39.105","172.15.2.3","172.32.2.3"})assert(rif::publicIPv4(s));
    rejects([]{rif::undo("# RIF-BEGIN v1 pad=0\n1.1.1.1 tr.rbxcdn.com\n");});
    rejects([]{rif::undo("# RIF-END v1\n");});
    rejects([]{rif::apply(std::string("abc\0xyz",7),"1.1.1.1");});
    rejects([]{rif::apply("\xff\xfe" "x","1.1.1.1");});
    auto conflict=rif::apply("1.2.3.4 tr.rbxcdn.com other.local\n","1.1.1.1");
    conflict.replace(conflict.find("other.local"),11,"edited.local");
    rejects([&]{rif::undo(conflict);});
    auto ips=rif::dnsAddresses(R"({"Status":0,"Answer":[{"type":5,"data":"cdn.example"},{"type":1,"data":"18.64.211.78"},{"type":1,"data":"127.0.0.1"},{"type":1,"data":"18.64.211.78"}]})");
    assert(ips==std::vector<std::string>{"18.64.211.78"});
    assert(rif::dnsAddresses(R"({"Status":3,"Answer":[{"type":1,"data":"1.1.1.1"}]})").empty());
    auto url=rif::imageURL(R"({"data":[{"state":"Completed","imageUrl":"https:\/\/tr.rbxcdn.com\/example\/150\/150"}]})");
    assert(url=="https://tr.rbxcdn.com/example/150/150");
    for(auto s:{"http://tr.rbxcdn.com/a","https://tr.rbxcdn.com.evil/a","https://tr.rbxcdn.com@evil/a","https://tr.rbxcdn.com/\n--insecure","https://tr.rbxcdn.com/"})assert(!rif::validImageURL(s));
    for(auto s:{"{", "{}x", R"({"Status":0,"Status":0})", R"([1,])", R"({"x":01})", R"({"x":"\ud800"})"})rejects([&]{rif::JsonParser(s).parse();});
    assert(rif::JsonParser(R"({"x":"\u0074\u0072"})").parse().at("x").value=="tr");
    assert(!rif::imageMagic("<html>bad image</html>"));
    assert(rif::imageMagic(std::string("\x89PNG\r\n\x1a\n")+std::string(32,'\0')));
    std::mt19937 random(42);
    for(int i=0;i<2000;i++){
        std::string s;
        for(int n=0;n<static_cast<int>(random()%12);n++){
            s+=(random()%2?"# preserved comment":"1.2.3.4 other.local tr.rbxcdn.com more.local");
            s+=(random()%2?"\n":"\r\n");
        }
        if(!s.empty() && random()%2)s.pop_back();
        assert(rif::undo(rif::apply(s,"18.65.39.105"))==s);
    }
    std::cout<<"PASS: byte-exact rollback, unrelated edits, conflict detection, 2,000 round trips, DNS/JSON and image validation\n";
}
