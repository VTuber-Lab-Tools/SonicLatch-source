#include "qt_metrics.hpp"
#include <QApplication>
#include <iostream>
#include <stdexcept>
int main(int argc,char** argv){QApplication app(argc,argv);try{
 QWidget root;auto* layout=new QGridLayout(&root);pitatuner::qt::Metrics metrics(&root,layout,[](pitatuner::qt::Text t){using T=pitatuner::qt::Text;if(t==T::Protected)return QStringLiteral("Selection unavailable");if(t==T::Waiting)return QStringLiteral("Measuring");if(t==T::Unknown)return QStringLiteral("Unknown");if(t==T::Milliseconds)return QStringLiteral("%1 ms");return QString::number(int(t));});unsigned checks=0;auto check=[&](bool ok){++checks;if(!ok)throw std::runtime_error("selection rejection UI regression");};using T=pitatuner::qt::Text;
 metrics.showUnbound(false);check(metrics.stateIs(T::NoSource));
 // Reproduce repeated owner clear/draw turns after a rejected source selection.
 for(int tick=0;tick<3;++tick){metrics.update({});metrics.authority(true,true,true);metrics.showUnbound(true);app.processEvents();check(metrics.stateIs(T::Protected));check(!metrics.stateIs(T::Waiting));check(metrics.writesDisabled());for(size_t i=0;i<4;++i)check(metrics.value(i)==pitatuner::qt::nsValue({}));}
 metrics.showUnbound(false);check(metrics.stateIs(T::NoSource));check(metrics.writesDisabled());check(metrics.unknown());
 std::cout<<checks<<" selection-state Qt checks PASS; no OBS or audio\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
