#pragma once
#include <QDoubleSpinBox>
#include <QKeyEvent>
#include <QLineEdit>
#include <cmath>
namespace pitatuner::qt {
// The editor is a literal draft. OBS-qualified ranges are enforced by the
// owner/model at commit, never by deleting characters while the user types.
class LiteralSpinBox final:public QDoubleSpinBox {
public:
 explicit LiteralSpinBox(QWidget* parent=nullptr):QDoubleSpinBox(parent){}
 QValidator::State validate(QString&,int&)const override{return QValidator::Acceptable;}
 void fixup(QString&)const override{}
 // A pre-existing OBS readback can exceed our write limits. Keep its literal
 // display; QDoubleSpinBox::value() is range-bound and is not authoritative.
 void seed(double value){setValue(value);lineEdit()->setText(prefix()+textFromValue(value)+suffix());}
protected:
 void keyPressEvent(QKeyEvent* event)override{
  if(event->key()==Qt::Key_Return||event->key()==Qt::Key_Enter){event->accept();emit editingFinished();return;}
  QDoubleSpinBox::keyPressEvent(event);
 }
 void focusOutEvent(QFocusEvent* event)override{QWidget::focusOutEvent(event);emit editingFinished();}
 void stepBy(int steps)override{
  bool ok=false;const double draft=locale().toDouble(cleanText(),&ok);
  if(!ok||!std::isfinite(draft)||draft<minimum()||draft>maximum())return;
  setValue(draft);QDoubleSpinBox::stepBy(steps);
 }
};
}
