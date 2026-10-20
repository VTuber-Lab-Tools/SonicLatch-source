#!/usr/bin/env python3
"""Exact production method bodies against Qt/OBS doubles, NOT real Qt/OBS UI."""
from pathlib import Path
import re, subprocess, tempfile
root=Path(__file__).resolve().parents[1]
header=(root/'src/qt_wrap_combo.hpp').read_text()
module=(root/'poc/normal-source-dock/normal-source-dock-module.cpp').read_text()
show=re.search(r' void showPopup\(\)override\{[^\n]+',header).group()
refresh=module[module.index(' void refreshCatalog(){'):module.index(' void select(',module.index(' void refreshCatalog(){'))]
callback=module[module.index('picker=new pitatuner::qt::WrapComboBox(root,')+len('picker=new pitatuner::qt::WrapComboBox(root,'):module.index(');picker->setAccessibleName',module.index('picker=new pitatuner::qt::WrapComboBox(root,'))]
assert 'QSignalBlocker block(picker)' in refresh
assert show.index('beforePopup_()') < show.index('QComboBox::showPopup()')
assert 'std::function<void()> beforePopup_' in header
assert 'beforePopup_(std::move(beforePopup))' in header
assert 'select(' not in refresh and 'select(' not in callback
prefix=r'''
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
struct QString {std::string s;static QString fromStdString(const std::string& s){return {s};}bool isEmpty()const{return s.empty();}bool operator==(const QString&)const=default;};
struct QVariant {QString s;QString toString()const{return s;}};
std::vector<std::string> order;
struct QComboBox {
 bool blocked=false;int index=-1;unsigned selections=0;unsigned session=77;bool popup=false;
 std::vector<std::pair<QString,QString>> items;
 virtual ~QComboBox()=default;
 virtual void showPopup(){order.push_back("popup");popup=true;}
 void hidePopup(){popup=false;}
 void setCurrentIndex(int n){if(n!=index){index=n;if(!blocked){++selections;++session;}}}
 void clear(){items.clear();setCurrentIndex(-1);}
 void addItem(QString name,QString uuid){items.push_back({name,uuid});if(index<0)setCurrentIndex(0);}
 QVariant currentData()const{return {index<0?QString{}:items.at(index).second};}
 int findData(QString uuid)const{for(size_t i=0;i<items.size();++i)if(items[i].second==uuid)return int(i);return -1;}
};
struct QSignalBlocker {QComboBox* p;bool old;explicit QSignalBlocker(QComboBox* p):p(p),old(p->blocked){p->blocked=true;}~QSignalBlocker(){p->blocked=old;}};
struct WrapComboBox:QComboBox {std::function<void()> beforePopup_;
'''
prefix+=show+'\n};\n'
prefix+=r'''
struct Source{std::string name,uuid;};struct Api{std::vector<Source> sources;bool fail=false;} apiStorage;Api* api=&apiStorage;
namespace cap {std::vector<Source> catalog(Api& a){order.push_back("catalog");if(a.fail)throw std::runtime_error("catalog");return a.sources;}}
QComboBox* picker=nullptr;bool isolatedQa=false;
struct Owner {bool done=false;unsigned failures=0;void uiFailure(const char*){++failures;}
'''
prefix+=refresh+'\n};\nstd::unique_ptr<Owner> owner;\n'
main=r'''
int main(){
 WrapComboBox combo;picker=&combo;owner=std::make_unique<Owner>();
 combo.beforePopup_=CALLBACK;
 apiStorage.sources={{"Microphone A","uuid-a"}};owner->refreshCatalog();combo.setCurrentIndex(0);
 const auto selections=combo.selections,session=combo.session;
 apiStorage.sources.push_back({"Added microphone B","uuid-b"});order.clear();combo.showPopup();
 assert(combo.items.size()==2&&combo.findData({"uuid-b"})==1);assert((order==std::vector<std::string>{"catalog","popup"}));
 puts("PASS 1: added source visible before popup (double)");
 for(int i=0;i<5;++i){combo.hidePopup();combo.showPopup();assert(combo.currentData().toString().s=="uuid-a");assert(combo.selections==selections&&combo.session==session);assert(!combo.blocked);}
 puts("PASS 2: repeated popup keeps UUID, select count and session (double)");
 apiStorage.sources[0].name="Renamed microphone A";combo.hidePopup();combo.showPopup();assert(combo.items[0].first.s=="Renamed microphone A");assert(combo.currentData().toString().s=="uuid-a");assert(combo.selections==selections&&combo.session==session);
 puts("PASS 3: renamed source appears on popup (double)");
 combo.blocked=true;combo.showPopup();assert(combo.blocked);combo.blocked=false;
 owner->done=true;order.clear();combo.showPopup();assert((order==std::vector<std::string>{"popup"}));owner->done=false;
 isolatedQa=true;order.clear();combo.showPopup();assert((order==std::vector<std::string>{"popup"}));isolatedQa=false;
 apiStorage.fail=true;combo.showPopup();assert(owner->failures==1&&!combo.blocked);apiStorage.fail=false;
 owner.reset();combo.showPopup();puts("PASS guards: existing blocker, done, isolated QA, exception and missing owner");
}
'''.replace('CALLBACK',callback)
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.cpp').write_text(prefix+main)
 subprocess.run(['g++','-std=c++20','-O1','-g','-fsanitize=undefined','-fno-sanitize-recover=all',str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
print('STATIC PASS: constructor passes callback; exact refreshCatalog and showPopup bodies checked')
