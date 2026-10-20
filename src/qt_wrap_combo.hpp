#pragma once
#include <QComboBox>
#include <QPainter>
#include <QStyleOptionComboBox>
#include <QTextLayout>
#include <functional>
#include <utility>
namespace pitatuner::qt {
class WrapComboBox:public QComboBox {
 std::function<void()> beforePopup_;
 qreal arrange(QTextLayout& layout,int width)const{QTextOption option;option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);layout.setTextOption(option);layout.beginLayout();qreal y=0;while(true){auto line=layout.createLine();if(!line.isValid())break;line.setLineWidth(std::max(1,width));line.setPosition(QPointF(0,y));y+=line.height();}layout.endLayout();return y;}
public:
 explicit WrapComboBox(QWidget* parent=nullptr,std::function<void()> beforePopup={}):QComboBox(parent),beforePopup_(std::move(beforePopup)){QSizePolicy policy(QSizePolicy::Expanding,QSizePolicy::Preferred);policy.setHeightForWidth(true);setSizePolicy(policy);QObject::connect(this,&QComboBox::currentTextChanged,this,[this]{updateGeometry();update();});}
 void showPopup()override{if(beforePopup_)beforePopup_();QComboBox::showPopup();}
 bool hasHeightForWidth()const override{return true;}
 int heightForWidth(int width)const override{QTextLayout layout(currentText(),font());return std::max(32,int(std::ceil(arrange(layout,width-40)))+20);}
 QSize minimumSizeHint()const override{return {0,32};}
 QSize sizeHint()const override{return {120,heightForWidth(width()>0?width():120)};}
protected:
 void paintEvent(QPaintEvent*)override{QStyleOptionComboBox option;initStyleOption(&option);option.currentText.clear();QPainter painter(this);style()->drawComplexControl(QStyle::CC_ComboBox,&option,&painter,this);auto rect=style()->subControlRect(QStyle::CC_ComboBox,&option,QStyle::SC_ComboBoxEditField,this).adjusted(4,0,-4,0);QTextLayout layout(currentText(),font());const auto height=arrange(layout,rect.width());painter.setPen(palette().color(isEnabled()?QPalette::Active:QPalette::Disabled,QPalette::ButtonText));layout.draw(&painter,QPointF(rect.left(),rect.top()+(rect.height()-height)/2));}
};
}
