#pragma once
// Declaration-only shim for the pure callback/worker replay. No Windows calls.
using HMODULE=void*;
void* GetProcAddress(HMODULE,const char*);
