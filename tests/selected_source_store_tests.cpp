#include "selected_source_store.hpp"
#include <QApplication>
#include <QTemporaryDir>
#include <iostream>
#include <stdexcept>
using namespace pitatuner::selection;
int main(int argc,char** argv){QApplication app(argc,argv);unsigned checks=0;auto check=[&](bool b){++checks;if(!b)throw std::runtime_error("UUID save/restore check="+std::to_string(checks));};try{
 const QString a="5ab2bd9c-9801-42fd-93f9-a385a6d2c34f",b="68cfcc22-4f9a-4fa0-a27c-14c989bd20c2";QTemporaryDir dir(QDir::currentPath()+"/selection-store-test-XXXXXX");check(dir.isValid());const auto path=dir.path()+"/plugin/selected-source.json";Store store(path);
 check(store.load().status==Status::Missing);check(!store.save("same microphone name"));check(!QFile::exists(path));check(validUuid(a)&&!validUuid(a+" ")&&!validUuid("../"));const bool firstSaved=store.save(a);if(!firstSaved){std::cerr<<"initial save failed; path="<<path.toStdString()<<" parentExists="<<QDir(QFileInfo(path).absolutePath()).exists()<<" status="<<static_cast<int>(store.load().status)<<'\n';QSaveFile probe(dir.path()+"/atomic-probe.json");const bool opened=probe.open(QIODevice::WriteOnly);std::cerr<<"probeOpen="<<opened<<" error="<<probe.errorString().toStdString()<<'\n';if(opened){probe.write("{}");const bool committed=probe.commit();std::cerr<<"probeCommit="<<committed<<" error="<<probe.errorString().toStdString()<<'\n';}}check(firstSaved);check(store.load().uuid==a);
 auto bytes=[&](){QFile file(path);check(file.open(QIODevice::ReadOnly));return file.readAll();};auto write=[&](const QByteArray& value){QFile file(path);check(file.open(QIODevice::WriteOnly));check(file.write(value)==value.size());};
 const auto initial=bytes();Store restarted(path);check(restarted.load().uuid==a);QComboBox picker;picker.addItem("same mic",b);picker.addItem("same mic",a);picker.setCurrentIndex(-1);unsigned signalCount=0,selectCalls=0;QString selected;QObject::connect(&picker,qOverload<int>(&QComboBox::currentIndexChanged),[&](int){++signalCount;});
 auto choose=[&](const QString& uuid){++selectCalls;selected=uuid;return uuid==a;};check(restore(picker,restarted,choose)==Restore::Restored);check(picker.currentIndex()==1&&selected==a&&selectCalls==1&&signalCount==0);check(bytes()==initial);
 // A second OBS lifetime restores the same UUID; loading alone does not save.
 picker.setCurrentIndex(-1);signalCount=0;selected.clear();Store restartedAgain(path);check(restore(picker,restartedAgain,choose)==Restore::Restored&&selected==a&&signalCount==0);check(bytes()==initial);
 // Deleted source / same-name replacement: no fallback and no lookup callback.
 picker.clear();picker.addItem("same mic",b);picker.setCurrentIndex(-1);selectCalls=0;signalCount=0;check(restore(picker,restartedAgain,choose)==Restore::MissingSource);check(picker.currentIndex()==-1&&selectCalls==0&&signalCount==0&&store.load().uuid==a);check(bytes()==initial);
 // Source removed after enumeration: capture selection rejection stays empty.
 picker.addItem("same mic",a);picker.setCurrentIndex(-1);signalCount=0;check(restore(picker,store,[&](const QString& uuid){++selectCalls;return false;})==Restore::SelectionFailed);check(picker.currentIndex()==-1&&selectCalls==1&&signalCount==0);check(bytes()==initial);
 // Explicit successful selection changes only the dedicated selected UUID field.
 QJsonObject metadata=QJsonDocument::fromJson(initial).object();metadata.insert("future_setting",QJsonObject{{"retain",true}});write(QJsonDocument(metadata).toJson());check(store.save(b));auto saved=QJsonDocument::fromJson(bytes()).object();check(saved.value("selected_source_uuid").toString()==b&&saved.value("future_setting")==metadata.value("future_setting"));
 check(!store.save(QString(10000,'a')));check(store.load().uuid==b);
 // Corrupt, unknown schema, wrong type and excessive files are preserved exactly.
 for(const QByteArray bad:{QByteArray("{bad json"),QByteArray("[]"),QJsonDocument(QJsonObject{{"schema",1.5},{"selected_source_uuid",a}}).toJson(),QByteArray("{\"schema\":2,\"selected_source_uuid\":\"future\"}"),QByteArray("{\"schema\":1,\"selected_source_uuid\":4}"),QByteArray(65537,' '),QByteArray("{\"schema\":1,\"selected_source_uuid\":\"same mic\"}")}){
  write(bad);check(store.load().status==Status::Invalid);check(!store.save(a));check(bytes()==bad);picker.setCurrentIndex(-1);selectCalls=0;check(restore(picker,store,choose)==Restore::InvalidStorage&&selectCalls==0);
 }
 Store noPath("");check(noPath.load().status==Status::IoError&&!noPath.save(a));Store directory(dir.path());check(directory.load().status!=Status::Ready&&!directory.save(a));
 // Case-sensitive identity: even UUID text case is never substituted by name.
 write(initial);picker.clear();picker.addItem("same mic",a.toUpper());picker.setCurrentIndex(-1);selectCalls=0;check(restore(picker,store,choose)==Restore::MissingSource&&selectCalls==0);
 std::cout<<"PASS "<<checks<<" Qt/temp-file UUID save/restart/exact-identity/deleted-source/unknown-field/corrupt-preservation checks; no OBS or offset setter\n";return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
