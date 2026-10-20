#pragma once
#include <QPushButton>
#include <algorithm>
#include <QPainter>
#include <QStyleOptionButton>
namespace pitatuner::qt {
// Keep the original text/accessibility/signal; wrap only its painting and height.
class WrapButton:public QPushButton {
 bool prominent_{},comfortable_{};
public:
 explicit WrapButton(const QString& text,QWidget* parent=nullptr):QPushButton(text,parent){QSizePolicy policy(QSizePolicy::Expanding,QSizePolicy::Preferred);policy.setHeightForWidth(true);setSizePolicy(policy);}
 void emphasize(){
  prominent_=true;comfortable_=true;auto f=font();f.setBold(true);setFont(f);
  updateGeometry();
 }
 void comfortable(){comfortable_=true;updateGeometry();}
 bool prominent()const{return prominent_;}
 bool hasHeightForWidth()const override{return true;}
 int heightForWidth(int width)const override{const auto r=fontMetrics().boundingRect(QRect(0,0,std::max(1,width-24),10000),Qt::TextWordWrap|Qt::TextShowMnemonic,text());if(comfortable_){QStyleOptionButton option;initStyleOption(&option);return std::max({32,r.height()+12,style()->sizeFromContents(QStyle::CT_PushButton,&option,QSize(std::max(1,width-24),r.height()+8),this).height()});}return std::max(prominent_?40:30,r.height()+(prominent_?14:12));}
 QSize sizeHint()const override{return {120,heightForWidth(width()>0?width():120)};}
 QSize minimumSizeHint()const override{return {0,comfortable_?32:30};}
protected:
 void paintEvent(QPaintEvent*)override{
  if(prominent_){
   const QColor background=!isEnabled()?QColor("#3c5145"):isDown()?QColor("#12512d"):underMouse()?QColor("#1d7a43"):QColor("#176b3a");
   const QColor foreground=isEnabled()?QColor("#ffffff"):QColor("#c0c9c3");
   const QColor border=!isEnabled()?background:hasFocus()?QColor("#b7f5cb"):QColor("#145b31");
   QPainter painter(this);painter.setRenderHint(QPainter::Antialiasing);painter.setPen(QPen(border,2));painter.setBrush(background);painter.drawRoundedRect(QRectF(rect()).adjusted(1,1,-1,-1),4,4);painter.setPen(foreground);painter.drawText(rect().adjusted(12,5,-12,-5),Qt::AlignCenter|Qt::TextWordWrap|Qt::TextShowMnemonic,text());return;
  }
  QStyleOptionButton option;initStyleOption(&option);option.text.clear();QPainter painter(this);style()->drawControl(QStyle::CE_PushButton,&option,&painter,this);auto rect=style()->subElementRect(QStyle::SE_PushButtonContents,&option,this).adjusted(8,4,-8,-4);style()->drawItemText(&painter,rect,Qt::AlignCenter|Qt::TextWordWrap|Qt::TextShowMnemonic,option.palette,isEnabled(),text(),QPalette::ButtonText);}
};
}
