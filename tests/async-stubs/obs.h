#pragma once
#include <cstddef>
#include <cstdint>
struct obs_source_t;struct signal_handler_t;struct calldata_t;
obs_source_t* obs_source_get_ref(obs_source_t*);void obs_source_release(obs_source_t*);
const char* obs_source_get_uuid(obs_source_t*);bool obs_source_removed(obs_source_t*);
signal_handler_t* obs_source_get_signal_handler(obs_source_t*);
int64_t obs_source_get_sync_offset(obs_source_t*);void obs_source_set_sync_offset(obs_source_t*,int64_t);
void signal_handler_connect(signal_handler_t*,const char*,void(*)(void*,calldata_t*),void*);
void signal_handler_disconnect(signal_handler_t*,const char*,void(*)(void*,calldata_t*),void*);
bool calldata_get_data(calldata_t*,const char*,void*,size_t);
