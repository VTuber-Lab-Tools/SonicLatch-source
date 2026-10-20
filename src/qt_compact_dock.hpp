#pragma once
#include "qt_metrics.hpp"
#include <QVBoxLayout>
#include <QComboBox>
#include "qt_wrap_combo.hpp"
#include <QScrollArea>
#include <QResizeEvent>
#include <QTimer>
#include <QToolButton>
namespace pitatuner::qt {
class CompactDock:public QWidget {
 Metrics* metrics_;QGridLayout* numbers_;QGridLayout* adjustments_;
 QWidget* settingsPanel_{};QPushButton* settingsButton_{};std::function<void()> shown_;
 std::array<QWidget*,4> cells_{};int columns_{},adjustColumns_{};bool scheduled_{};
 void reflow(){
  const int available=std::max(1,std::min(width(),parentWidget()?parentWidget()->width():width())-24);int need=0;
  for(size_t i=0;i<4;++i){auto* value=metrics_->metricValue(i);need=std::max(need,value->fontMetrics().horizontalAdvance(value->text().section(QStringLiteral(" · "),0,0))+8);auto* label=metrics_->metricLabel(i);for(const auto& word:label->text().split(' '))need=std::max(need,std::min(180,label->fontMetrics().horizontalAdvance(word)));}
  const int columns=available>=2*need+8?2:1;
  if(columns!=columns_){for(auto* cell:cells_)numbers_->removeWidget(cell);for(size_t i=0;i<4;i++)numbers_->addWidget(cells_[i],int(i)/columns,int(i)%columns);columns_=columns;}
  const int count=available>=420?4:2;if(count!=adjustColumns_){for(size_t i=0;i<4;i++)adjustments_->removeWidget(metrics_->operationButton(i));for(size_t i=0;i<4;i++)adjustments_->addWidget(metrics_->operationButton(i),int(i)/count,int(i)%count);adjustColumns_=count;}
  for(size_t i=0;i<4;++i){const int cellWidth=std::max(1,(available-8*(columns_-1))/columns_);for(auto* label:{metrics_->metricLabel(i),metrics_->metricValue(i)})if(label->wordWrap()){const int needed=std::max(label->fontMetrics().height(),label->heightForWidth(cellWidth));if(label->minimumHeight()!=needed)label->setMinimumHeight(needed);}}
  int actionHeight=32;for(size_t i=0;i<8;++i)actionHeight=std::max(actionHeight,metrics_->operationButton(i)->heightForWidth(metrics_->operationButton(i)->width()));
  const auto rateButtons=findChildren<QToolButton*>();for(auto* button:rateButtons)actionHeight=std::max(actionHeight,button->fontMetrics().height()+12);
  for(size_t i=0;i<8;++i){auto* button=metrics_->operationButton(i);if(button->minimumHeight()!=actionHeight||button->maximumHeight()!=actionHeight)button->setFixedHeight(actionHeight);}
  for(auto* button:rateButtons)if(button->minimumHeight()!=actionHeight||button->maximumHeight()!=actionHeight)button->setFixedHeight(actionHeight);
  for(auto* combo:findChildren<QComboBox*>())if(dynamic_cast<WrapComboBox*>(combo)&&combo->width()>0){const int needed=combo->heightForWidth(combo->width());if(combo->minimumHeight()!=needed||combo->maximumHeight()!=needed)combo->setFixedHeight(needed);}
  // QScrollArea must reserve the wrapped content height, not compress warnings.
  if(layout()){const int desired=layout()->totalHeightForWidth(std::min(width(),parentWidget()?parentWidget()->width():width()));if(desired>0&&minimumHeight()!=desired)setMinimumHeight(desired);}
 }
 void schedule(){if(scheduled_)return;scheduled_=true;QTimer::singleShot(0,this,[this]{scheduled_=false;reflow();});}
protected:
 void showEvent(QShowEvent* e)override{QWidget::showEvent(e);if(shown_)shown_();}
 bool eventFilter(QObject* watched,QEvent* event)override{if(event->type()==QEvent::FocusIn){if(auto* widget=qobject_cast<QWidget*>(watched)){for(auto* ancestor=parentWidget();ancestor;ancestor=ancestor->parentWidget())if(auto* scroll=qobject_cast<QScrollArea*>(ancestor)){scroll->ensureWidgetVisible(widget,8,8);break;}}}return QWidget::eventFilter(watched,event);}
 void resizeEvent(QResizeEvent* e)override{QWidget::resizeEvent(e);reflow();}
 bool event(QEvent* e)override{const auto type=e->type();const bool result=QWidget::event(e);if(type==QEvent::LayoutRequest||type==QEvent::FontChange||type==QEvent::StyleChange)schedule();return result;}
public:
 CompactDock(Metrics* metrics,QComboBox* picker,QPushButton* read,QWidget* pre,QWidget* post,const std::vector<QPushButton*>& references,QWidget* parent=nullptr,std::function<void()> shown={}):QWidget(parent),metrics_(metrics),shown_(std::move(shown)){
  auto* layout=new QVBoxLayout(this);layout->setContentsMargins(12,12,12,12);layout->setSpacing(6);setMinimumWidth(0);setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Preferred);
  picker->setMinimumContentsLength(1);picker->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);QSizePolicy pickerPolicy(QSizePolicy::Expanding,QSizePolicy::Preferred);pickerPolicy.setHeightForWidth(true);picker->setSizePolicy(pickerPolicy);picker->setToolTip(picker->currentText());QObject::connect(picker,&QComboBox::currentTextChanged,picker,&QWidget::setToolTip);layout->addWidget(picker);
  // Retain diagnostic state/confidence and every write guard, without layout rows.
  for(size_t i:{size_t(5),size_t(4)})for(auto* label:{metrics->metricLabel(i),metrics->metricValue(i)}){label->setParent(this);label->hide();}
  layout->addWidget(pre);if(post)layout->addWidget(post);
  numbers_=new QGridLayout;numbers_->setHorizontalSpacing(8);numbers_->setVerticalSpacing(8);layout->addLayout(numbers_);
  for(size_t i=0;i<4;i++){auto* cell=new QWidget(this);auto* pair=new QVBoxLayout(cell);pair->setContentsMargins(0,0,0,0);pair->setSpacing(4);metrics->metricLabel(i)->setWordWrap(true);metrics->metricLabel(i)->setMinimumWidth(0);metrics->metricValue(i)->setMinimumWidth(0);metrics->metricValue(i)->setWordWrap(true);pair->addWidget(metrics->metricLabel(i));pair->addWidget(metrics->metricValue(i));cells_[i]=cell;}
  settingsPanel_=new QWidget(this);auto* settingsLayout=new QVBoxLayout(settingsPanel_);settingsLayout->setContentsMargins(0,0,0,0);settingsLayout->setSpacing(6);
  // Host Audio Details replaces the bottom action; no expanding settings panel.
  for(auto* button:references)button->hide();read->hide();settingsPanel_->hide();
  layout->addWidget(metrics->manualEditor());
  adjustments_=new QGridLayout;adjustments_->setSpacing(6);layout->addLayout(adjustments_);
  auto* footer=new QGridLayout;footer->setSpacing(6);layout->addLayout(footer);
  settingsButton_=metrics->operationButton(4);settingsButton_->setText(metrics->text(Text::Settings));
  for(size_t i=0;i<8;++i)dynamic_cast<WrapButton*>(metrics->operationButton(i))->comfortable();
  dynamic_cast<WrapButton*>(metrics->operationButton(5))->emphasize();dynamic_cast<WrapButton*>(settingsButton_)->comfortable();
  dynamic_cast<WrapButton*>(metrics->operationButton(6))->comfortable();dynamic_cast<WrapButton*>(metrics->operationButton(7))->comfortable();
  const std::array<QPushButton*,4> actions{metrics->operationButton(5),metrics->operationButton(6),metrics->operationButton(7),settingsButton_};
  footer->addWidget(actions[0],0,0,1,2);footer->addWidget(actions[1],1,0);footer->addWidget(actions[2],1,1);footer->addWidget(actions[3],2,0,1,2);
  layout->addStretch(1); // Extra dock height belongs below the controls.

  picker->installEventFilter(this);QWidget* previous=picker;auto tab=[&](QWidget* next){next->installEventFilter(this);QWidget::setTabOrder(previous,next);previous=next;};
  tab(metrics->manualEditor());for(size_t i=0;i<4;i++)tab(metrics->operationButton(i));for(auto* action:actions)tab(action);reflow();
 }
 QWidget* settingsPanel()const{return settingsPanel_;}
 QPushButton* settingsButton()const{return settingsButton_;}
 int numericColumns()const{return columns_;}int adjustmentColumns()const{return adjustColumns_;}
};
}
