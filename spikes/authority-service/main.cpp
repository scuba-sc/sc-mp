// SPDX-License-Identifier: GPL-3.0-only
// Host-side synthetic fixture. Session control never crosses into the plugin.
#include "fixture.h"
#include "sco/app.h"
#include "sco/net/session.h"
#include <charconv>
#include <chrono>
#include <csignal>
#include <fcntl.h>
#include <random>
#include <sstream>
#include <string>
#include <unistd.h>
#include <poll.h>
static uint64_t now() {return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());}
static std::string epoch() {std::random_device r;static constexpr char hex[]="0123456789abcdef";for(int a=0;a<4;++a){std::string s;for(int i=0;i<16;++i){auto b=static_cast<unsigned>(r())&255U;s+=hex[b>>4];s+=hex[b&15];}if(s!=std::string(32,'0'))return s;}return {};}
template<class T> static bool number(const std::string& s,T& v){if(s.empty())return false;auto r=std::from_chars(s.data(),s.data()+s.size(),v);return r.ec==std::errc{}&&r.ptr==s.data()+s.size();}
static void emit(const char* kind,const char* status,const char* ep,uint64_t peer,uint64_t entity,int64_t value,uint64_t rev,uint64_t id) {std::ostringstream o;o<<"{\"kind\":\""<<kind<<"\",\"status\":\""<<status<<"\",\"epoch\":\""<<ep<<"\",\"world\":\""<<ep<<"\",\"peer\":"<<peer<<",\"entity\":"<<entity<<",\"value\":"<<value<<",\"revision\":"<<rev<<",\"id\":"<<id<<"}\n";auto line=o.str();if(write(STDOUT_FILENO,line.data(),line.size())!=static_cast<ssize_t>(line.size()))_exit(2);}
int main(int argc,char** argv){
 std::signal(SIGPIPE,SIG_IGN);int flags=fcntl(STDOUT_FILENO,F_GETFL);if(flags<0||fcntl(STDOUT_FILENO,F_SETFL,flags|O_NONBLOCK)<0)return 2;
 if(argc<2||argc>3)return 1;std::string mode=argv[1];if(mode=="sc-world"){emit("error","Unavailable","",0,0,0,0,0);return 1;}
 bool solo=mode=="solo",listen=mode=="listen",dedicated=mode=="dedicated",client=mode=="client";if(!(solo||listen||dedicated||client)||(solo?argc!=2:argc!=3))return 1;unsigned port=0;if(!solo&&(!number(std::string(argv[2]),port)||!port||port>65535))return 1;
 auto ep=epoch();if(ep.empty())return 2;
 static const sco::plugins::Builtin builtin{"authority",authority_query,authority_load,authority_unload};sco::app::Platform pf;pf.hostVersion="authority fixture";pf.pluginsEnabled=false;pf.builtins=&builtin;pf.nBuiltins=1;pf.privateSession.enabled=false;
 if(!sco::app::Start(pf))return 1;const FixtureControl control=*fixture_control();if(control.size<sizeof(control)||!control.loaded()||!control.configure(!client,solo||listen,ep.c_str(),emit)){sco::app::Stop();return 1;}
 sco::Result result=sco::Result::Ok;
 if(!solo){if(!client){sco::net::HostOptions o;o.port=static_cast<uint16_t>(port);o.playerName="server";o.passphrase="synthetic fixture only";o.maxPeers=16;result=sco::net::Host(o);}else{sco::net::JoinOptions o;o.address="127.0.0.1";o.port=static_cast<uint16_t>(port);o.playerName="client";o.passphrase="synthetic fixture only";result=sco::net::Join(o);}}
 if(result!=sco::Result::Ok){emit("error","SetupFailed","",0,0,0,0,0);sco::app::Stop();return 1;}
 if(solo||listen)emit("context","Ok",ep.c_str(),1,1,0,0,0);else if(dedicated)emit("ready","Ok",ep.c_str(),0,0,0,0,0);else emit("ready","Joining","",0,0,0,0,0);
 std::string buffer;bool overflow=false,done=false;while(!done){sco::app::Tick(static_cast<uint32_t>(now()));control.tick(now());pollfd fd{STDIN_FILENO,POLLIN,0};int rc=poll(&fd,1,10);if(rc<0)break;if(rc>0&&(fd.revents&(POLLIN|POLLHUP))){char bytes[256];auto n=read(STDIN_FILENO,bytes,sizeof(bytes));if(n<=0)break;for(ssize_t i=0;i<n;++i){char c=bytes[i];if(c=='\n'){if(overflow)emit("error","InvalidInput","",0,0,0,0,0);else if(buffer=="quit"||buffer=="disconnect")done=true;else control.submit(buffer.c_str());buffer.clear();overflow=false;if(done)break;}else if(buffer.size()<256&&!overflow&&c!='\0'&&c!='\r')buffer+=c;else overflow=true;}}}
 sco::app::Stop();return control.loaded()?2:0;
}
