#pragma once
#include <display_consumer.hpp>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <qt_wrap_button.hpp>
#include <qt_wrap_label.hpp>
#include <qt_literal_spinbox.hpp>
#include <QLocale>
#include <QSignalBlocker>
#include <functional>
#include <array>
#include <algorithm>
namespace pitatuner::qt {
// Semantic roles, not a new translation catalog. The host binds these roles to
// the approved frozen catalog and obs_module_text after the locale ZIP arrives.
enum class Text {Delay,Current,Recommended,Residual,Confidence,State,Milliseconds,
 Unknown,Stable,Invalid,Silent,Muted,Low,Read,Apply,Undo,Manual,Minus5,Minus1,Plus1,Plus5,
 Waiting,NoBaseline,Protected,Expired,Unconnected,Reset,Settings,NoSource};
using Translate=std::function<QString(Text)>;
inline QString nsValue(std::optional<pita::Ns> value){
 if(!value)return QString(QChar(0x2014));
 const auto n=*value;const auto magnitude=n<0?static_cast<uint64_t>(-(n+1))+1:static_cast<uint64_t>(n);
 return (n<0?QStringLiteral("-"):QString{})+QString::number(magnitude/1000000ULL)+QStringLiteral(".")+QString::number((magnitude%1000000ULL)/1000ULL).rightJustified(3,QChar('0'));
}
class Metrics {
 pita::Ns manualReadback_{};uint64_t textUpdates_{};bool manualDirty_{},heldStyle_{};Translate tr_;std::array<QLabel*,6> labels_{},values_{};
 std::array<QPushButton*,8> operations_{};LiteralSpinBox* manual_{};
 void setValue(size_t index,const QString& text){if(values_[index]->text()!=text){values_[index]->setText(text);++textUpdates_;}}
void heldStyle(bool held){if(held==heldStyle_)return;heldStyle_=held;values_[0]->setStyleSheet(held?QStringLiteral("color: #d4ad63;"):QString{});values_[0]->setToolTip(held?tr_(Text::Expired):QString{});}
public:
 uint64_t textUpdates()const{return textUpdates_;}
 struct HeldDelay {pita::Ns delay;uint64_t ageNs;};
 explicit Metrics(QWidget* root,QGridLayout* layout,Translate translate):tr_(std::move(translate)){
  constexpr std::array roles{Text::Delay,Text::Current,Text::Recommended,Text::Residual,Text::Confidence,Text::State};
  for(size_t i=0;i<roles.size();i++){labels_[i]=new WrapLabel(tr_(roles[i]),root);values_[i]=new WrapLabel(root);values_[i]->setTextInteractionFlags(Qt::TextSelectableByMouse);layout->addWidget(labels_[i],static_cast<int>(i/3)*2,static_cast<int>(i%3));layout->addWidget(values_[i],static_cast<int>(i/3)*2+1,static_cast<int>(i%3));}
  constexpr std::array actions{Text::Minus5,Text::Minus1,Text::Plus1,Text::Plus5,Text::Manual,Text::Apply,Text::Undo,Text::Reset};
  for(size_t i=0;i<actions.size();i++){operations_[i]=new WrapButton(tr_(actions[i]),root);operations_[i]->setEnabled(false);operations_[i]->setToolTip(tr_(Text::Unconnected));layout->addWidget(operations_[i],4+static_cast<int>(i/4),static_cast<int>(i%4));}
  manual_=new LiteralSpinBox(root);manual_->setRange(-500,500);manual_->setDecimals(6);manual_->setLocale(QLocale::c());manual_->setKeyboardTracking(false);manual_->setEnabled(false);manual_->setSuffix(QStringLiteral(" ")+tr_(Text::Milliseconds).arg(QString{}).trimmed());layout->addWidget(manual_,6,0,1,2);
  update({});
 }
 void update(const display::Presentation& presentation,std::optional<Text> unavailableReason={},std::optional<HeldDelay> held={},std::optional<pita::Ns> readback={}){
  const auto& snapshot=presentation.snapshot;const auto format=tr_(Text::Milliseconds);
  const auto numeric=[&](std::optional<pita::Ns> value){return value?format.arg(nsValue(value)):nsValue({});};
  const auto currentDelay=snapshot?snapshot->delay:std::nullopt;
  const auto delayText=currentDelay?numeric(currentDelay):(held?heldDelayText(held->delay,held->ageNs):numeric({}));
  heldStyle(!currentDelay&&bool(held));setValue(0,delayText);setValue(1,numeric(snapshot?snapshot->current:readback));setValue(2,numeric(snapshot?snapshot->target:std::nullopt));setValue(3,numeric(snapshot?snapshot->filterResidualPre:std::nullopt));
  Text confidence=Text::Unknown;if(snapshot){switch(snapshot->confidence){case display::Confidence::Stable:confidence=Text::Stable;break;case display::Confidence::Invalid:confidence=Text::Unknown;break;case display::Confidence::Silent:confidence=Text::Unknown;break;case display::Confidence::Muted:confidence=Text::Unknown;break;case display::Confidence::Low:confidence=Text::Low;break;default:break;}}
  setValue(4,tr_(confidence));Text state=Text::Waiting;if(snapshot){if(snapshot->state==display::State::Silent)state=Text::Silent;else if(snapshot->state==display::State::Muted)state=Text::Muted;else if(snapshot->state==display::State::InvalidEstimate)state=Text::Invalid;}if(snapshot&&snapshot->state==display::State::NoBaseline)state=Text::NoBaseline;else if(presentation.hidden==display::Hidden::Expired)state=Text::Expired;else if(presentation.hidden==display::Hidden::ContextBlocked)state=Text::Protected;if(!snapshot&&unavailableReason)state=*unavailableReason;values_[5]->setWordWrap(true);setValue(5,tr_(state));
  // A presentation is not write authority. Until every actual click is routed
  // through fresh owner/Intent/backend guards, correction controls stay disabled.
 }
 // Signal wiring adds no authority: each handler must recheck owner state.
 void connectActions(QObject* receiver,std::function<void(size_t)> click){
  auto commit=[this,click]{if(!manualDirty_||!manual_->isEnabled())return;manualDirty_=false;click(4);};
  QObject::connect(manual_,qOverload<double>(&QDoubleSpinBox::valueChanged),receiver,[this](double){manualDirty_=true;});
  QObject::connect(manual_->findChild<QLineEdit*>(),&QLineEdit::textChanged,receiver,[this](const QString&){manualDirty_=true;});
  QObject::connect(manual_,&QDoubleSpinBox::editingFinished,receiver,commit);
  for(size_t i=0;i<operations_.size();++i)if(i!=4)QObject::connect(operations_[i],&QPushButton::clicked,receiver,[click,i]{click(i);});
 }
 void discardManualDraft(){seedManualOffset(manualReadback_);}
 void seedManualOffset(pita::Ns value){manualReadback_=value;QSignalBlocker block(manual_);manual_->seed(static_cast<double>(value)/1000000.0);manualDirty_=false;}
 QString text(Text role)const{return tr_(role);}
 // Display-only stale value. The control model and correction guards never read this label.
 QString heldDelayText(pita::Ns delay,uint64_t ageNs)const{return tr_(Text::Milliseconds).arg(nsValue(delay))+QStringLiteral(" · ")+QString::number(ageNs/1000000000ULL)+QStringLiteral(" s");}
 void showHeldDelay(pita::Ns delay,uint64_t ageNs){heldStyle(true);setValue(0,heldDelayText(delay,ageNs));}
 void readOffset(std::optional<pita::Ns> offset){setValue(1,offset?tr_(Text::Milliseconds).arg(nsValue(offset)):nsValue({}));}
 QLabel* metricLabel(size_t index)const{return labels_.at(index);}
 QLabel* metricValue(size_t index)const{return values_.at(index);}
 QDoubleSpinBox* manualEditor()const{return manual_;}
 QPushButton* operationButton(size_t index)const{return operations_.at(index);}
 std::string enteredMilliseconds()const{return manual_->cleanText().toStdString();}
 void authority(bool manual,bool apply,bool undo){
  if(!manual)manualDirty_=false;
  for(size_t i=0;i<4;++i)operations_[i]->setEnabled(manual);
  operations_[7]->setEnabled(manual);operations_[5]->setEnabled(apply);operations_[6]->setEnabled(undo);manual_->setEnabled(manual);
 }
 // Owner calls this on every unbound draw, preserving a rejected selection
 // instead of silently returning to Measuring after clearDisplay().
 void showUnbound(bool rejected){authority(false,false,false);update({{},rejected?display::Hidden::ContextBlocked:display::Hidden::NoSnapshot},rejected?std::optional<Text>{}:std::optional{Text::NoSource});}
 bool stateIs(Text role)const{return values_[5]->text()==tr_(role);}
 bool unknown()const{return values_[4]->text()==tr_(Text::Unknown);}
 QString value(size_t index)const{return values_.at(index)->text();}
 bool writesDisabled()const{if(manual_->isEnabled())return false;for(size_t i=0;i<operations_.size();++i)if(i!=4&&operations_[i]->isEnabled())return false;return true;}
};
}
