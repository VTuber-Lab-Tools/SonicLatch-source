#pragma once
// Compiled only into the opt-in isolated43 QA module; never normal capture.
namespace pitatuner::capture::lifetimeqa {
enum class Point {PreLease,PostLease,WorkerEnter,WorkerExit,GateClosed,DoneSet,WorkerJoined,SessionDestroyed};
inline void(*observer)(Point,const void*)noexcept=nullptr;
inline void point(Point p,const void* session)noexcept{if(observer)observer(p,session);}
struct WorkerScope {const void* session;explicit WorkerScope(const void* p):session(p){point(Point::WorkerEnter,session);}~WorkerScope(){point(Point::WorkerExit,session);}};
}
