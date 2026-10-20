#include <QApplication>
#include <QDockWidget>
#include <QMainWindow>
#include <QMessageBox>
#include <QPushButton>
#include <QTimer>
#include <iostream>
int main(int argc,char** argv){QApplication app(argc,argv);QMainWindow main;QDockWidget dock(&main);QWidget root;dock.setWidget(&root);main.addDockWidget(Qt::LeftDockWidgetArea,&dock);main.show();dock.setFloating(true);dock.show();
 int checks=0;
 for(bool yes:{true,false}){
  QTimer responder;responder.setInterval(20);bool answered=false,timedOut=false;
  const QString title="Independent product reference test",body="Exact test-owned modal body";
  QObject::connect(&responder,&QTimer::timeout,[&]{for(auto* w:QApplication::allWidgets())if(auto* box=qobject_cast<QMessageBox*>(w);box&&box->windowTitle()==title&&box->text()==body){if(auto* b=box->button(yes?QMessageBox::Yes:QMessageBox::No)){answered=true;responder.stop();b->click();}}});
  QTimer watchdog;watchdog.setSingleShot(true);watchdog.setInterval(1000);QObject::connect(&watchdog,&QTimer::timeout,[&]{timedOut=true;for(auto* w:QApplication::allWidgets())if(auto* box=qobject_cast<QMessageBox*>(w);box&&box->windowTitle()==title)box->reject();});
  responder.start();watchdog.start();const auto result=QMessageBox::question(&root,title,body,QMessageBox::Yes|QMessageBox::No,QMessageBox::No);watchdog.stop();
  if(!answered||timedOut||result!=(yes?QMessageBox::Yes:QMessageBox::No)){std::cerr<<"modal result mismatch\n";dock.setWidget(nullptr);return 1;}checks+=3;
 }
 // The watchdog must close an unanswered test-owned question as well.
 QTimer watchdog;watchdog.setSingleShot(true);watchdog.setInterval(50);bool closed=false;QObject::connect(&watchdog,&QTimer::timeout,[&]{for(auto* w:QApplication::allWidgets())if(auto* box=qobject_cast<QMessageBox*>(w);box&&box->windowTitle()=="Watchdog test"){closed=true;box->reject();}});watchdog.start();QMessageBox::question(&root,"Watchdog test","Unanswered fixture question",QMessageBox::Yes|QMessageBox::No,QMessageBox::No);if(!closed){dock.setWidget(nullptr);return 1;}++checks;
 dock.setWidget(nullptr);std::cout<<checks<<" modal harness checks PASS; in-process Qt only\n";return 0;
}
