// Read-only replay of a copied plan/journal/payload. No WindowsStore/root override.
#include "product_windows_store.hpp"
#include <fstream>
#include <iostream>
using namespace soniclatch::product;
using namespace soniclatch::product::win;
Bytes load(const fs::path& path){std::ifstream f(path,std::ios::binary);need(bool(f),"fixture input missing");Bytes b{std::istreambuf_iterator<char>(f),std::istreambuf_iterator<char>()};need(b.size()<=fileLimit,"fixture bound");return b;}
struct CopiedStore final:Store {
 fs::path root;Bytes plan,journal,receipt;Plan p;std::string digest;unsigned writes{};
 explicit CopiedStore(fs::path input):root(std::move(input)),plan(load(root/L"plan.bin")),journal(load(root/L"journal.bin")),receipt(load(root/L"receipt.bin")),p(decodePlan(plan)),digest(hashBytes(plan)){}
 std::string sha256(const Bytes& b)const override{return hashBytes(b);}
 void guard(const Plan& value)const override{need(encode(value)==plan,"copied plan drift");}
 void authorize(const Plan& value,const std::string& d)const override{guard(value);need(d==digest,"copied digest drift");} // fixture anchor only, no product authority claim
 std::optional<Bytes> readOwned(const std::string& name)const override{need(safePath(name),"fixture owned path");return load(root/L"payload"/fs::path(name));}
 std::optional<Bytes> readReceipt()const override{return receipt;}
 Bytes readPlan()const override{return plan;}
 Bytes readJournal()const override{return journal;}
 Bytes readBlob(const std::string& name)const override{need(hex(name,64),"fixture blob digest");return load(root/L"blobs"/fs::path(name));}
 void mutation(){++writes;throw std::runtime_error("unexpected mutation in committed replay");}
 void createPrepared(const Bytes&,const std::map<std::string,Bytes>&,const Bytes&)override{mutation();}
 void writeJournalDurable(const Bytes&)override{mutation();}
 void changeOwned(const std::string&,const Image&,const std::optional<Bytes>&)override{mutation();}
 void changeReceipt(const Image&,const std::optional<Bytes>&)override{mutation();}
};
int wmain(int argc,wchar_t** argv){try{need(argc==2,"copied fixture directory required");CopiedStore s(argv[1]);need(decodeJournal(s.journal,s.p,s.digest).state==State::Committed,"actual state must remain committed");auto unknown=load(s.root/L"unknown-attempt-temp.bin"),active=load(s.root/L"active.bin");need(unknown.size()==8686,"reported unknown temp size");auto result=Engine(s).recover();need(result.payloadReceiptCommitted&&!result.rolledBack&&s.writes==0,"committed replay must preserve installed payload");need(load(s.root/L"unknown-attempt-temp.bin")==unknown&&load(s.root/L"active.bin")==active,"unknown attempt/active retained");unsigned before{};for(const auto& step:s.p.steps)if(step.before)++before;need(before==0,"actual transaction has no legacy before images");std::cout<<"copiedActualTransaction="<<s.p.transactionId<<" committedRecovery=PASS writes=0 payload="<<s.p.steps.size()<<" beforeImages="<<before<<" unknownPlanlessTempPreserved=true activePreserved=true; read-only fixture replay, NOT Setup/WindowsStore recovery\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}
