#pragma once
#include "product_transaction.hpp"
#include <sstream>

namespace soniclatch::product {
// Product helpers receive operation/transaction/plan identity ONLY. A caller-
// supplied root path is never accepted through this IPC contract.
struct Request {Operation operation;std::string transactionId, planDigest;};
inline Request parseRequest(const std::vector<std::string>& args) {
 need(args.size()==6&&args[0]=="--operation"&&args[2]=="--transaction"&&args[4]=="--plan-sha256","unsupported helper arguments");
 Operation op;
 if(args[1]=="install")op=Operation::Install;
 else if(args[1]=="upgrade")op=Operation::Upgrade;
 else if(args[1]=="repair")op=Operation::Repair;
 else if(args[1]=="uninstall")op=Operation::Uninstall;
 else throw std::runtime_error("unsupported operation");
 need(hex(args[3],32)&&hex(args[5],64),"invalid bounded IPC identity");return {op,args[3],args[5]};
}
inline void matchRequest(const Request& request,const Plan& plan,const std::string& digest) {
 need(request.transactionId==plan.transactionId&&request.operation==plan.operation&&request.planDigest==digest,"request/persisted plan identity mismatch");
}
// Payload/receipt commit is NOT whole-installer success: registration, bootstrap
// cleanup and management completion remain separate native integration work.
inline std::string blockedResult(const Request& r) {
 need(hex(r.transactionId,32)&&hex(r.planDigest,64),"invalid IPC result identity");
 return "{\"schema\":1,\"transactionId\":\""+r.transactionId+"\",\"planSha256\":\""+r.planDigest+"\",\"code\":90,\"state\":\"DRAFT_BLOCKED\",\"publicationAuthorized\":false}\r\n";
}
// This header deliberately does not declare a success-producing process entry.
// NSIS must compare the exact transaction/plan and code against a bounded result;
// never accept exit 0 or an arbitrary `state` string on its own.
} // namespace soniclatch::product
