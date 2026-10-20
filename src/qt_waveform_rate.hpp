#pragma once
#include <QToolButton>
#include <QEvent>
#include <functional>
#include <algorithm>
namespace pitatuner::qt {
class WaveformRateButton final:public QToolButton {
 unsigned fps_{60};std::function<void(unsigned)> changed_;
 void place(){adjustSize();if(parentWidget())move(std::max(0,parentWidget()->width()-width()-8),8);raise();}
protected:
 void changeEvent(QEvent* event)override{QToolButton::changeEvent(event);if(event->type()==QEvent::FontChange||event->type()==QEvent::StyleChange)place();}
 bool eventFilter(QObject* watched,QEvent* event)override{if(watched==parentWidget()&&event->type()==QEvent::Resize)place();return QToolButton::eventFilter(watched,event);}
public:
 WaveformRateButton(QWidget* plot,std::function<void(unsigned)> changed):QToolButton(plot),changed_(std::move(changed)){
  setText(QStringLiteral("60 fps"));setToolTip(QStringLiteral("30 / 60 fps"));setAccessibleName(QStringLiteral("30 / 60 fps"));setAutoRaise(true);setSizePolicy(QSizePolicy::Fixed,QSizePolicy::Fixed);plot->installEventFilter(this);
  QObject::connect(this,&QToolButton::clicked,this,[this]{fps_=fps_==60?30:60;setText(QString::number(fps_)+QStringLiteral(" fps"));place();if(changed_)changed_(fps_);});place();
 }
 unsigned fps()const{return fps_;}
};
}
