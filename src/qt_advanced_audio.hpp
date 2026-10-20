#pragma once
#include <QAction>
#include <QPointer>
#include <QPushButton>
#include <functional>
namespace pitatuner::qt {
// Reuse the host's existing, translated QAction and its normal dialog lifecycle.
// No OBS-private class linkage, source lookup, or offset write is performed here.
inline bool bindAdvancedAudioButton(QPushButton* button,QObject* mainWindow,QObject* receiver,std::function<bool()> beforeOpen){
 button->setObjectName(QStringLiteral("soniclatchAudioDetailsButton"));button->setFocusPolicy(Qt::NoFocus);
 auto* found=mainWindow?mainWindow->findChild<QAction*>(QStringLiteral("actionAdvAudioProperties")):nullptr;
 if(!found&&mainWindow)found=mainWindow->findChild<QAction*>(QStringLiteral("actionMixerToolbarAdvAudio"));
 if(!found){button->setEnabled(false);return false;}
 const QPointer<QAction> action(found);const QPointer<QPushButton> safeButton(button);
 auto sync=[action,safeButton]{if(!safeButton)return;safeButton->setEnabled(action&&action->isEnabled());if(action){auto text=action->text();const auto mnemonic=text.lastIndexOf(QStringLiteral("(&"));if(mnemonic>=0&&text.size()-mnemonic==4&&text.endsWith(QChar(')')))text.truncate(mnemonic);text.remove(QChar('&'));text=text.trimmed();safeButton->setText(text);safeButton->setToolTip(action->toolTip());}};
 sync();QObject::connect(found,&QAction::changed,receiver,sync);
 QObject::connect(found,&QObject::destroyed,receiver,[safeButton]{if(safeButton)safeButton->setEnabled(false);});
 QObject::connect(button,&QPushButton::clicked,receiver,[action,beforeOpen]{if(action&&action->isEnabled()&&beforeOpen())action->trigger();});
 return true;
}
}
