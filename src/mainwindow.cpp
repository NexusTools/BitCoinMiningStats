#include "mainwindow.h"
#include "manageminers.h"
#include "graph.h"
#include "loosejson.h"

#include <stdlib.h>
#include <QCursor>
#include <QInputDialog>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QResource>
#include <QDebug>
#include <QThread>

QProcess MainWindow::miner;

void MainWindow::shutdown(){
	if(miner.state() != QProcess::NotRunning) {
		miner.terminate();
		qDebug() << "Waiting for Miner to Exit...";
		miner.waitForFinished(1500);
		if(miner.state() != QProcess::NotRunning)
			miner.kill();
	}
	qDebug() << "Exiting...";
	_Exit(0);
}

MainWindow::MainWindow(QWidget *parent) :
	QMainWindow(parent),
	currencies(0)
#ifdef DBUS_NOTIFICATIONS
	,DBusNotificationInterface("org.freedesktop.Notifications",
							  "/org/freedesktop/Notifications",
							  "org.freedesktop.Notifications")
#endif
{
	qApp->setQuitOnLastWindowClosed(false);
	setupUi(this);

	trayIcon = new QSystemTrayIcon(this);
	minerGroup = new QActionGroup(menuMining);
	confirmed->setMode(ColorIndicatorLabel::BitCoins);
	unconfirmed->setMode(ColorIndicatorLabel::BitCoins);
	next_reward->setMode(ColorIndicatorLabel::BitCoins);
	estimated->setMode(ColorIndicatorLabel::BitCoins);
	potential->setMode(ColorIndicatorLabel::BitCoins);
	workers_rate->setMode(ColorIndicatorLabel::HashRate);

	connect(this, SIGNAL(exchangeRateChanged(float,QChar)), confirmed, SLOT(exchangeRateChanged(float,QChar)));
	connect(this, SIGNAL(exchangeRateChanged(float,QChar)), unconfirmed, SLOT(exchangeRateChanged(float,QChar)));
	connect(this, SIGNAL(exchangeRateChanged(float,QChar)), next_reward, SLOT(exchangeRateChanged(float,QChar)));
	connect(this, SIGNAL(exchangeRateChanged(float,QChar)), estimated, SLOT(exchangeRateChanged(float,QChar)));
	connect(this, SIGNAL(exchangeRateChanged(float,QChar)), potential, SLOT(exchangeRateChanged(float,QChar)));

	updateExchangeRate.setSingleShot(true);
	updateExchangeRate.setInterval(50000);
	connect(&updateExchangeRate, SIGNAL(timeout()), this, SLOT(requestCurrencyExchangeRate()));

	updateAccountDataTimer.setSingleShot(true);
	updateAccountDataTimer.setInterval(10000);
	connect(&updateAccountDataTimer, SIGNAL(timeout()), this, SLOT(requestAccountDataUpdate()));

	updateBlockInfoTimer.setSingleShot(true);
	updateBlockInfoTimer.setInterval(30000);
	connect(&updateBlockInfoTimer, SIGNAL(timeout()), this, SLOT(requestBlockInfoUpdate()));

	exchangeRateRequest = 0;
	accountDataRequest = 0;
	blockInfoRequest = 0;
	poolStatsRequest = 0;
	widgetMode = false;

	killMiner.setInterval(1500);
	idleWatcher.setInterval(50);

	apiKey = settings.value("slush_api").toString();
	minersUpdated(settings.value("miners").toMap(), false);

	if(apiKey.isNull())
		changeApiToken();
	else
		requestAccountDataUpdate();

	exchangeRate = 1;
	requestBlockInfoUpdate();
	qDebug() << "Using API Key" << apiKey;
	connect(&idleWatcher, SIGNAL(timeout()), this, SLOT(checkIdle()));
	connect(actionIdleControl, SIGNAL(triggered()), this, SLOT(idleControlUpdated()));
	connect(actionQuit, SIGNAL(triggered()), qApp, SLOT(quit()));
	connect(graphBtn, SIGNAL(clicked()), this, SLOT(showGraph()));
	connect(tglWidget, SIGNAL(clicked()), this, SLOT(toggleWidget()));
	connect(actionManage_Miners, SIGNAL(triggered()), this, SLOT(showMinerManagement()));
	connect(minerGroup, SIGNAL(triggered(QAction*)), this, SLOT(updateSelectedMiner(QAction*)));
	connect(actionSet_API_Token, SIGNAL(triggered()), this, SLOT(changeApiToken()));
	connect(actionMinerControl, SIGNAL(triggered()), this, SLOT(toggleMiner()));
	connect(&killMiner, SIGNAL(timeout()), &miner, SLOT(kill()));
	connect(&miner, SIGNAL(stateChanged(QProcess::ProcessState)), this, SLOT(minerStateChanged(QProcess::ProcessState)));
	connect(&miner, SIGNAL(readyReadStandardOutput()), this, SLOT(passStdOut()));
	connect(&miner, SIGNAL(readyReadStandardError()), this, SLOT(passStdErr()));

	QAction* active = 0;
	activeCurrency = settings.value("display_currency", "BTC").toString();
	foreach(QAction* action, menuCurrency->actions()) {
		if(activeCurrency == action->text())
			active = action;
		currencies.addAction(action);
	}
	if(active)
		active->setChecked(true);
	connect(&currencies, SIGNAL(triggered(QAction*)), this, SLOT(displayCurrencyChanged(QAction*)));
	requestCurrencyExchangeRate();

	workers->resizeColumnsToContents();
	dragPoint = QPoint(-1, -1);
	miners = 0;
	graph = 0;

	trayIcon->setIcon(qApp->windowIcon());
	if(!QSystemTrayIcon::isSystemTrayAvailable()) {
		trayHashRate = 0;
		qWarning() << "System Tray Not Available.";
		setAttribute(Qt::WA_DeleteOnClose);
		if(qApp->arguments().contains("-m"))
			QTimer::singleShot(50, this, SLOT(showMinimized()));
	} else {
		QMenu* menu = new QMenu();

		windowVisibilityAction = new QAction("Hide Window", menu);
		connect(windowVisibilityAction, SIGNAL(triggered()), this, SLOT(toggleVisible()));
		menu->addAction(windowVisibilityAction);

		trayHashRate = new QAction("HashRate: 0MH/s", menu);
		trayHashRate->setDisabled(true);
		menu->addAction(trayHashRate);

		menu->addSeparator();
		menu->addMenu(menuMining);
		menu->addSeparator();

		QAction* action = new QAction("Set API Token", menu);
		connect(action, SIGNAL(triggered()), this, SLOT(changeApiToken()));
		menu->addAction(action);

		action = new QAction("Settings", menu);
		menu->addAction(action);

		action = new QAction("Quit", menu);
		connect(action, SIGNAL(triggered()), qApp, SLOT(quit()));
		menu->addAction(action);

		trayIcon->setToolTip("No Miner Running");
		trayIcon->setContextMenu(menu);
		trayIcon->show();

		if(qApp->arguments().contains("-m"))
			QTimer::singleShot(50, this, SLOT(hide()));
	}

	if(qApp->arguments().contains("-r"))
		QTimer::singleShot(100, this, SLOT(toggleMiner()));

	if(qApp->arguments().contains("-a") || settings.value("auto").toBool()) {
		actionMinerControl->setDisabled(true);
		actionIdleControl->setChecked(true);
	}
}

void MainWindow::focusInEvent(QFocusEvent *)
{
	if(widgetMode)
		setWindowOpacity(0.8);
}

void MainWindow::focusOutEvent(QFocusEvent *){
	if(widgetMode)
		setWindowOpacity(0.4);
}

void MainWindow::closeEvent(QCloseEvent *){
	if(!trayIcon->isVisible())
		qApp->quit();
	else
		toggleVisible();
}


void MainWindow::keyPressEvent(QKeyEvent * k){
	if(widgetMode) {
		k->accept();
		if(k->key() == Qt::Key_Escape)
			setWidget(false);
	}
}

void MainWindow::mouseMoveEvent(QMouseEvent * m){
	if(widgetMode) {
		m->accept();
		if(dragPoint.x() > -1 && dragPoint.y() > -1)
			move(m->globalPos() - dragPoint);
	}
}

void MainWindow::mousePressEvent(QMouseEvent * m){
	if(widgetMode) {
		m->accept();
		grabMouse();
		dragPoint = m->globalPos() - pos();
	}
}

void MainWindow::mouseReleaseEvent(QMouseEvent * m){
	if(widgetMode) {
		m->accept();
		if(dragPoint.x() > -1 && dragPoint.y() > -1) {
			dragPoint = QPoint(-1, -1);
			releaseMouse();
		}
	}
}

void MainWindow::changeApiToken()
{
	QInputDialog inputDiag(this);
	inputDiag.setInputMode(QInputDialog::TextInput);
	inputDiag.setLabelText("Slush's Pool API Token");
	inputDiag.setTextValue(settings.value("slush_api").toString());
	inputDiag.exec();
	apiKey = inputDiag.textValue();

	if(!apiKey.isNull()) {
		requestPoolStatsUpdate();
		requestAccountDataUpdate();
		settings.setValue("slush_api", apiKey);
	}
}

void MainWindow::toggleVisible()
{
	setVisible(!isVisible());
	if(isVisible())
		windowVisibilityAction->setText("Hide Window");
	else
		windowVisibilityAction->setText("Show Window");
}

void MainWindow::toggleWidget()
{
	setWidget(!widgetMode);
}

void MainWindow::setWidget(bool checked)
{
	if(widgetMode == checked)
		return;

	widgetMode = checked;
	advanced->setVisible(!widgetMode);
	fileMenu->setVisible(!widgetMode);

	emit invertChanged(widgetMode);
	QTimer::singleShot(0, this, SLOT(hide()));
	QTimer::singleShot(50, this, SLOT(finishTransform()));
}

void MainWindow::finishTransform(){
	static Qt::WindowFlags oldFlags;
	static QRect oldGeometry;
	if (widgetMode)
	{
		oldFlags = windowFlags();
		QResource styleRes(":/widget.css");
		setStyleSheet(QString((const char*)styleRes.data()));
		oldGeometry = geometry();
		setWindowOpacity(0.7);
		setWindowFlags(Qt::SplashScreen | Qt::WindowStaysOnTopHint | Qt::FramelessWindowHint);
		resize(minimumSize());
		move(oldGeometry.topLeft());
	}
	else
	{
		setStyleSheet("");
		setWindowOpacity(1);
		setWindowFlags(oldFlags);
		setGeometry(oldGeometry);
	}
	show();
}

void MainWindow::minerManagementDestroyed()
{
	miners = 0;
}

void MainWindow::graphDestroyed()
{
	graph = 0;
}

void MainWindow::minersUpdated(QVariantMap data, bool store){
	foreach(QAction* action, minerGroup->actions())
		menuMining->removeAction(action);

	QAction* selMiner = 0;
	QStringList miners = data.keys();
	if(miners.isEmpty())
		menuMining->addAction(actionNo_Miners_Configured);
	else {
		menuMining->removeAction(actionNo_Miners_Configured);
		foreach(QString miner, miners) {
			QAction* action = new QAction(miner, menuMining);
			action->setCheckable(true);
			if(settings.value("miner").toString() == miner)
				selMiner = action;
			minerGroup->addAction(action);
			menuMining->addAction(action);
		}
	}
	if(selMiner)
		selMiner->setChecked(true);
	updateSelectedMiner(selMiner);

	if(store) {
		settings.setValue("miners", data);
		settings.sync();
	}
}

void MainWindow::updateSelectedMiner(QAction* action)
{
	QString minerText = action ? action->text() : "";
	settings.setValue("miner", minerText);
	if(miner.state() == QProcess::NotRunning) {
		actionMinerControl->setEnabled(!isMinerBusy() && !actionIdleControl->isChecked());
		actionMinerControl->setText(action ? QString("Start `%1`").arg(action->text()) : "Select a Miner");
	}

	actionMinerControl->setEnabled(action);
	actionIdleControl->setEnabled(action);
}

bool MainWindow::isMinerBusy()
{
	return miner.state() == QProcess::Starting || killMiner.isActive();
}

void MainWindow::checkIdle()
{
	QPoint mPos = QCursor::pos();
	if(mPos != lastMousePos) {
		lastMouseMove.start();
		lastMousePos = mPos;
		stopMiner();
	} else if(lastMouseMove.elapsed() > settings.value("idle_timeout", 30).toInt() * 1000) {
		lastMouseMove.start();
		if(miner.state() == QProcess::NotRunning)
			startMiner();
	}
}

void MainWindow::idleControlUpdated()
{
	settings.setValue("auto", actionIdleControl->isChecked());
	if(actionIdleControl->isChecked()) {
		idleWatcher.start();
		actionMinerControl->setEnabled(false);
	} else {
		idleWatcher.stop();
		actionMinerControl->setEnabled(!isMinerBusy());
	}
}

void MainWindow::passStdOut(){
	qDebug() << miner.readAllStandardOutput().data();
}

void MainWindow::passStdErr(){
	qDebug() << miner.readAllStandardError().data();
}

void MainWindow::showMessage(QString title, QString message)
{
	qWarning() << "Showing Notification" << title << message;
#ifdef DBUS_NOTIFICATIONS
	if(DBusNotificationInterface.isValid()) {
		qDebug() << "Using DBus Notifications";
		QList<QVariant> argumentList;
		argumentList << qApp->applicationName(); //app_name
		argumentList << (uint)0;  // replace_id
		argumentList << "";  // app_icon
		argumentList << title; // summary
		argumentList << message; // body
		argumentList << QStringList();  // actions
		argumentList << QVariantMap();  // hints
		argumentList << (int)2500; // timeout in ms
		QDBusMessage reply = DBusNotificationInterface.callWithArgumentList(QDBus::AutoDetect, "Notify", argumentList);
		if(reply.type() != QDBusMessage::ErrorMessage)
			return;
	}
#endif
	if(QSystemTrayIcon::supportsMessages()) {
		qDebug() << "Using Tray Icon Notifications";
		trayIcon->showMessage(title, message);
	} else
		qWarning() << "No Qt Notification Fallback";
}

void MainWindow::minerStateChanged(QProcess::ProcessState state)
{
	qDebug() << "Miner State Changed" << state;
	switch(state) {
	default:
		return;

	case QProcess::NotRunning:
		if(killMiner.isActive()) {
			showMessage("Miner Stopped", "The mining software has stopped running.");
			killMiner.stop();
		}
		updateSelectedMiner(minerGroup->checkedAction());
		trayIcon->setToolTip("Miner Stopped");
		break;

	case QProcess::Running:
		actionMinerControl->setEnabled(!actionIdleControl->isChecked());
		showMessage("Miner Running", "The mining software is now running.");
		trayIcon->setToolTip("Miner Running");
		break;
	}
}

void MainWindow::stopMiner(){
	if(miner.state() != QProcess::Running || killMiner.isActive())
		return;

	showMessage("Stopping Miner", "The mining software is being stopped.");
	miner.terminate();
	killMiner.start();
	actionMinerControl->setEnabled(false);
}

void MainWindow::toggleMiner(){
	switch(miner.state()){
	default:
		return;

	case QProcess::Running:
		stopMiner();
		break;

	case QProcess::NotRunning:
		startMiner();
		break;
	}
}

void MainWindow::startMiner(QString name){
	if(miner.state() != QProcess::NotRunning) {
		stopMiner();
		return;
	}

	if(name.isEmpty()) {
		QAction* active = minerGroup->checkedAction();
		if(!active)
			return;
		name = active->text();
	}

	QVariantMap minerEntry = settings.value("miners").toMap().value(name).toMap();
	if(minerEntry.isEmpty()) {
		qWarning() << "Attempted to Start Invalid Miner" << name;
		return;
	}

	actionMinerControl->setEnabled(false);
	actionMinerControl->setText(QString("Stop `%1`").arg(name));
	miner.start(minerEntry.value("program").toString(), minerEntry.value("arguments").toStringList());
}

void MainWindow::showMinerManagement(){
	if(miners) {
		miners->setFocus();
		return;
	}

	miners = new ManageMiners(this);
	miners->setMinerData(settings.value("miners"));
	connect(miners, SIGNAL(dataUpdated(QVariantMap)), this, SLOT(minersUpdated(QVariantMap)));
	connect(miners, SIGNAL(destroyed()), this, SLOT(minerManagementDestroyed()));
}

void MainWindow::showGraph(){
	if(graph) {
		graph->setFocus();
		return;
	}

	graph = new Graph(this);
	connect(graph, SIGNAL(destroyed()), this, SLOT(graphDestroyed()));
	connect(this, SIGNAL(receivedAccountData(QVariantMap)), graph, SLOT(receivedAccountData(QVariantMap)));
}

void MainWindow::displayCurrencyChanged(QAction* act) {
	activeCurrency = act->text();
	settings.setValue("display_currency", activeCurrency);
	qDebug() << "Currency changed to" << activeCurrency;
	requestCurrencyExchangeRate();
}

void MainWindow::requestCurrencyExchangeRate()
{
	updateExchangeRate.stop();

	if(exchangeRateRequest)
		exchangeRateRequest->deleteLater();

	if(activeCurrency == "BTC") {
		emit exchangeRateChanged(1, 'B');
		return; // Nothing to do
	}

	exchangeRate = -1;
	exchangeRateRequest = accessMan.get(QNetworkRequest(QUrl(QString("http://data.mtgox.com/api/2/BTC%1/money/ticker_fast").arg(activeCurrency))));
	connect(exchangeRateRequest, SIGNAL(finished()), this, SLOT(exchangeRateReply()));
}

void MainWindow::requestAccountDataUpdate()
{
	updateAccountDataTimer.stop();
	qDebug() << "Requesting Account Data Update";
	if(apiKey.isEmpty())
		return;
	if(accountDataRequest)
		accountDataRequest->deleteLater();
	accountDataRequest = accessMan.get(QNetworkRequest(QUrl(QString("https://mining.bitcoin.cz/accounts/profile/json/%1").arg(apiKey))));
	connect(accountDataRequest, SIGNAL(finished()), this, SLOT(accountDataReply()));
}

void MainWindow::requestBlockInfoUpdate()
{
	updateBlockInfoTimer.stop();
	qDebug() << "Requesting Block Info Update";
	if(blockInfoRequest)
		blockInfoRequest->deleteLater();
	blockInfoRequest = accessMan.get(QNetworkRequest(QUrl("http://blockchain.info/latestblock")));
	connect(blockInfoRequest, SIGNAL(finished()), this, SLOT(blockInfoReply()));
}

void MainWindow::requestPoolStatsUpdate()
{
	qDebug() << "Requesting Pool Statistics Update";
	if(poolStatsRequest)
		poolStatsRequest->deleteLater();

	poolStatsRequest = accessMan.get(QNetworkRequest(QUrl(QString("https://mining.bitcoin.cz/stats/json/%1").arg(apiKey))));
	connect(poolStatsRequest, SIGNAL(finished()), this, SLOT(poolStatsReply()));
}

void MainWindow::exchangeRateReply() {
	QTimer::singleShot(0, exchangeRateRequest, SLOT(deleteLater()));

	updateExchangeRate.start();
	if(exchangeRateRequest->error()) {
		qWarning() << "Request Failed" << exchangeRateRequest->errorString();
		exchangeRateRequest = 0;
		return;

	}

	bool okay;
	QVariantMap map = LooseJSON::parse(exchangeRateRequest->readAll()).toMap();
	exchangeRate = map.value("data").toMap().value("buy").toMap().value("value").toFloat(&okay);
	if(!okay) {
		qCritical() << "Failed to retreive exchange rate for requested currency...";
		exchangeRate = -1;
	} else {
		qDebug() << "Exchange rate for BTC to" << activeCurrency << "is" << exchangeRate;
		QChar s;
		if(activeCurrency == "EUR")
			s = L'£';
		else
			s = '$';
		emit exchangeRateChanged(exchangeRate, s);
	}

	exchangeRateRequest = 0;
}

void MainWindow::accountDataReply()
{
	QTimer::singleShot(0, accountDataRequest, SLOT(deleteLater()));
	updateAccountDataTimer.start();
	if(accountDataRequest->error()) {
		qWarning() << "Request Failed" << accountDataRequest->errorString();

		workers->horizontalHeader()->setVisible(false);
		workers->clearContents();
		workers->insertRow(0);
		workers->setItem(0, 0, new QTableWidgetItem(QString("Connection Issue: %1").arg(accountDataRequest->errorString())));
		workers->resizeColumnsToContents();

		accountDataRequest = 0;
		return;
	}

	qreal totalRate = 0;
	QVariantMap map = LooseJSON::parse(accountDataRequest->readAll()).toMap();
	if(!map.isEmpty()) {
		qDebug() << map.keys();
		QStringList knownWorkers;

		emit receivedAccountData(map);

		// Process Workers
		if(map.contains("workers")) {
			QVariantMap workersMap = map.value("workers").toMap();
			QVariantMap::iterator i;
			for (i = workersMap.begin(); i != workersMap.end(); ++i) {
				QString workerName = i.key();
				QVariantMap workerMap = i.value().toMap();
				knownWorkers.append(workerName);

				totalRate += workerMap.value("hashrate").toFloat();

				int row = -1;
				for(int a=0; a<workers->rowCount(); a++) {
					if(workers->item(a, 0) &&
							workers->item(a, 0)->text() == workerName) {
						row = a;
						break;
					}
				}
				if(row == -1) {
					row = workers->rowCount();
					workers->insertRow(row);
					workers->setItem(row, 0, new QTableWidgetItem(workerName));
				}

				workers->item(row, 0)->setIcon(style()->standardIcon(workerMap.value("alive").toBool()
													? QStyle::SP_MediaPlay : QStyle::SP_MediaStop));

				workers->setItem(row, 1, new QTableWidgetItem(QString("%1MH/s").arg(workerMap.value("hashrate").toString())));
				workers->setItem(row, 2, new QTableWidgetItem(workerMap.value("shares").toString()));
				workers->setItem(row, 3, new QTableWidgetItem(workerMap.value("score").toString()));
			}
		}

		for(int a=workers->rowCount()-1; a>=0; a--) {
			if(!workers->item(a, 0) ||
					!knownWorkers.contains(workers->item(a, 0)->text()))
				workers->removeRow(a);
		}

		workers->resizeColumnsToContents();

		ew = map.value("estimated_reward").toReal();
		cw = map.value("confirmed_reward").toReal();
		uw = map.value("unconfirmed_reward").toReal();
		workers->horizontalHeader()->setVisible(true);
		// Set Labels
		workers_rate->setValue(totalRate);
		if(trayHashRate)
			trayHashRate->setText(QString("HashRate: %1").arg(workers_rate->text()));

		confirmed->setValue(cw);
		unconfirmed->setValue(uw);
		estimated->setValue(ew);
		potential->setValue(cw + uw);
	} else
		qWarning() << "Bad Account Data Reply";


	accountDataRequest = 0;
}

void MainWindow::poolStatsReply()
{
	QTimer::singleShot(0, poolStatsRequest, SLOT(deleteLater()));
	if(poolStatsRequest->error()) {
		qWarning() << "Pool Statistics Request Failed" << poolStatsRequest->errorString();

		poolStatsRequest = 0;
		return;
	}

	QVariantMap map = LooseJSON::parse("(" + poolStatsRequest->readAll() + ")").toMap();

	if(!map.isEmpty()) {
		qreal reward = 0;
		emit receivedPoolStatsData(map);
		int leastConfirmations = -1;
		int unconfirmedblocks = 0;
		if(map.contains("blocks")) {
			QVariantMap blocksMap = map.value("blocks").toMap();
			QVariantMap::iterator i;
			for (i = blocksMap.begin(); i != blocksMap.end(); ++i) {
				QVariantMap blockMap = i.value().toMap();
				if(blockMap.value("reward").toReal() > 0) {
					int confirmationsLeft = 100 - blockMap.value("confirmations").toInt();
					if(confirmationsLeft > 0) {
						if((leastConfirmations == -1 || confirmationsLeft < leastConfirmations)) {
							leastConfirmations = confirmationsLeft;
							reward = blockMap.value("reward").toReal();
						}
						unconfirmedblocks++;
					}
				}
			}
		}

		unconfirmed_blocks->setValue(unconfirmedblocks);
		next_reward->setValue(reward);
		confirmations_left->setValue(leastConfirmations);
	} else {
		qWarning() << "Bad Pool Stats Reply";
	}

	poolStatsRequest = 0;
}

void MainWindow::blockInfoReply()
{
	QTimer::singleShot(0, blockInfoRequest, SLOT(deleteLater()));
	updateBlockInfoTimer.start();
	if(blockInfoRequest->error()) {
		qWarning() << "Pool Statistics Request Failed" << blockInfoRequest->errorString();

		blockInfoRequest = 0;
		return;
	}

	QVariantMap map = LooseJSON::parse("(" + blockInfoRequest->readAll() + ")").toMap();
	if(!map.isEmpty()) {
		emit receivedBlockInfoData(map);

		if(map.contains("height")) {
			uint height = map.value("height").toUInt();
			if(height > blockchain_height->value()) {
				blockchain_height->setValue(height);
				requestPoolStatsUpdate();
			}
		}
	} else
		qWarning() << "Bad Block Info Reply";


	blockInfoRequest = 0;
}

void MainWindow::changeEvent(QEvent *e)
{
	QMainWindow::changeEvent(e);
	switch (e->type()) {
	case QEvent::LanguageChange:
		retranslateUi(this);
		break;
	default:
		break;
	}
}
