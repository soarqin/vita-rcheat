#include "rxclient.h"

#include "kcpclient.h"

#include <QMessageBox>
#include <QLocale>
#include <QDir>

rxclient::rxclient(QWidget *parent): QMainWindow(parent), timer(this) {
    ui.setupUi(this);
    QDir dir(qApp->applicationDirPath());
    if (dir.cd("lang")) {
        QStringList ll = dir.entryList({"*.qm"}, QDir::Filter::Files, QDir::SortFlag::IgnoreCase);
        ui.comboLang->addItem(trans.isEmpty() ? "English" : trans.translate("base", "English"));
        for (auto &p: ll) {
            QString compPath = dir.filePath(p);
            QTranslator transl;
            if (transl.load(compPath)) {
                ui.comboLang->addItem(transl.translate("base", "LANGUAGE"), compPath);
            }
        }
    }
	client = new KcpClient;
    timer.start(20);
	connect(&timer, SIGNAL(timeout()), this, SLOT(timerUpdate()));
	connect(ui.btnConnect, SIGNAL(clicked()), this, SLOT(onConnect()));
	connect(ui.comboLang, SIGNAL(currentIndexChanged(int)), this, SLOT(langChange()));
}

void rxclient::langChange() {
    QVariant var = ui.comboLang->currentData();
    QString compPath = var.toString();
    loadLanguage(compPath);
}

void rxclient::onConnect() {
	client->start(ui.editAddr->text().toStdString().c_str(), 9527);
}

void rxclient::timerUpdate() {
    client->runOnce();
}

void rxclient::loadLanguage(const QString &s) {
    qApp->removeTranslator(&trans);
    if (trans.load(s)) {
        qApp->installTranslator(&trans);
    }
    ui.retranslateUi(this);
    ui.comboLang->setItemText(0, trans.isEmpty() ? "English" : trans.translate("base", "English"));
}