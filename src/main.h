// src/main.h
//----------------------------------
// RP Soundboard Source Code
// Copyright (c) 2018 Marius Graefe
// All rights reserved
// Contact: rp_soundboard@mgraefe.de
//----------------------------------



#pragma once
#ifndef rpsbsrc__main_H__
#define rpsbsrc__main_H__

#include "common.h"

//#pragma comment (lib, "Ws2_32.lib")

//#define NOMINMAX
//#include <WinSock2.h>
//#define _WINSOCK2API_
//#define _WINSOCKAPI_

//#include <atomic>

#ifdef __cplusplus
class SoundInfo;
int sb_playFile(const SoundInfo &sound);
int sb_playFilePath(std::string path);

// UDP Server

#define SOCKET_BUFLEN 512  //Max length of buffer
#define SOCKET_PORT 35810  //The port on which to listen for incoming data. This is also set in the config dialog, so don't forget to change that as well.

//bool socketInitialized;
void sb_enableUDPServer(bool enable);
void sb_setUDPServerOnlyLocal(bool onlyLocal);
void sb_startServer();
bool sb_tryInitializeSocket();
void sb_stopServer();
void sb_killSocket();

//WSADATA wsaData;
//HANDLE serverHandle;
//std::atomic<SOCKET> serverSocket;
//DWORD WINAPI ServerThread(LPVOID lpParam);
//std::atomic<bool> shutdownServer;
//static std::atomic<bool> serverOnlyLocal;

// UDP Server End

class Sampler;
Sampler *sb_getSampler();
#endif

CAPI void sb_handleCaptureData(uint64 serverConnectionHandlerID, short* samples,
	int sampleCount, int channels, int* edited);
CAPI void sb_handlePlaybackData(uint64 serverConnectionHandlerID, short* samples, int sampleCount,
	int channels, const unsigned int *channelSpeakerArray, unsigned int *channelFillMask);
CAPI void sb_stopPlayback();
//CAPI void sb_setVolume(int vol);
//CAPI void sb_setLocalPlayback(int enabled);
CAPI void sb_init();
CAPI void sb_kill();
CAPI void sb_onServerChange(uint64 serverID);
CAPI void sb_saveConfig();
CAPI void sb_openDialog();
CAPI int sb_playButtonEx(const char* btn);
CAPI void sb_playButton(int btn);
CAPI void sb_setConfig(int cfg);
CAPI void sb_openAbout();
CAPI void sb_pauseSound();
CAPI void sb_unpauseSound();
CAPI void sb_pauseButtonPressed();
CAPI void sb_onConnectStatusChange(uint64 serverConnectionHandlerID, int newStatus, unsigned int errorNumber);
CAPI void sb_getInternalHotkeyName(int buttonId, char *buf); // buf should be at sized 16
CAPI void sb_getInternalConfigHotkeyName(int configId, char *buf);
CAPI void sb_onHotkeyRecordedEvent(const char *keyword, const char *key);
CAPI void sb_onStopTalking();
CAPI void sb_onHotkeyPressed(const char *keyword);
CAPI void sb_checkForUpdates();
CAPI int sb_parseCommand(char**, int);


#define HOTKEY_STOP_ALL "stop_all"
#define HOTKEY_PAUSE_ALL "pause_all"
#define HOTKEY_MUTE_MYSELF "mute_myself"
#define HOTKEY_MUTE_ON_MY_CLIENT "mute_on_my_client"
#define HOTKEY_VOLUME_INCREASE "volume_increase"
#define HOTKEY_VOLUME_DECREASE "volume_decrease"

#endif
