// Fixture only: invoke exact production receipt lookup, never product run().
#define wmain product_entry_never_called
#include "product_main.cpp"
#undef wmain
#include <fstream>
int wmain(int argc,wchar_t** argv){try{need(argc==4,"copied receipt/root/expected required");std::ifstream f(argv[1],std::ios::binary);need(bool(f),"copied receipt missing");Bytes raw{std::istreambuf_iterator<char>(f),std::istreambuf_iterator<char>()};std::wstring rootWide=argv[2];std::string root(rootWide.begin(),rootWide.end());bool accepted=false;try{const auto* prior=oldRelease(raw,root);need(prior!=nullptr,"lookup null");accepted=true;}catch(const std::exception& e){std::cout<<"PRODUCTION_LOOKUP_ERROR="<<e.what()<<"\n";}need(accepted==(std::wstring(argv[3])==L"accepted"),"lookup outcome");std::cout<<(accepted?"PASS exact production lookup accepted authenticated copied receipt\n":"PASS reproduced exact production rejection; no Store/native mutation invoked\n");return 0;}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}
