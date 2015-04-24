#ifndef samples_H__
#define samples_H__

#include "SampleBuffer.h"
#include "SampleProducerThread.h"

#include <mutex>

class InputFile;
class SoundInfo;

class Sampler
{
public:
	Sampler();
	~Sampler();
	void init();
	void shutdown();
	int fetchInputSamples(short *samples, int count, int channels, bool *finished);
	int fetchOutputSamples(short *samples, int count, int channels, const unsigned int *channelSpeakerArray, unsigned int *channelFillMask);
	bool playFile(const SoundInfo &sound);
	void stopPlayback();
	void setVolume(int vol);
	void setLocalPlayback(bool enabled);
	void setMuteMyself(bool enabled);

private:
	void setVolumeDb(double decibel);
	int fetchSamples(SampleBuffer &sb, short *samples, int count, int channels, bool eraseConsumed, int ciLeft, int ciRight, bool overLeft, bool overRight);
	int findChannelId(int channel, const unsigned int *channelSpeakerArray, int count);
	inline short scale(int val) const {
		//return (short)((val << 16) / m_volumeDivider);
		return (short)((val * m_volumeDivider) >> 12);
	}

private:
	class OnBufferProduceCB : public SampleBuffer::ProduceCallback
	{
	public:
		OnBufferProduceCB(Sampler &parent) :
			parent(parent)
		{}
		void onProduceSamples(const short *samples, int count, SampleBuffer *caller);
	private:
		Sampler &parent;
	};
	
private:
	SampleBuffer m_sbCapture;
	SampleBuffer m_sbPlayback;
	SampleProducerThread m_sampleProducerThread;
	OnBufferProduceCB m_onBufferProduceCB;
	InputFile *m_inputFile;
	int m_volumeDivider;
	double m_globalDbSetting;
	double m_soundDbSetting;
	std::mutex m_mutex;
	bool m_playing;
	bool m_localPlayback;
	bool m_muteMyself;
};


#endif // samples_H__
