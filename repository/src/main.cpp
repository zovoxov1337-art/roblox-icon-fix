#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <atomic>
#include <chrono>
#include <future>
#include <iomanip>
#include <mutex>
#include <thread>
#include "core.hpp"

namespace {
constexpr UINT LOG_MESSAGE=WM_APP+1, DONE_MESSAGE=WM_APP+2, COMMIT_MESSAGE=WM_APP+3;
constexpr int FIX=101, CHECK=102, UNDO=103, FOLDER=104, CANCEL=105;
HWND windowHandle=nullptr, logEdit=nullptr, progress=nullptr;
HFONT normalFont=nullptr, titleFont=nullptr, boldFont=nullptr, monoFont=nullptr, smallFont=nullptr;
HBRUSH background=nullptr, logBackground=nullptr;
std::wstring systemDir, hostsPath, backupDir, logPath;
std::wstring statusTitle=L"Готово к проверке", statusDetail=L"Закрой Roblox и нажми «Исправить иконки».";
std::mutex logMutex;
std::atomic<bool> cancelled{false}, critical{false};
bool busy=false, closeAfter=false; int statusKind=0, dpi=96;
std::thread job;
int px(int v){return MulDiv(v,dpi,96);}
constexpr COLORREF BG=RGB(16,21,31), PANEL=RGB(25,33,46), FG=RGB(236,242,250), MUTED=RGB(158,173,195), ACCENT=RGB(92,223,184);

std::wstring wide(const std::string& s){
    if(s.empty())return {};
    int n=MultiByteToWideChar(CP_UTF8,0,s.data(),static_cast<int>(s.size()),nullptr,0);
    std::wstring o(n,0);MultiByteToWideChar(CP_UTF8,0,s.data(),static_cast<int>(s.size()),o.data(),n);return o;
}
std::string utf8(const std::wstring& s){
    if(s.empty())return {};
    int n=WideCharToMultiByte(CP_UTF8,0,s.data(),static_cast<int>(s.size()),nullptr,0,nullptr,nullptr);
    std::string o(n,0);WideCharToMultiByte(CP_UTF8,0,s.data(),static_cast<int>(s.size()),o.data(),n,nullptr,nullptr);return o;
}
std::wstring osError(DWORD e=GetLastError()){
    wchar_t* text=nullptr;FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER|FORMAT_MESSAGE_FROM_SYSTEM|FORMAT_MESSAGE_IGNORE_INSERTS,nullptr,e,0,reinterpret_cast<wchar_t*>(&text),0,nullptr);
    std::wstring s=text?text:L"Ошибка Windows";if(text)LocalFree(text);
    while(!s.empty()&&(s.back()==L'\r'||s.back()==L'\n'))s.pop_back();
    return s+L" ("+std::to_wstring(e)+L")";
}
[[noreturn]] void fail(const std::wstring& what){throw std::runtime_error(utf8(what));}
struct Handle{
    HANDLE h=INVALID_HANDLE_VALUE;Handle()=default;explicit Handle(HANDLE v):h(v){}
    ~Handle(){reset();}Handle(const Handle&)=delete;Handle& operator=(const Handle&)=delete;
    void reset(HANDLE v=INVALID_HANDLE_VALUE){if(h!=INVALID_HANDLE_VALUE&&h)CloseHandle(h);h=v;}
    bool valid()const{return h!=INVALID_HANDLE_VALUE&&h;}
};
std::wstring stamp(){SYSTEMTIME t;GetLocalTime(&t);wchar_t s[80];swprintf(s,80,L"%04u%02u%02u_%02u%02u%02u_%03u",t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond,t.wMilliseconds);return s;}
void safeDirectory(const std::wstring& p){
    if(!CreateDirectoryW(p.c_str(),nullptr)&&GetLastError()!=ERROR_ALREADY_EXISTS)fail(L"Не удалось создать папку резервных копий: "+osError());
    DWORD a=GetFileAttributesW(p.c_str());if(a==INVALID_FILE_ATTRIBUTES||!(a&FILE_ATTRIBUTE_DIRECTORY)||(a&FILE_ATTRIBUTE_REPARSE_POINT))fail(L"Папка резервных копий не является обычной папкой.");
}
void logLine(const std::wstring& line){
    std::lock_guard<std::mutex> lock(logMutex);SYSTEMTIME t;GetLocalTime(&t);wchar_t time[20];swprintf(time,20,L"[%02u:%02u:%02u] ",t.wHour,t.wMinute,t.wSecond);
    auto s=std::wstring(time)+line+L"\r\n";
    if(windowHandle){auto* copy=new std::wstring(s);if(!PostMessageW(windowHandle,LOG_MESSAGE,0,reinterpret_cast<LPARAM>(copy)))delete copy;}
    if(!logPath.empty()){
        Handle f(CreateFileW(logPath.c_str(),FILE_APPEND_DATA,FILE_SHARE_READ,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
        if(f.valid()){BY_HANDLE_FILE_INFORMATION info{};if(GetFileInformationByHandle(f.h,&info)&&!(info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)){auto b=utf8(s);DWORD written=0;WriteFile(f.h,b.data(),static_cast<DWORD>(b.size()),&written,nullptr);}}
    }
}
std::string readFile(const std::wstring& p){
    Handle f(CreateFileW(p.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
    if(!f.valid())fail(L"Не удалось прочитать hosts: "+osError());
    BY_HANDLE_FILE_INFORMATION info{};if(!GetFileInformationByHandle(f.h,&info)||(info.dwFileAttributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY)))fail(L"Файл hosts должен быть обычным файлом, не ссылкой.");
    LARGE_INTEGER size{};if(!GetFileSizeEx(f.h,&size)||size.QuadPart>1024*1024||size.QuadPart<0)fail(L"Необычный размер hosts. Изменения отменены.");
    std::string b(static_cast<size_t>(size.QuadPart),0);DWORD n=0;if(!b.empty()&&(!ReadFile(f.h,b.data(),static_cast<DWORD>(b.size()),&n,nullptr)||n!=b.size()))fail(L"Не удалось полностью прочитать hosts.");return b;
}
void writeNew(const std::wstring& p,const std::string& b){
    Handle f(CreateFileW(p.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr));
    if(!f.valid())fail(L"Не удалось подготовить файл: "+osError());
    DWORD n=0;if(!WriteFile(f.h,b.data(),static_cast<DWORD>(b.size()),&n,nullptr)||n!=b.size()||!FlushFileBuffers(f.h))fail(L"Не удалось записать файл: "+osError());
}
std::wstring replaceHosts(const std::string& expected,const std::string& replacement){
    safeDirectory(backupDir);
    auto suffix=stamp()+L"_"+std::to_wstring(GetCurrentProcessId());
    auto temp=hostsPath+L".rif_"+suffix+L".tmp";
    auto backup=backupDir+L"\\hosts_"+suffix+L".bak";
    try{
        writeNew(temp,replacement);
        if(readFile(hostsPath)!=expected)fail(L"Другая программа изменила hosts. Повтори действие; чужие изменения сохранены.");
        if(!ReplaceFileW(hostsPath.c_str(),temp.c_str(),backup.c_str(),0,nullptr,nullptr)){
            auto error=osError();
            if(GetFileAttributesW(backup.c_str())!=INVALID_FILE_ATTRIBUTES)logLine(L"Копия исходного hosts после сбоя: "+backup);
            // Some documented ReplaceFile failure modes leave the original at the backup path.
            // Recover only if hosts is absent; never overwrite a concurrently created file.
            DWORD a=GetFileAttributesW(hostsPath.c_str());DWORD attributeError=GetLastError();
            if(a==INVALID_FILE_ATTRIBUTES && attributeError==ERROR_FILE_NOT_FOUND
                && GetFileAttributesW(backup.c_str())!=INVALID_FILE_ATTRIBUTES){
                if(CopyFileW(backup.c_str(),hostsPath.c_str(),TRUE))logLine(L"После сбоя замены исходный hosts восстановлен из копии.");
                else logLine(L"Требуется восстановление hosts вручную из копии: "+backup);
            }
            fail(L"Windows не разрешила заменить hosts: "+error+L". Подробности и путь к копии — в журнале.");
        }
        if(readFile(hostsPath)!=replacement)fail(L"Содержимое hosts изменилось сразу после записи. Резервная копия: "+backup);
    }catch(...){DeleteFileW(temp.c_str());throw;}
    logLine(L"Резервная копия: "+backup);return backup;
}

// curl is the Windows component from System32. No shell, downloaded tools or curlrc.
struct ProcessResult {DWORD code=1;std::string output,error;bool timedOut=false;};
std::wstring quote(const std::wstring& s){
    std::wstring o=L"\"";size_t slashes=0;
    for(auto c:s){if(c==L'\\'){slashes++;continue;}if(c==L'"'){o.append(slashes*2+1,L'\\');o+=c;}else{o.append(slashes,L'\\');o+=c;}slashes=0;}
    o.append(slashes*2,L'\\');return o+L"\"";
}
ProcessResult runProcess(const std::wstring& exe,const std::vector<std::wstring>& args,DWORD deadline=12000){
    ProcessResult result;SECURITY_ATTRIBUTES sa{sizeof(sa),nullptr,TRUE};
    Handle outRead,outWrite,errRead,errWrite,inNull;
    if(!CreatePipe(&outRead.h,&outWrite.h,&sa,0)||!CreatePipe(&errRead.h,&errWrite.h,&sa,0)){result.error="Cannot create pipes";return result;}
    SetHandleInformation(outRead.h,HANDLE_FLAG_INHERIT,0);SetHandleInformation(errRead.h,HANDLE_FLAG_INHERIT,0);
    inNull.reset(CreateFileW(L"NUL",GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,&sa,OPEN_EXISTING,0,nullptr));
    if(!inNull.valid()){result.error="Cannot open NUL";return result;}
    SIZE_T size=0;InitializeProcThreadAttributeList(nullptr,1,0,&size);std::vector<unsigned char> mem(size);
    auto* attrs=reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(mem.data());
    if(!InitializeProcThreadAttributeList(attrs,1,0,&size)){result.error="Cannot initialize process attributes";return result;}
    HANDLE inherited[3]={outWrite.h,errWrite.h,inNull.h};
    if(!UpdateProcThreadAttribute(attrs,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,inherited,sizeof(inherited),nullptr,nullptr)){DeleteProcThreadAttributeList(attrs);result.error="Cannot set process handles";return result;}
    STARTUPINFOEXW si{};si.StartupInfo.cb=sizeof(si);si.StartupInfo.dwFlags=STARTF_USESTDHANDLES|STARTF_USESHOWWINDOW;si.StartupInfo.wShowWindow=SW_HIDE;si.StartupInfo.hStdOutput=outWrite.h;si.StartupInfo.hStdError=errWrite.h;si.StartupInfo.hStdInput=inNull.h;si.lpAttributeList=attrs;
    std::wstring command=quote(exe);for(const auto& a:args)command+=L" "+quote(a);
    PROCESS_INFORMATION pi{};
    BOOL created=CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW|EXTENDED_STARTUPINFO_PRESENT,nullptr,systemDir.c_str(),&si.StartupInfo,&pi);
    DWORD createError=GetLastError();DeleteProcThreadAttributeList(attrs);outWrite.reset();errWrite.reset();inNull.reset();
    if(!created){result.error=utf8(osError(createError));return result;}
    Handle process(pi.hProcess),thread(pi.hThread);ULONGLONG start=GetTickCount64();bool tooLarge=false;
    auto drain=[&](HANDLE h,std::string& target,size_t cap){
        DWORD available=0;
        while(PeekNamedPipe(h,nullptr,0,nullptr,&available,nullptr)&&available){
            char buffer[4096];DWORD n=0;if(!ReadFile(h,buffer,std::min<DWORD>(available,sizeof(buffer)),&n,nullptr)||!n)break;
            if(target.size()+n>cap){tooLarge=true;return;}target.append(buffer,n);
        }
    };
    for(;;){
        drain(outRead.h,result.output,600*1024);drain(errRead.h,result.error,16*1024);
        if(WaitForSingleObject(process.h,25)==WAIT_OBJECT_0)break;
        if(tooLarge||(cancelled.load()&&!critical.load())||GetTickCount64()-start>deadline){
            result.timedOut=GetTickCount64()-start>deadline;TerminateProcess(process.h,124);WaitForSingleObject(process.h,2000);break;
        }
    }
    drain(outRead.h,result.output,600*1024);drain(errRead.h,result.error,16*1024);GetExitCodeProcess(process.h,&result.code);
    if(tooLarge){result.code=125;result.error="Response exceeded size limit";}return result;
}
ProcessResult curl(const std::vector<std::wstring>& extra){
    std::vector<std::wstring> a={L"-q",L"--silent",L"--show-error",L"--noproxy",L"*",L"--proto",L"=https",L"--ipv4",L"--connect-timeout",L"3",L"--max-time",L"8",L"--max-filesize",L"524288",L"--user-agent",L"RobloxIconFix/1.0"};
    a.insert(a.end(),extra.begin(),extra.end());return runProcess(systemDir+L"\\curl.exe",a,11000);
}
void ensureNotCancelled(){if(cancelled.load())fail(L"Проверка отменена. Новые изменения не применены.");}
std::vector<std::string> getDNS(const std::string& host){
    auto lookup=[&](bool google){
        std::vector<std::string> ips;
        const auto label=google?L"Google DoH":L"Cloudflare DoH";
        auto url=google?"https://dns.google/resolve?name="+host+"&type=A&edns_client_subnet=0.0.0.0/0":"https://cloudflare-dns.com/dns-query?name="+host+"&type=A";
        auto r=curl({L"--fail",L"--header",L"Accept: application/dns-json",L"--resolve",google?L"dns.google:443:8.8.8.8":L"cloudflare-dns.com:443:1.1.1.1",L"--url",wide(url)});
        if(r.code==0)try{ips=rif::dnsAddresses(r.output);}catch(const std::exception&){}
        logLine(std::wstring(label)+L": "+wide(host)+L" — "+std::to_wstring(ips.size())+L" адресов.");
        if(ips.empty()&&!r.error.empty())logLine(std::wstring(label)+L": "+wide(r.error.substr(0,280)));
        return ips;
    };
    auto a=std::async(std::launch::async,lookup,true);auto b=std::async(std::launch::async,lookup,false);
    auto ips=a.get();for(auto& ip:b.get())if(std::find(ips.begin(),ips.end(),ip)==ips.end())ips.push_back(ip);
    if(ips.size()>16)ips.resize(16);
    return ips;
}
std::string fetchImage(){
    const std::wstring api=L"https://thumbnails.roblox.com/v1/users/avatar-headshot?userIds=1&size=150x150&format=Png&isCircular=false";
    auto parse=[](const ProcessResult& r){try{if(r.code==0)return rif::imageURL(r.output);}catch(const std::exception&){}return std::string();};
    auto result=parse(curl({L"--fail",L"--url",api}));
    if(!result.empty())return result;
    if(cancelled.load())return {};
    logLine(L"API тестовой картинки не ответил. Проверяю альтернативное разрешение его адреса.");
    auto ips=getDNS("thumbnails.roblox.com");
    for(size_t i=0;i<std::min<size_t>(ips.size(),2)&&!cancelled.load();i++){
        result=parse(curl({L"--fail",L"--resolve",L"thumbnails.roblox.com:443:"+wide(ips[i]),L"--url",api}));if(!result.empty())return result;
    }
    logLine(L"Картинку для теста получить не удалось. Будет проверена только связь с CDN по HTTPS.");return {};
}
struct Probe{bool ok=false,image=false;std::string ip,info;};
Probe probe(const std::string& ip,const std::string& image){
    Probe p;p.ip=ip;std::vector<std::wstring> args;
    if(!ip.empty()){args.push_back(L"--resolve");args.push_back(L"tr.rbxcdn.com:443:"+wide(ip));}
    args.insert(args.end(),{L"--url",wide(image.empty()?"https://tr.rbxcdn.com/":image),L"--write-out",L"\nRIF_META:%{http_code}|%{content_type}|%{remote_ip}"});
    auto r=curl(args);auto pos=r.output.rfind("\nRIF_META:");
    if(r.code!=0||pos==std::string::npos){p.info=r.timedOut?"timeout":("curl "+std::to_string(r.code)+": "+r.error.substr(0,180));return p;}
    auto meta=r.output.substr(pos+10);auto a=meta.find('|'),b=meta.rfind('|');
    if(a==std::string::npos||b==a){p.info="invalid response metadata";return p;}
    auto code=meta.substr(0,a),type=rif::lower(meta.substr(a+1,b-a-1));p.ip=meta.substr(b+1);
    if(!ip.empty()&&p.ip!=ip){p.info="unexpected remote IP";return p;}
    p.image=!image.empty()&&(code=="200"||code=="206")&&type.rfind("image/",0)==0&&rif::imageMagic(r.output.substr(0,pos));
    p.ok=image.empty()?(code=="200"||code=="403"||code=="404"):p.image;
    p.info="HTTP "+code+(p.image?", image OK":(image.empty()?", TLS verified":", image unavailable"));return p;
}
void flushDNS(){
    auto r=runProcess(systemDir+L"\\ipconfig.exe",{L"/flushdns"},12000);
    logLine(r.code==0?L"Кэш DNS Windows очищен.":L"Не удалось очистить кэш DNS. Перезагрузи Windows, если изменения не вступили в силу.");
}
struct Done{std::wstring title,detail;int kind=0;};
Done work(int action){
    if(action==UNDO){
        auto before=readFile(hostsPath);bool changed=false;auto after=rif::undo(before,&changed);
        if(!changed)return{L"Отменять нечего",L"Записей этой утилиты в hosts нет.",0};
        critical=true;ensureNotCancelled();PostMessageW(windowHandle,COMMIT_MESSAGE,0,0);
        replaceHosts(before,after);flushDNS();logLine(L"Откат завершён. Чужие строки hosts сохранены.");
        return{L"Прежние настройки возвращены",L"Полностью закрой Roblox и открой его заново.",1};
    }
    if(GetFileAttributesW((systemDir+L"\\curl.exe").c_str())==INVALID_FILE_ATTRIBUTES)fail(L"Не найден встроенный curl.exe. Нужна Windows 10 версии 1803 или новее / Windows 11.");
    logLine(L"Получаю актуальные адреса CDN и тестовую картинку Roblox.");
    auto imageFuture=std::async(std::launch::async,fetchImage);
    auto candidates=getDNS(rif::domain);auto image=imageFuture.get();ensureNotCancelled();
    logLine(image.empty()?L"Режим проверки: HTTPS без подтверждения загрузки картинки.":L"Получена свежая ссылка на публичную тестовую картинку.");
    auto baseline=probe("",image);ensureNotCancelled();
    logLine(L"Текущий адрес: "+wide(baseline.ip)+L" — "+wide(baseline.info));
    if(baseline.image)return{L"Тестовая картинка уже загружается",L"Изменения не нужны. Перезапусти Roblox, если в нём остались пустые иконки.",1};
    // Historical candidates from Roblox's public bug report. Each must pass fresh TLS validation.
    for(auto ip:{"18.64.211.78","18.64.211.88","18.64.211.103","18.64.211.77","18.65.39.105","2.20.245.170"})
        if(std::find(candidates.begin(),candidates.end(),ip)==candidates.end())candidates.push_back(ip);
    logLine(L"Проверяю альтернативные адреса: "+std::to_wstring(candidates.size())+L". Это может занять около минуты.");
    std::atomic<size_t> next{0};std::atomic<bool> found{false};std::mutex winnerMutex;Probe winner;
    auto worker=[&]{
        while(!cancelled.load()&&!found.load()){
            auto i=next.fetch_add(1);if(i>=candidates.size())break;auto p=probe(candidates[i],image);
            logLine(wide(candidates[i])+L" — "+wide(p.info));
            if(p.ok){std::lock_guard<std::mutex> lock(winnerMutex);if(!found.load()){winner=p;found=true;}}
        }
    };
    std::vector<std::future<void>> pending;for(int i=0;i<4;i++)pending.push_back(std::async(std::launch::async,worker));for(auto& f:pending)f.get();ensureNotCancelled();
    if(!found.load())return{L"Рабочий адрес не найден",L"Ничего не изменено. Причина может быть в сети, блокировке соединений или сбое Roblox. Подробности — в журнале.",2};
    logLine(L"Выбран адрес: "+wide(winner.ip));
    if(action==CHECK)return{winner.image?L"Найден адрес с работающей картинкой":L"Найден адрес с доступным HTTPS",winner.image?L"Нажми «Исправить иконки», чтобы применить его.":L"Загрузка картинки пока не подтверждена. Нажатие «Исправить иконки» применит только проверенный адрес CDN.",winner.image?1:3};
    auto before=readFile(hostsPath);auto after=rif::apply(before,winner.ip);
    critical=true;ensureNotCancelled();PostMessageW(windowHandle,COMMIT_MESSAGE,0,0);
    replaceHosts(before,after);flushDNS();logLine(L"Проверяю результат через системный DNS.");
    auto verify=probe("",image);logLine(L"После применения: "+wide(verify.ip)+L" — "+wide(verify.info));
    if(!verify.ok){
        logLine(L"Проверка после применения не прошла. Возвращаю прежнее содержимое hosts.");
        replaceHosts(after,before);flushDNS();return{L"Исправление не подтвердилось",L"Прежние настройки автоматически возвращены. Подробности — в журнале.",2};
    }
    logLine(L"Готово. Можно закрыть утилиту; изменение сохранится. Для отмены — «Отменить фикс».");
    if(verify.image)return{L"Готово — тестовая картинка загрузилась",L"Открой Roblox заново. Утилиту можно закрыть. Если адрес перестанет работать, запусти исправление ещё раз.",1};
    return{L"Адрес CDN обновлён",L"HTTPS работает, но загрузка картинки не проверена. Перезапусти Roblox и проверь иконки; при необходимости отмени фикс.",3};
}
void startJob(int action){
    if(busy)return;
    busy=true;cancelled=false;critical=false;
    statusKind=0;statusTitle=action==UNDO?L"Возвращаю прежние настройки…":L"Проверяю соединение…";statusDetail=L"Окно можно оставить открытым. Ход работы виден в журнале.";
    for(int id:{FIX,CHECK,UNDO})EnableWindow(GetDlgItem(windowHandle,id),FALSE);
    EnableWindow(GetDlgItem(windowHandle,CANCEL),TRUE);SendMessageW(progress,PBM_SETMARQUEE,TRUE,35);InvalidateRect(windowHandle,nullptr,FALSE);
    logLine(L"────────────────────────────");logLine(action==FIX?L"Исправление иконок":action==CHECK?L"Диагностика без изменения hosts":L"Отмена исправления");
    job=std::thread([action]{
        Done done;
        try{done=work(action);}catch(const std::exception& e){
            logLine(L"Ошибка: "+wide(e.what()));
            bool stopped=cancelled.load()&&!critical.load();
            done={stopped?L"Проверка остановлена":L"Не удалось завершить действие",stopped?L"Новые изменения не применены.":L"Причина указана в журнале ниже. При ошибке записи там же будет путь к резервной копии.",2};
        }
        critical=false;auto* result=new Done(std::move(done));if(!PostMessageW(windowHandle,DONE_MESSAGE,0,reinterpret_cast<LPARAM>(result)))delete result;
    });
}
void text(HDC dc,const wchar_t* s,int x,int y,int w,int h,HFONT font,COLORREF color,UINT flags=DT_LEFT|DT_TOP|DT_WORDBREAK){
    SelectObject(dc,font);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,color);RECT r{px(x),px(y),px(x+w),px(y+h)};DrawTextW(dc,s,-1,&r,flags);
}
void panel(HDC dc,int x,int y,int w,int h,COLORREF color){
    auto brush=CreateSolidBrush(color);auto old=SelectObject(dc,brush);auto pen=SelectObject(dc,GetStockObject(NULL_PEN));RoundRect(dc,px(x),px(y),px(x+w),px(y+h),px(16),px(16));SelectObject(dc,old);SelectObject(dc,pen);DeleteObject(brush);
}
void paint(HDC dc){
    RECT area;GetClientRect(windowHandle,&area);FillRect(dc,&area,background);
    text(dc,L"ROBLOX  /  ICON REPAIR",32,25,600,24,smallFont,ACCENT);
    text(dc,L"Верни иконки в Roblox",30,58,720,48,titleFont,FG);
    text(dc,L"Проверка и исправление загрузки аватарок и превью игр.",32,111,740,28,normalFont,MUTED);
    panel(dc,32,157,736,112,PANEL);
    COLORREF c=statusKind==1?ACCENT:statusKind==2?RGB(255,139,143):statusKind==3?RGB(247,199,109):FG;
    text(dc,statusTitle.c_str(),50,173,700,30,boldFont,c);
    text(dc,statusDetail.c_str(),50,210,700,49,normalFont,MUTED);
    text(dc,L"ЖУРНАЛ ПРОВЕРКИ",32,365,350,22,smallFont,MUTED);
    text(dc,L"Фикс меняет только адрес tr.rbxcdn.com в hosts и сохраняет копию.",32,587,736,23,smallFont,MUTED);
    text(dc,L"Windows 10 / 11 · x64    |    v1.0    |    Без установки",32,619,700,24,smallFont,MUTED);
}
void buttonDraw(const DRAWITEMSTRUCT* d){
    bool enabled=IsWindowEnabled(d->hwndItem);bool pressed=(d->itemState&ODS_SELECTED)!=0;bool primary=d->CtlID==FIX;
    COLORREF fill=primary?(enabled?(pressed?RGB(68,191,158):ACCENT):RGB(45,86,81)):(pressed?RGB(49,64,86):RGB(34,45,63));
    auto brush=CreateSolidBrush(fill);auto oldBrush=SelectObject(d->hDC,brush);auto oldPen=SelectObject(d->hDC,GetStockObject(NULL_PEN));
    RoundRect(d->hDC,d->rcItem.left,d->rcItem.top,d->rcItem.right,d->rcItem.bottom,px(10),px(10));SelectObject(d->hDC,oldBrush);SelectObject(d->hDC,oldPen);DeleteObject(brush);
    wchar_t s[120];GetWindowTextW(d->hwndItem,s,120);SetBkMode(d->hDC,TRANSPARENT);SetTextColor(d->hDC,enabled?(primary?RGB(11,38,35):FG):RGB(116,134,151));SelectObject(d->hDC,d->CtlID>=FOLDER?smallFont:boldFont);
    RECT r=d->rcItem;DrawTextW(d->hDC,s,-1,&r,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    if(d->itemState&ODS_FOCUS){InflateRect(&r,-px(4),-px(4));DrawFocusRect(d->hDC,&r);}
}
void addButton(int id,const wchar_t* label,int x,int y,int w,int h){
    HWND control=CreateWindowW(L"BUTTON",label,WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,px(x),px(y),px(w),px(h),windowHandle,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),GetModuleHandleW(nullptr),nullptr);
    SendMessageW(control,WM_SETFONT,reinterpret_cast<WPARAM>(normalFont),TRUE);
}
LRESULT CALLBACK windowProc(HWND h,UINT msg,WPARAM wp,LPARAM lp){
    switch(msg){
    case WM_CREATE:{
        windowHandle=h;
        addButton(FIX,L"Исправить иконки",32,292,264,51);addButton(CHECK,L"Проверить",308,292,214,51);addButton(UNDO,L"Отменить фикс",534,292,234,51);
        addButton(FOLDER,L"Открыть журнал",484,360,164,30);addButton(CANCEL,L"Стоп",660,360,108,30);EnableWindow(GetDlgItem(h,CANCEL),FALSE);
        progress=CreateWindowExW(0,PROGRESS_CLASSW,L"",WS_CHILD|WS_VISIBLE|PBS_MARQUEE,px(32),px(349),px(736),px(3),h,nullptr,GetModuleHandleW(nullptr),nullptr);
        logEdit=CreateWindowExW(0,L"EDIT",L"",WS_CHILD|WS_VISIBLE|WS_VSCROLL|WS_TABSTOP|ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL,px(32),px(402),px(736),px(164),h,nullptr,GetModuleHandleW(nullptr),nullptr);
        SendMessageW(logEdit,WM_SETFONT,reinterpret_cast<WPARAM>(monoFont),TRUE);SendMessageW(logEdit,EM_SETLIMITTEXT,200000,0);
        return 0;
    }
    case WM_PAINT:{PAINTSTRUCT p;HDC dc=BeginPaint(h,&p);paint(dc);EndPaint(h,&p);return 0;}
    case WM_ERASEBKGND:return 1;
    case WM_DRAWITEM:buttonDraw(reinterpret_cast<DRAWITEMSTRUCT*>(lp));return TRUE;
    case WM_CTLCOLOREDIT:case WM_CTLCOLORSTATIC:if(reinterpret_cast<HWND>(lp)==logEdit){SetTextColor(reinterpret_cast<HDC>(wp),FG);SetBkColor(reinterpret_cast<HDC>(wp),PANEL);return reinterpret_cast<LRESULT>(logBackground);}break;
    case WM_COMMAND:
        switch(LOWORD(wp)){
        case FIX:case CHECK:case UNDO:startJob(LOWORD(wp));return 0;
        case CANCEL:if(busy&&!critical.load()){cancelled=true;EnableWindow(GetDlgItem(h,CANCEL),FALSE);statusTitle=L"Останавливаю проверку…";InvalidateRect(h,nullptr,FALSE);}return 0;
        case FOLDER:{
            // Explorer is a fixed system executable; a local folder is the only argument.
            wchar_t wd[MAX_PATH]{};GetWindowsDirectoryW(wd,MAX_PATH);auto explorer=std::wstring(wd)+L"\\explorer.exe";
            if(GetFileAttributesW(backupDir.c_str())!=INVALID_FILE_ATTRIBUTES)ShellExecuteW(h,L"open",explorer.c_str(),quote(backupDir).c_str(),nullptr,SW_SHOWNORMAL);
            else MessageBoxW(h,L"Журнал пока не создан. Его текст можно выделить и скопировать прямо из окна.",L"Журнал",MB_OK|MB_ICONINFORMATION);
            return 0;
        }}break;
    case LOG_MESSAGE:{auto* s=reinterpret_cast<std::wstring*>(lp);int end=GetWindowTextLengthW(logEdit);SendMessageW(logEdit,EM_SETSEL,end,end);SendMessageW(logEdit,EM_REPLACESEL,FALSE,reinterpret_cast<LPARAM>(s->c_str()));delete s;return 0;}
    case COMMIT_MESSAGE:EnableWindow(GetDlgItem(h,CANCEL),FALSE);statusTitle=L"Применяю и проверяю результат…";statusDetail=L"Резервная копия сохранит прежние настройки.";InvalidateRect(h,nullptr,FALSE);return 0;
    case DONE_MESSAGE:{
        auto* d=reinterpret_cast<Done*>(lp);if(job.joinable())job.join();busy=false;statusTitle=d->title;statusDetail=d->detail;statusKind=d->kind;delete d;
        for(int id:{FIX,CHECK,UNDO})EnableWindow(GetDlgItem(h,id),TRUE);
        EnableWindow(GetDlgItem(h,CANCEL),FALSE);SendMessageW(progress,PBM_SETMARQUEE,FALSE,0);InvalidateRect(h,nullptr,FALSE);
        if(closeAfter)DestroyWindow(h);
        return 0;
    }
    case WM_CLOSE:
        if(busy){closeAfter=true;if(!critical.load())cancelled=true;statusDetail=L"Завершаю текущее действие перед закрытием окна.";InvalidateRect(h,nullptr,FALSE);return 0;}DestroyWindow(h);return 0;
    case WM_DESTROY:PostQuitMessage(0);return 0;
    }return DefWindowProcW(h,msg,wp,lp);
}
HFONT makeFont(int pt,int weight,const wchar_t* face=L"Segoe UI"){return CreateFontW(-MulDiv(pt,dpi,72),0,0,0,weight,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,face);}
} // namespace

int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int show){
    // Do not search the executable directory for optional DLLs.
    SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32);
    Handle mutex(CreateMutexW(nullptr,TRUE,L"Global\\RobloxIconFix_v1_2026"));
    if(!mutex.valid()||GetLastError()==ERROR_ALREADY_EXISTS){MessageBoxW(nullptr,L"Утилита уже открыта. Используй существующее окно.",L"Roblox Icon Fix",MB_OK|MB_ICONINFORMATION);return 0;}
    SetProcessDPIAware();HDC screen=GetDC(nullptr);dpi=GetDeviceCaps(screen,LOGPIXELSX);ReleaseDC(nullptr,screen);
    wchar_t dir[MAX_PATH]{};if(!GetSystemDirectoryW(dir,MAX_PATH))return 1;systemDir=dir;hostsPath=systemDir+L"\\drivers\\etc\\hosts";backupDir=systemDir+L"\\drivers\\etc\\RobloxIconFix-backups";
    normalFont=makeFont(11,FW_NORMAL);smallFont=makeFont(10,FW_NORMAL);titleFont=makeFont(25,FW_BOLD);boldFont=makeFont(12,FW_SEMIBOLD);monoFont=makeFont(9,FW_NORMAL,L"Consolas");background=CreateSolidBrush(BG);logBackground=CreateSolidBrush(PANEL);
    INITCOMMONCONTROLSEX ic{sizeof(ic),ICC_PROGRESS_CLASS|ICC_STANDARD_CLASSES};InitCommonControlsEx(&ic);
    WNDCLASSEXW wc{};wc.cbSize=sizeof(wc);wc.hInstance=instance;wc.lpfnWndProc=windowProc;wc.lpszClassName=L"RobloxIconFixWindow";wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.hIcon=LoadIconW(instance,MAKEINTRESOURCEW(101));wc.hIconSm=wc.hIcon;wc.hbrBackground=background;RegisterClassExW(&wc);
    DWORD style=WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX|WS_CLIPCHILDREN;RECT r{0,0,px(800),px(666)};AdjustWindowRectEx(&r,style,FALSE,0);
    RECT desktop{};SystemParametersInfoW(SPI_GETWORKAREA,0,&desktop,0);int x=std::max(desktop.left,(desktop.right-(r.right-r.left))/2),y=std::max(desktop.top,(desktop.bottom-(r.bottom-r.top))/2);
    windowHandle=CreateWindowExW(0,wc.lpszClassName,L"Roblox Icon Fix — восстановление иконок",style,x,y,r.right-r.left,r.bottom-r.top,nullptr,nullptr,instance,nullptr);if(!windowHandle)return 1;
    ShowWindow(windowHandle,show);UpdateWindow(windowHandle);
    try{safeDirectory(backupDir);logPath=backupDir+L"\\log_"+stamp()+L"_"+std::to_wstring(GetCurrentProcessId())+L".txt";}catch(const std::exception& e){logLine(wide(e.what()));}
    logLine(L"Roblox Icon Fix 1.0 · Windows x64");logLine(L"Проверяется только сервер картинок tr.rbxcdn.com.");logLine(L"«Исправить» применяет адрес, «Отменить фикс» возвращает прежние записи.");
    if(!IsUserAnAdmin())logLine(L"Нет прав администратора. Для изменения hosts запусти .exe от имени администратора.");
    MSG msg{};while(GetMessageW(&msg,nullptr,0,0)>0){if(!IsDialogMessageW(windowHandle,&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}}
    if(job.joinable())job.join();
    windowHandle=nullptr;
    for(auto font:{normalFont,titleFont,boldFont,monoFont,smallFont})DeleteObject(font);
    DeleteObject(background);DeleteObject(logBackground);return 0;
}
