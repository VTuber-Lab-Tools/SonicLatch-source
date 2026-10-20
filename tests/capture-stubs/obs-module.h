#pragma once
// Declaration-only types used to compile the production callback path on Linux.
// No OBS implementation, source, device, filter registration or runtime is used.
#include <cstddef>
#include <cstdint>
struct obs_source_t;struct obs_data_t;struct signal_handler_t;struct calldata_t;
struct obs_audio_data {uint8_t* data[8]{};uint32_t frames{};uint64_t timestamp{};};
struct audio_data {uint8_t* data[8]{};uint32_t frames{};uint64_t timestamp{};};
enum obs_source_type {OBS_SOURCE_TYPE_INPUT,OBS_SOURCE_TYPE_FILTER};
enum obs_order_movement {OBS_ORDER_MOVE_TOP};
enum speaker_layout {SPEAKERS_STEREO};
inline constexpr uint32_t OBS_SOURCE_AUDIO=2;
struct obs_audio_info {uint32_t samples_per_sec{};speaker_layout speakers{};};
struct obs_source_info {const char* id{};obs_source_type type{};uint32_t output_flags{};const char*(*get_name)(void*){};void*(*create)(obs_data_t*,obs_source_t*){};void(*destroy)(void*){};obs_audio_data*(*filter_audio)(void*,obs_audio_data*){};};
void obs_source_enum_filters(obs_source_t*,void(*)(obs_source_t*,obs_source_t*,void*),void*);
void obs_enum_sources(bool(*)(void*,obs_source_t*),void*);
obs_source_t* obs_get_source_by_uuid(const char*);obs_source_t* obs_source_get_ref(obs_source_t*);void obs_source_release(obs_source_t*);
const char* obs_source_get_uuid(obs_source_t*);const char* obs_source_get_name(obs_source_t*);const char* obs_source_get_id(obs_source_t*);
uint32_t obs_source_get_output_flags(obs_source_t*);obs_source_type obs_source_get_type(obs_source_t*);
bool obs_source_removed(obs_source_t*);bool obs_source_muted(obs_source_t*);bool obs_source_enabled(obs_source_t*);
void obs_register_source_s(const obs_source_info*,size_t);obs_source_t* obs_source_create_private(const char*,const char*,obs_data_t*);
void obs_source_filter_add(obs_source_t*,obs_source_t*);void obs_source_filter_remove(obs_source_t*,obs_source_t*);void obs_source_filter_set_order(obs_source_t*,obs_source_t*,obs_order_movement);
int obs_source_filter_get_index(obs_source_t*,obs_source_t*);size_t obs_source_filter_count(obs_source_t*);
void obs_source_add_audio_capture_callback(obs_source_t*,void(*)(void*,obs_source_t*,const audio_data*,bool),void*);
void obs_source_remove_audio_capture_callback(obs_source_t*,void(*)(void*,obs_source_t*,const audio_data*,bool),void*);
signal_handler_t* obs_source_get_signal_handler(obs_source_t*);
void signal_handler_connect(signal_handler_t*,const char*,void(*)(void*,calldata_t*),void*);void signal_handler_disconnect(signal_handler_t*,const char*,void(*)(void*,calldata_t*),void*);
bool obs_get_audio_info(obs_audio_info*);
