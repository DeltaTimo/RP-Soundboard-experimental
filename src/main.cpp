// src/main.cpp
//----------------------------------
// RP Soundboard Source Code
// Copyright (c) 2018 Marius Graefe
// All rights reserved
// Contact: rp_soundboard@mgraefe.de
//----------------------------------

#pragma comment (lib, "Ws2_32.lib")

#define NOMINMAX
#include <WinSock2.h>
#define _WINSOCK2API_
#define _WINSOCKAPI_

#include <atomic>

#include "common.h"

#include <cstdio>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>
#include <fstream>
#include <vector>
#include <cstdarg>
#include <map>

#include <QObject>
#include <QtWidgets/qmessagebox.h>
#include <QtCore/QString>

#include "main.h"

#include "ts3log.h"
#include "inputfile.h"
#include "samples.h"
#include "config_qt.h"
#include "about_qt.h"
#include "ConfigModel.h"
#include "UpdateChecker.h"
#include "SoundInfo.h"
#include "TalkStateManager.h"
#include "SpeechBubble.h"

#include <algorithm>

class ModelObserver_Prog : public ConfigModel::Observer
{
public:
	void notify(ConfigModel &model, ConfigModel::notifications_e what, int data) override;
};


static uint64 activeServerId = 1;

ConfigModel *configModel = NULL;
SpeechBubble *notConnectedBubble = NULL;
ConfigQt *configDialog = NULL;
AboutQt *aboutDialog = NULL;
Sampler *sampler = NULL;
TalkStateManager *tsMgr = NULL;

ModelObserver_Prog *modelObserver = NULL;
UpdateChecker *updateChecker = NULL;
std::map<uint64, int> connectionStatusMap;
typedef std::lock_guard<std::mutex> Lock;

// UDP Server

WSADATA wsaData;
HANDLE serverHandle;
std::atomic<SOCKET> serverSocket;
DWORD WINAPI ServerThread(LPVOID lpParam);
static std::atomic<bool> shutdownServer = false;

bool socketInitialized = false;
static std::atomic<bool> serverOnlyLocal = true;

void sb_startServer() {
	if (!sb_tryInitializeSocket()) return;
	if (serverHandle == NULL) {
		if (serverSocket) closesocket(serverSocket);
		shutdownServer = false;
		serverHandle = CreateThread(NULL, 0, ServerThread, NULL, 0, NULL);
	}
}

bool sb_tryInitializeSocket() {
	if (socketInitialized) return true;
	socketInitialized = WSAStartup(MAKEWORD(2, 0), &wsaData) == 0;
	return socketInitialized;
}

void sb_stopServer() {
	if (!socketInitialized || serverHandle == NULL) return;

	shutdownServer = true;

	DWORD threadResult = WaitForSingleObject(serverHandle, 250);
	if (threadResult == WAIT_TIMEOUT) {
		TerminateThread(serverHandle, 0);
	}

	if (serverSocket) {
		closesocket(serverSocket);
	}

	shutdownServer = false;
	serverSocket = NULL;
	serverHandle = NULL;
}

void sb_killSocket() {
	sb_stopServer();

	WSACleanup();

	socketInitialized = false;
}

DWORD WINAPI ServerThread(LPVOID lpParam) {
	struct sockaddr_in server, si_other;
	int slen, recv_len;
	char buf[SOCKET_BUFLEN];
	//wsaData was already initialized.

	slen = sizeof(si_other);

	if ((serverSocket = socket(AF_INET, SOCK_DGRAM, 0)) == INVALID_SOCKET) {
		return EXIT_FAILURE;
	}

	server.sin_family = AF_INET;
	server.sin_addr.s_addr = INADDR_ANY;
	server.sin_port = htons(SOCKET_PORT);

	if (bind((SOCKET)serverSocket, (sockaddr*)&server, sizeof(server)) == SOCKET_ERROR) {
		return EXIT_FAILURE;
	}

	while (!shutdownServer) {
		memset(buf, '\0', SOCKET_BUFLEN);

		if ((recv_len = recvfrom(serverSocket, buf, SOCKET_BUFLEN, 0, (sockaddr*)&si_other, &slen)) == SOCKET_ERROR) {
		}
		else {
			std::string address(inet_ntoa(si_other.sin_addr));
			// Ignore foreign addresses if only listening to local packets.
			if (serverOnlyLocal && !(address.compare("127.0.0.1") == 0 || address.compare("localhost") == 0)) continue;
			std::string command(buf);
			std::string lowered(command);
			std::transform(lowered.begin(), lowered.end(), lowered.begin(), ::tolower);

			auto commandLength = command.length();

			if (commandLength > 5 && lowered.substr(0, 5).compare("play ") == 0) {
				// Try playing path
				std::string path(command.substr(5));
				sb_playFilePath(path);
			}
			else if (commandLength >= 4 && commandLength <= 5 && lowered.substr(0, 4).compare("stop") == 0) {
				// Stop playback
				sb_stopPlayback();
			}
			else if (commandLength >= 9 && commandLength <= 10 && lowered.substr(0, 9).compare("playpause") == 0) {
				if (sampler->getState() == Sampler::ePLAYING)
					sb_pauseSound();
				else if (sampler->getState() == Sampler::ePAUSED)
					sb_unpauseSound();
			}
			else if (commandLength >= 5 && commandLength <= 6 && lowered.substr(0, 5).compare("pause") == 0) {
				sb_pauseSound();
			}
			else if (commandLength >= 7 && commandLength <= 8 && lowered.substr(0, 7).compare("unpause") == 0) {
				sb_unpauseSound();
			}
			else if (commandLength > 7 && lowered.substr(0, 6).compare("button") == 0) {
				auto buttonNameOrId = command.substr(7).c_str();
				sb_playButtonEx(buttonNameOrId);
			}
		}
	}

	return EXIT_SUCCESS;
}

// UDP Server end


void ModelObserver_Prog::notify(ConfigModel &model, ConfigModel::notifications_e what, int data)
{
	switch(what)
	{
	case ConfigModel::NOTIFY_SET_VOLUME:
		sampler->setVolume(data);
		break;
	case ConfigModel::NOTIFY_SET_PLAYBACK_LOCAL:
		sampler->setLocalPlayback(model.getPlaybackLocal());
		break;
	case ConfigModel::NOTIFY_SET_MUTE_MYSELF_DURING_PB:
		sampler->setMuteMyself(model.getMuteMyselfDuringPb());
	case ConfigModel::NOTIFY_SET_UDPSERVER_ENABLED:
		sb_enableUDPServer(data != 0);
		break;
	case ConfigModel::NOTIFY_SET_UDPSERVER_ONLYLOCAL:
		sb_setUDPServerOnlyLocal(data != 0);
		break;
	default:
		break;
	}
}


CAPI void sb_handlePlaybackData(uint64 serverConnectionHandlerID, short* samples, int sampleCount,
	int channels, const unsigned int *channelSpeakerArray, unsigned int *channelFillMask)
{
	if (serverConnectionHandlerID != activeServerId)
		return; //Ignore other servers

	sampler->fetchOutputSamples(samples, sampleCount, channels, channelSpeakerArray, channelFillMask);
}


CAPI void sb_handleCaptureData(uint64 serverConnectionHandlerID, short* samples, int sampleCount, int channels, int* edited)
{
	if (serverConnectionHandlerID != activeServerId)
		return; //Ignore other servers

	int written = sampler->fetchInputSamples(samples, sampleCount, channels, NULL);
	if(written > 0)
		*edited |= 0x1;
}


int sb_playFile(const SoundInfo &sound)
{
	if (activeServerId == 0)
		return 2;
	return sampler->playFile(sound) ? 0 : 1;
}

int sb_playFilePath(std::string path) {
	QString str = QString::fromStdString(path);
	SoundInfo sinfo = SoundInfo::SoundInfo();
	sinfo.filename = str.trimmed();
	return sb_playFile(sinfo);
}

void sb_enableUDPServer(bool enable) {
	// Abort if we got an error initializing Windows Sockets.
	// TODO: Think of a way to tell the user about this. Disable the checkboxes, something like that.
	if (enable) {
		sb_startServer();
	}
	else {
		sb_stopServer();
	}
}

void sb_setUDPServerOnlyLocal(bool onlyLocal) {
	serverOnlyLocal = onlyLocal;
}

Sampler *sb_getSampler()
{
	return sampler;
}


void sb_enableInterface(bool enabled) 
{
	if (!enabled)
	{
		if (!notConnectedBubble)
		{
			notConnectedBubble = new SpeechBubble(configDialog);
			notConnectedBubble->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Minimum);
			notConnectedBubble->setFixedSize(350, 80);
			notConnectedBubble->setBackgroundColor(QColor(255, 255, 255));
			notConnectedBubble->setBubbleStyle(false);
			notConnectedBubble->setClosable(false);
			notConnectedBubble->setText("You are not connected to a server.\n"
				"RP Soundboard is disabled until you are connected properly.");
			notConnectedBubble->attachTo(configDialog);
			if (configDialog->isVisible())
				notConnectedBubble->show();
		}
	}
	else if (notConnectedBubble)
	{
		delete notConnectedBubble;
		notConnectedBubble = NULL;
	}

	configDialog->setEnabled(enabled);
}

CAPI void sb_init()
{
#ifdef _DEBUG
	QMessageBox::information(NULL, "", "rp soundboard plugin init, attach debugger now");
#endif

	InitFFmpegLibrary();

	sb_tryInitializeSocket();

	QTimer::singleShot(10, []{
		configModel = new ConfigModel();
		configModel->readConfig();

		/* This if first QObject instantiated, it will load the resources */
		sampler = new Sampler();
		sampler->init();

		tsMgr = new TalkStateManager();
		QObject::connect(sampler, &Sampler::onStartPlaying, tsMgr, &TalkStateManager::onStartPlaying, Qt::QueuedConnection);
		QObject::connect(sampler, &Sampler::onStopPlaying, tsMgr, &TalkStateManager::onStopPlaying, Qt::QueuedConnection);
		QObject::connect(sampler, &Sampler::onPausePlaying, tsMgr, &TalkStateManager::onPauseSound, Qt::QueuedConnection);
		QObject::connect(sampler, &Sampler::onUnpausePlaying, tsMgr, &TalkStateManager::onUnpauseSound, Qt::QueuedConnection);

		configDialog = new ConfigQt(configModel);
		//configDialog->showMinimized();
		//configDialog->hide();

		modelObserver = new ModelObserver_Prog();
		configModel->addObserver(modelObserver);

		configModel->notifyAllEvents();

		updateChecker = new UpdateChecker();
		updateChecker->startCheck(false, configModel);
	});
}


CAPI void sb_saveConfig()
{
	configModel->writeConfig();
}


CAPI void sb_kill()
{
	sb_killSocket();

	configModel->remObserver(modelObserver);
	delete modelObserver; 
	modelObserver = NULL;

	sampler->shutdown();
	delete sampler;
	sampler = NULL;

	configDialog->close();
	delete configDialog;
	configDialog = NULL;

	configModel->writeConfig();
	delete configModel;
	configModel = NULL;

	if(aboutDialog)
	{
		aboutDialog->close();
		delete aboutDialog;
		aboutDialog = NULL;
	}

	delete updateChecker;
	updateChecker = NULL;
}


CAPI void sb_onServerChange(uint64 serverID)
{
	if (connectionStatusMap.find(serverID) == connectionStatusMap.end())
		connectionStatusMap[serverID] = STATUS_DISCONNECTED;
	bool connected = connectionStatusMap[serverID] == STATUS_CONNECTION_ESTABLISHED;

	tsMgr->setActiveServerId(serverID);
	activeServerId = serverID;
	logInfo("Server Id: %ull", (unsigned long long)serverID);
	sb_enableInterface(connected);
}


CAPI void sb_openDialog()
{
	if(!configDialog)
		configDialog = new ConfigQt(configModel);
	configDialog->showNormal();
	configDialog->raise();
	configDialog->activateWindow();

	sb_enableInterface(connectionStatusMap[activeServerId]);
}


CAPI void sb_stopPlayback()
{
	sampler->stopPlayback();
}


CAPI void sb_pauseSound()
{
	sampler->pausePlayback();
}


CAPI void sb_unpauseSound()
{
	sampler->unpausePlayback();
}


CAPI void sb_pauseButtonPressed()
{
	if (sampler->getState() == Sampler::ePLAYING)
		sb_pauseSound();
	else if (sampler->getState() == Sampler::ePAUSED)
		sb_unpauseSound();
}

/** play button by name or index(strtol), return 0 on success */
CAPI int sb_playButtonEx(const char* button)
{
	long arg1 = strtol(button, NULL, 10);

	if ((NULL != configDialog) && (configDialog->hotkeysEnabled()))
	{
		if (arg1 <= 0)
		{
			//TODO search by name, too lazy right now
		}
		else
		{
			const SoundInfo *sound = configModel->getSoundInfo(arg1);
			if (sound)
				sb_playFile(*sound);
			else
				return 1;
		}
	}
	return 0;
}

CAPI void sb_playButton(int btn)
{
    if ((NULL != configDialog) && (configDialog->hotkeysEnabled()))
    {
        const SoundInfo *sound = configModel->getSoundInfo(btn);
        if (sound)
            sb_playFile(*sound);
    }
}

CAPI void sb_setConfig(int cfg)
{
    if (configDialog)
        configDialog->setConfiguration(cfg);
}

CAPI void sb_openAbout()
{
	if(!aboutDialog)
		aboutDialog = new AboutQt();
	aboutDialog->show();
}


CAPI void sb_onConnectStatusChange(uint64 serverConnectionHandlerID, int newStatus, unsigned int errorNumber) 
{
    Q_UNUSED(errorNumber)

    if(newStatus == STATUS_DISCONNECTED)
		connectionStatusMap.erase(serverConnectionHandlerID);
	else
		connectionStatusMap[serverConnectionHandlerID] = newStatus;

	if (serverConnectionHandlerID == activeServerId)
	{
		if (newStatus == STATUS_DISCONNECTED)
			sb_stopPlayback();
		sb_enableInterface(newStatus == STATUS_CONNECTION_ESTABLISHED);
	}
}


CAPI void sb_getInternalHotkeyName(int buttonId, char *buf)
{
	sprintf(buf, "button_%i", buttonId + 1);
}


CAPI void sb_getInternalConfigHotkeyName(int configId, char *buf)
{
	sprintf(buf, "config_%i", configId);
}


CAPI void sb_onHotkeyRecordedEvent(const char *keyword, const char *key)
{
	if (configDialog)
		configDialog->onHotkeyRecordedEvent(keyword, key);
}


CAPI void sb_onStopTalking()
{
	tsMgr->onClientStopsTalking();
}

CAPI void sb_onHotkeyPressed(const char * keyword)
{
	int btn = -1;
	if (sscanf(keyword, "button_%i", &btn) > 0)
	{
		sb_playButton(btn - 1);
	}
	else if (sscanf(keyword, "config_%i", &btn) > 0)
	{
		sb_setConfig(btn);
	}
	else if (strcmp(keyword, HOTKEY_STOP_ALL) == 0)
	{
		sb_stopPlayback();
	}
	else if (strcmp(keyword, HOTKEY_PAUSE_ALL) == 0)
	{
		sb_pauseButtonPressed();
	}
	else if (strcmp(keyword, HOTKEY_MUTE_MYSELF) == 0)
	{
		configModel->setMuteMyselfDuringPb(!configModel->getMuteMyselfDuringPb());
	}
	else if (strcmp(keyword, HOTKEY_MUTE_ON_MY_CLIENT) == 0)
	{
		configModel->setPlaybackLocal(!configModel->getPlaybackLocal());
	}
	else if (strcmp(keyword, HOTKEY_VOLUME_INCREASE) == 0)
	{
		configModel->setVolume(std::min(configModel->getVolume() + 20, 100));
	}
	else if (strcmp(keyword, HOTKEY_VOLUME_DECREASE) == 0)
	{
		configModel->setVolume(std::max(configModel->getVolume() - 20, 0));
	}
}


CAPI void sb_checkForUpdates()
{
	if (!updateChecker)
		updateChecker = new UpdateChecker();
	updateChecker->startCheck(true);
}

/** return 0 if the command was handled, 1 otherwise */
CAPI int sb_parseCommand(char** args, int argc)
{
	if (argc >= 3)
		ts3Functions.printMessageToCurrentTab("Too many arguments");
	else if (argc == 0)
		sb_openDialog();
	else if (argc == 1)
	{
		long arg1 = strtol(args[0], NULL, 10);
		if (strcmp(args[0], "stop")==0)
			sb_stopPlayback();
		else if (strcmp(args[0], "-?") == 0)
			ts3Functions.printMessageToCurrentTab("Arguments: 'stop' to stop playback or '[configuration number] <button number>'");
		else if (sb_playButtonEx(args[0]) != 0)
			ts3Functions.printMessageToCurrentTab("No such button found");
	}
	else if (argc == 2)
	{
		long arg0 = strtol(args[0], NULL, 10);
		int pconfig = configModel->getConfiguration(); //TODO ConfigModel::getConfiguration() { return m_activeConfig; }
		if (arg0 < 1 || arg0 > 4)
			ts3Functions.printMessageToCurrentTab("Invalid configuration number");
		configModel->setConfiguration((int)arg0); //switch to specified configuration
		if (sb_playButtonEx(args[0]) != 0)
			ts3Functions.printMessageToCurrentTab("No such button found");
		configModel->setConfiguration(pconfig); //return to previous configuration

	}
	return 0;
}
