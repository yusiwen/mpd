/*
 * Copyright (C) 2003-2015 The Music Player Daemon Project
 * http://www.musicpd.org
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "config.h"
#include "OpenALOutputPlugin.hxx"
#include "../OutputAPI.hxx"
#include "../Wrapper.hxx"
#include "util/Error.hxx"
#include "util/Domain.hxx"

#include <unistd.h>

#ifndef __APPLE__
#include <AL/al.h>
#include <AL/alc.h>
#else
#include <OpenAL/al.h>
#include <OpenAL/alc.h>
#endif

class OpenALOutput {
	friend struct AudioOutputWrapper<OpenALOutput>;

	/* should be enough for buffer size = 2048 */
	static constexpr unsigned NUM_BUFFERS = 16;

	AudioOutput base;

	const char *device_name;
	ALCdevice *device;
	ALCcontext *context;
	ALuint buffers[NUM_BUFFERS];
	unsigned filled;
	ALuint source;
	ALenum format;
	ALuint frequency;

	OpenALOutput()
		:base(openal_output_plugin) {}

	bool Configure(const ConfigBlock &block, Error &error);

	static OpenALOutput *Create(const ConfigBlock &block, Error &error);

	bool Open(AudioFormat &audio_format, Error &error);

	void Close();

	gcc_pure
	unsigned Delay() const {
		return filled < NUM_BUFFERS || HasProcessed()
			? 0
			/* we don't know exactly how long we must wait
			   for the next buffer to finish, so this is a
			   random guess: */
			: 50;
	}

	size_t Play(const void *chunk, size_t size, Error &error);

	void Cancel();

private:
	gcc_pure
	ALint GetSourceI(ALenum param) const {
		ALint value;
		alGetSourcei(source, param, &value);
		return value;
	}

	gcc_pure
	bool HasProcessed() const {
		return GetSourceI(AL_BUFFERS_PROCESSED) > 0;
	}

	gcc_pure
	bool IsPlaying() const {
		return GetSourceI(AL_SOURCE_STATE) == AL_PLAYING;
	}

	bool SetupContext(Error &error);
};

static constexpr Domain openal_output_domain("openal_output");

static ALenum
openal_audio_format(AudioFormat &audio_format)
{
	/* note: cannot map SampleFormat::S8 to AL_FORMAT_STEREO8 or
	   AL_FORMAT_MONO8 since OpenAL expects unsigned 8 bit
	   samples, while MPD uses signed samples */

	switch (audio_format.format) {
	case SampleFormat::S16:
		if (audio_format.channels == 2)
			return AL_FORMAT_STEREO16;
		if (audio_format.channels == 1)
			return AL_FORMAT_MONO16;

		/* fall back to mono */
		audio_format.channels = 1;
		return openal_audio_format(audio_format);

	default:
		/* fall back to 16 bit */
		audio_format.format = SampleFormat::S16;
		return openal_audio_format(audio_format);
	}
}

inline bool
OpenALOutput::SetupContext(Error &error)
{
	device = alcOpenDevice(device_name);

	if (device == nullptr) {
		error.Format(openal_output_domain,
			     "Error opening OpenAL device \"%s\"",
			     device_name);
		return false;
	}

	context = alcCreateContext(device, nullptr);

	if (context == nullptr) {
		error.Format(openal_output_domain,
			     "Error creating context for \"%s\"",
			     device_name);
		alcCloseDevice(device);
		return false;
	}

	return true;
}

inline bool
OpenALOutput::Configure(const ConfigBlock &block, Error &error)
{
	if (!base.Configure(block, error))
		return false;

	device_name = block.GetBlockValue("device");
	if (device_name == nullptr)
		device_name = alcGetString(nullptr,
					   ALC_DEFAULT_DEVICE_SPECIFIER);

	return true;
}

inline OpenALOutput *
OpenALOutput::Create(const ConfigBlock &block, Error &error)
{
	OpenALOutput *oo = new OpenALOutput();

	if (!oo->Configure(block, error)) {
		delete oo;
		return nullptr;
	}

	return oo;
}

inline bool
OpenALOutput::Open(AudioFormat &audio_format, Error &error)
{
	format = openal_audio_format(audio_format);

	if (!SetupContext(error))
		return false;

	alcMakeContextCurrent(context);
	alGenBuffers(NUM_BUFFERS, buffers);

	if (alGetError() != AL_NO_ERROR) {
		error.Set(openal_output_domain, "Failed to generate buffers");
		return false;
	}

	alGenSources(1, &source);

	if (alGetError() != AL_NO_ERROR) {
		error.Set(openal_output_domain, "Failed to generate source");
		alDeleteBuffers(NUM_BUFFERS, buffers);
		return false;
	}

	filled = 0;
	frequency = audio_format.sample_rate;

	return true;
}

inline void
OpenALOutput::Close()
{
	alcMakeContextCurrent(context);
	alDeleteSources(1, &source);
	alDeleteBuffers(NUM_BUFFERS, buffers);
	alcDestroyContext(context);
	alcCloseDevice(device);
}

inline size_t
OpenALOutput::Play(const void *chunk, size_t size, gcc_unused Error &error)
{
	if (alcGetCurrentContext() != context)
		alcMakeContextCurrent(context);

	ALuint buffer;
	if (filled < NUM_BUFFERS) {
		/* fill all buffers */
		buffer = buffers[filled];
		filled++;
	} else {
		/* wait for processed buffer */
		while (!HasProcessed())
			usleep(10);

		alSourceUnqueueBuffers(source, 1, &buffer);
	}

	alBufferData(buffer, format, chunk, size, frequency);
	alSourceQueueBuffers(source, 1, &buffer);

	if (!IsPlaying())
		alSourcePlay(source);

	return size;
}

inline void
OpenALOutput::Cancel()
{
	filled = 0;
	alcMakeContextCurrent(context);
	alSourceStop(source);

	/* force-unqueue all buffers */
	alSourcei(source, AL_BUFFER, 0);
	filled = 0;
}

typedef AudioOutputWrapper<OpenALOutput> Wrapper;

const struct AudioOutputPlugin openal_output_plugin = {
	"openal",
	nullptr,
	&Wrapper::Init,
	&Wrapper::Finish,
	nullptr,
	nullptr,
	&Wrapper::Open,
	&Wrapper::Close,
	&Wrapper::Delay,
	nullptr,
	&Wrapper::Play,
	nullptr,
	&Wrapper::Cancel,
	nullptr,
	nullptr,
};
