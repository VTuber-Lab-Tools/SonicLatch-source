#include "product_registration.hpp"
#include <iostream>
using namespace soniclatch::product;
using namespace soniclatch::product::win;
int main(){try{
 unsigned assertions{};
 const auto check=[&](bool ok,const char* why){need(ok,why);++assertions;};
 const auto registry=untrustedMutationMask(SE_REGISTRY_KEY);
 check(!(registry&KEY_READ),"KEY_READ must remain readable");
 check(!(registry&KEY_NOTIFY),"KEY_NOTIFY is not a mutation right");
 check(!(registry&KEY_WOW64_64KEY),"64-bit view selector is not a mutation right");
 check(!(registry&KEY_WOW64_32KEY),"32-bit view selector is not a mutation right");
 for(DWORD right:std::array<DWORD,9>{KEY_SET_VALUE,KEY_CREATE_SUB_KEY,KEY_CREATE_LINK,DELETE,WRITE_DAC,WRITE_OWNER,GENERIC_WRITE,GENERIC_ALL,MAXIMUM_ALLOWED})check((registry&right)==right,"registry mutation must remain rejected");
 check((registry&0x3001f)!=0,"observed untrusted writable ACE must remain rejected");
 check(untrustedMutationMask(SE_FILE_OBJECT)==0x520d0156,"managed file mask must remain unchanged");
 check(untrustedMutationMask(SE_FILE_OBJECT,true)==0x120d0040,"ancestor file mask must remain unchanged");
 bool unknownRejected=false;try{untrustedMutationMask(SE_SERVICE);}catch(...){unknownRejected=true;}check(unknownRejected,"unknown object type must fail closed");
 NewRegistrationSecurity security;
 check(security.attributes.nLength==sizeof(SECURITY_ATTRIBUTES)&&security.attributes.lpSecurityDescriptor==security.descriptor&&!security.attributes.bInheritHandle,"creation attributes");
 check(IsValidSecurityDescriptor(security.descriptor)!=FALSE,"valid new key descriptor");
 SECURITY_DESCRIPTOR_CONTROL control{};DWORD revision{};
 check(GetSecurityDescriptorControl(security.descriptor,&control,&revision)!=FALSE&&(control&SE_DACL_PROTECTED),"new key DACL must block inherited untrusted writes");
 PSID owner{};BOOL defaulted{};check(GetSecurityDescriptorOwner(security.descriptor,&owner,&defaulted)!=FALSE,"new key owner");
 std::array<BYTE,SECURITY_MAX_SID_SIZE> admins{},system{},users{};DWORD as=static_cast<DWORD>(admins.size()),ss=static_cast<DWORD>(system.size()),us=static_cast<DWORD>(users.size());
 check(CreateWellKnownSid(WinBuiltinAdministratorsSid,nullptr,admins.data(),&as)&&CreateWellKnownSid(WinLocalSystemSid,nullptr,system.data(),&ss)&&CreateWellKnownSid(WinBuiltinUsersSid,nullptr,users.data(),&us),"well known SIDs");
 check(EqualSid(owner,admins.data())!=FALSE,"new key administrator owner");
 PACL acl{};BOOL present{};check(GetSecurityDescriptorDacl(security.descriptor,&present,&acl,&defaulted)!=FALSE&&present&&acl&&acl->AceCount==3,"three explicit non-null key ACEs");
 unsigned trustedWrites{},userReads{};
 for(DWORD i=0;i<acl->AceCount;++i){void* raw{};check(GetAce(acl,i,&raw)!=FALSE,"valid ACE");auto* ace=static_cast<ACCESS_ALLOWED_ACE*>(raw);check(ace->Header.AceType==ACCESS_ALLOWED_ACE_TYPE&&ace->Header.AceFlags==0,"explicit allow ACE");PSID sid=&ace->SidStart;
  if(EqualSid(sid,admins.data())||EqualSid(sid,system.data())){check(ace->Mask==KEY_ALL_ACCESS,"trusted key all access");++trustedWrites;}
  else {check(EqualSid(sid,users.data())&&ace->Mask==KEY_READ&&!(ace->Mask&registry),"users key read only");++userReads;}
 }
 check(trustedWrites==2&&userReads==1,"no extra write principals");
 std::cout<<"PASS "<<assertions<<" assertions; memory-only; no key creation or ACL changes\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}
