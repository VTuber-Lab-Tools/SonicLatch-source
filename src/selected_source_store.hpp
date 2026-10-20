#pragma once
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSignalBlocker>
#include <functional>

namespace pitatuner::selection {
inline bool validUuid(const QString& uuid){
 if(uuid.size()!=36)return false;
 for(int i=0;i<uuid.size();i++){
  const auto c=uuid[i].unicode();if(i==8||i==13||i==18||i==23){if(c!='-')return false;}
  else if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')||(c>='A'&&c<='F')))return false;
 }
 return true;
}
enum class Status {Missing,Ready,Invalid,IoError};
struct Loaded {Status status=Status::Missing;QString uuid;};
// UI-thread only, one dedicated module config file. Unknown JSON fields survive
// an explicit successful selection. Invalid/excessive/unknown-schema data is
// never overwritten. Atomic replace prevents an interrupted partial JSON save.
class Store {
 QString path_;
 Status document(QJsonObject& object)const{
  if(path_.isEmpty())return Status::IoError;
  QFile file(path_);if(!file.exists())return Status::Missing;
  if(!file.open(QIODevice::ReadOnly))return Status::IoError;
  if(file.size()>65536)return Status::Invalid;
  QJsonParseError error;const auto doc=QJsonDocument::fromJson(file.readAll(),&error);
  if(error.error!=QJsonParseError::NoError||!doc.isObject())return Status::Invalid;
  object=doc.object();if(object.value("schema")!=QJsonValue(1))return Status::Invalid;
  const auto saved=object.value("selected_source_uuid");
  if(!saved.isUndefined()&&(!saved.isString()||!validUuid(saved.toString())))return Status::Invalid;
  return Status::Ready;
 }
public:
 explicit Store(QString path):path_(std::move(path)){}
 Loaded load()const{QJsonObject object;const auto status=document(object);return{status,status==Status::Ready?object.value("selected_source_uuid").toString():QString{}};}
 bool save(const QString& uuid)const{
  if(!validUuid(uuid))return false;
  QJsonObject object;const auto status=document(object);if(status!=Status::Missing&&status!=Status::Ready)return false;
  if(status==Status::Ready&&object.value("selected_source_uuid").toString()==uuid)return true;
  object.insert("schema",1);object.insert("selected_source_uuid",uuid);
  if(!QDir().mkpath(QFileInfo(path_).absolutePath()))return false;
  QSaveFile file(path_);file.setDirectWriteFallback(false);if(!file.open(QIODevice::WriteOnly))return false;
  const auto json=QJsonDocument(object).toJson(QJsonDocument::Indented);
  if(file.write(json)!=json.size()){file.cancelWriting();return false;}return file.commit();
 }
};
enum class Restore {NoSavedSelection,InvalidStorage,MissingSource,SelectionFailed,Restored};
// UUID item data only; display names are never consulted. Block the normal user
// selection signal so loading does not save/overwrite configuration. The caller
// uses the normal capture select/getter-read path, never any offset action.
inline Restore restore(QComboBox& picker,const Store& store,const std::function<bool(const QString&)>& select){
 const auto loaded=store.load();if(loaded.status==Status::Invalid||loaded.status==Status::IoError)return Restore::InvalidStorage;
 if(loaded.uuid.isEmpty())return Restore::NoSavedSelection;
 const auto row=picker.findData(loaded.uuid,Qt::UserRole,Qt::MatchExactly|Qt::MatchCaseSensitive);
 if(row<0)return Restore::MissingSource;
 QSignalBlocker block(picker);picker.setCurrentIndex(row);
 if(!select(loaded.uuid)){picker.setCurrentIndex(-1);return Restore::SelectionFailed;}
 return Restore::Restored;
}
inline const char* code(Restore status){switch(status){case Restore::NoSavedSelection:return "NO_SAVED_SELECTION";case Restore::InvalidStorage:return "INVALID_STORAGE";case Restore::MissingSource:return "MISSING_SOURCE";case Restore::SelectionFailed:return "SELECTION_FAILED";case Restore::Restored:return "RESTORED";}return "INVALID";}
}
