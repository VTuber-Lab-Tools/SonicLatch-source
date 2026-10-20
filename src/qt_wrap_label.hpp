#pragma once
#include <QLabel>
#include <QPainter>
#include <QTextLayout>
#include <QTextOption>
namespace pitatuner::qt {
// Keep the exact text. Fall back to grapheme-safe wrapping for long Thai/etc words.
class WrapLabel:public QLabel {
 qreal arrange(QTextLayout& layout,int width)const{QTextOption option;option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);option.setAlignment(alignment()&Qt::AlignHorizontal_Mask);layout.setTextOption(option);layout.beginLayout();qreal y=0;while(true){auto line=layout.createLine();if(!line.isValid())break;line.setLineWidth(std::max(1,width));line.setPosition(QPointF(0,y));y+=line.height();}layout.endLayout();return y;}
public:
 using QLabel::QLabel;
 int heightForWidth(int width)const override{if(!wordWrap()||textFormat()==Qt::RichText)return QLabel::heightForWidth(width);QTextLayout layout(text(),font());return qCeil(arrange(layout,width-2*margin()))+2*margin();}
 QSize minimumSizeHint()const override{return wordWrap()?QSize(0,fontMetrics().height()+2*margin()):QLabel::minimumSizeHint();}
protected:
 void paintEvent(QPaintEvent* event)override{if(!wordWrap()||textFormat()==Qt::RichText){QLabel::paintEvent(event);return;}QPainter painter(this);auto rect=contentsRect().adjusted(margin(),margin(),-margin(),-margin());QTextLayout layout(text(),font());const qreal height=arrange(layout,rect.width());painter.setPen(palette().color(isEnabled()?QPalette::Active:QPalette::Disabled,QPalette::WindowText));QPointF position=rect.topLeft();if(alignment()&Qt::AlignVCenter)position.ry()+=(rect.height()-height)/2;else if(alignment()&Qt::AlignBottom)position.ry()+=rect.height()-height;QVector<QTextLayout::FormatRange> selections;if(selectionStart()>=0){QTextLayout::FormatRange selection;selection.start=selectionStart();selection.length=selectedText().size();selection.format.setBackground(palette().highlight());selection.format.setForeground(palette().highlightedText());selections.push_back(selection);}layout.draw(&painter,position,selections);}
};
}
