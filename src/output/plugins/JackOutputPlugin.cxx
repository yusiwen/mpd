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
#include "JackOutputPlugin.hxx"
#include "../OutputAPI.hxx"
#include "../Wrapper.hxx"
#include "config/ConfigError.hxx"
#include "util/ConstBuffer.hxx"
#include "util/SplitString.hxx"
#include "util/Error.hxx"
#include "util/Domain.hxx"
#include "Log.hxx"

#include <assert.h>

#include <jack/jack.h>
#include <jack/types.h>
#include <jack/ringbuffer.h>

#include <unistd.h> /* for usleep() */
#include <stdlib.h>
#include <string.h>

static constexpr unsigned MAX_PORTS = 16;

static constexpr size_t jack_sample_size = sizeof(jack_default_audio_sample_t);

struct JackOutput {
	AudioOutput base;

	/**
	 * libjack options passed to jack_client_open().
	 */
	jack_options_t options;

	const char *name;

	const char *server_name;

	/* configuration */

	std::string source_ports[MAX_PORTS];
	unsigned num_source_ports;

	std::string destination_ports[MAX_PORTS];
	unsigned num_destination_ports;

	size_t ringbuffer_size;

	/* the current audio format */
	AudioFormat audio_format;

	/* jack library stuff */
	jack_port_t *ports[MAX_PORTS];
	jack_client_t *client;
	jack_ringbuffer_t *ringbuffer[MAX_PORTS];

	bool shutdown;

	/**
	 * While this flag is set, the "process" callback generates
	 * silence.
	 */
	bool pause;

	JackOutput()
		:base(jack_output_plugin) {}

	bool Configure(const ConfigBlock &block, Error &error);

	bool Connect(Error &error);

	/**
	 * Disconnect the JACK client.
	 */
	void Disconnect();

	void Shutdown() {
		shutdown = true;
	}

	bool Enable(Error &error);
	void Disable();

	bool Open(AudioFormat &new_audio_format, Error &error);

	void Close() {
		Stop();
	}

	bool Start(Error &error);
	void Stop();

	/**
	 * Determine the number of frames guaranteed to be available
	 * on all channels.
	 */
	gcc_pure
	jack_nframes_t GetAvailable() const;

	void Process(jack_nframes_t nframes);

	/**
	 * @return the number of frames that were written
	 */
	size_t WriteSamples(const float *src, size_t n_frames);

	unsigned Delay() const {
		return base.pause && pause && !shutdown
			? 1000
			: 0;
	}

	size_t Play(const void *chunk, size_t size, Error &error);

	bool Pause();
};

static constexpr Domain jack_output_domain("jack_output");

inline jack_nframes_t
JackOutput::GetAvailable() const
{
	size_t min = jack_ringbuffer_read_space(ringbuffer[0]);

	for (unsigned i = 1; i < audio_format.channels; ++i) {
		size_t current = jack_ringbuffer_read_space(ringbuffer[i]);
		if (current < min)
			min = current;
	}

	assert(min % jack_sample_size == 0);

	return min / jack_sample_size;
}

/**
 * Call jack_ringbuffer_read_advance() on all buffers in the list.
 */
static void
MultiReadAdvance(ConstBuffer<jack_ringbuffer_t *> buffers,
		 size_t size)
{
	for (auto *i : buffers)
		jack_ringbuffer_read_advance(i, size);
}

/**
 * Write a specific amount of "silence" to the given port.
 */
static void
WriteSilence(jack_port_t &port, jack_nframes_t nframes)
{
	jack_default_audio_sample_t *out =
		(jack_default_audio_sample_t *)
		jack_port_get_buffer(&port, nframes);
	if (out == nullptr)
		/* workaround for libjack1 bug: if the server
		   connection fails, the process callback is invoked
		   anyway, but unable to get a buffer */
			return;

	std::fill_n(out, nframes, 0.0);
}

/**
 * Write a specific amount of "silence" to all ports in the list.
 */
static void
MultiWriteSilence(ConstBuffer<jack_port_t *> ports, jack_nframes_t nframes)
{
	for (auto *i : ports)
		WriteSilence(*i, nframes);
}

/**
 * Copy data from the buffer to the port.  If the buffer underruns,
 * fill with silence.
 */
static void
Copy(jack_port_t &dest, jack_nframes_t nframes,
     jack_ringbuffer_t &src, jack_nframes_t available)
{
	jack_default_audio_sample_t *out =
		(jack_default_audio_sample_t *)
		jack_port_get_buffer(&dest, nframes);
	if (out == nullptr)
		/* workaround for libjack1 bug: if the server
		   connection fails, the process callback is
		   invoked anyway, but unable to get a
		   buffer */
		return;

	/* copy from buffer to port */
	jack_ringbuffer_read(&src, (char *)out,
			     available * jack_sample_size);

	/* ringbuffer underrun, fill with silence */
	std::fill(out + available, out + nframes, 0.0);
}

inline void
JackOutput::Process(jack_nframes_t nframes)
{
	if (nframes <= 0)
		return;

	jack_nframes_t available = GetAvailable();

	const unsigned n_channels = audio_format.channels;

	if (pause) {
		/* empty the ring buffers */

		MultiReadAdvance({ringbuffer, n_channels},
				 available * jack_sample_size);

		/* generate silence while MPD is paused */

		MultiWriteSilence({ports, n_channels}, nframes);

		return;
	}

	if (available > nframes)
		available = nframes;

	for (unsigned i = 0; i < n_channels; ++i)
		Copy(*ports[i], nframes, *ringbuffer[i], available);

	/* generate silence for the unused source ports */

	MultiWriteSilence({ports + n_channels, num_source_ports - n_channels},
			  nframes);
}

static int
mpd_jack_process(jack_nframes_t nframes, void *arg)
{
	JackOutput &jo = *(JackOutput *) arg;

	jo.Process(nframes);
	return 0;
}

static void
mpd_jack_shutdown(void *arg)
{
	JackOutput &jo = *(JackOutput *) arg;

	jo.Shutdown();
}

static void
set_audioformat(JackOutput *jd, AudioFormat &audio_format)
{
	audio_format.sample_rate = jack_get_sample_rate(jd->client);

	if (jd->num_source_ports == 1)
		audio_format.channels = 1;
	else if (audio_format.channels > jd->num_source_ports)
		audio_format.channels = 2;

	/* JACK uses 32 bit float in the range [-1 .. 1] - just like
	   MPD's SampleFormat::FLOAT*/
	static_assert(jack_sample_size == sizeof(float), "Expected float32");
	audio_format.format = SampleFormat::FLOAT;
}

static void
mpd_jack_error(const char *msg)
{
	LogError(jack_output_domain, msg);
}

#ifdef HAVE_JACK_SET_INFO_FUNCTION
static void
mpd_jack_info(const char *msg)
{
	LogDefault(jack_output_domain, msg);
}
#endif

void
JackOutput::Disconnect()
{
	assert(client != nullptr);

	jack_deactivate(client);
	jack_client_close(client);
	client = nullptr;
}

/**
 * Connect the JACK client and performs some basic setup
 * (e.g. register callbacks).
 */
bool
JackOutput::Connect(Error &error)
{
	shutdown = false;

	jack_status_t status;
	client = jack_client_open(name, options, &status, server_name);
	if (client == nullptr) {
		error.Format(jack_output_domain, status,
			     "Failed to connect to JACK server, status=%d",
			     status);
		return false;
	}

	jack_set_process_callback(client, mpd_jack_process, this);
	jack_on_shutdown(client, mpd_jack_shutdown, this);

	for (unsigned i = 0; i < num_source_ports; ++i) {
		ports[i] = jack_port_register(client,
					      source_ports[i].c_str(),
					      JACK_DEFAULT_AUDIO_TYPE,
					      JackPortIsOutput, 0);
		if (ports[i] == nullptr) {
			error.Format(jack_output_domain,
				     "Cannot register output port \"%s\"",
				     source_ports[i].c_str());
			Disconnect();
			return false;
		}
	}

	return true;
}

static bool
mpd_jack_test_default_device(void)
{
	return true;
}

static unsigned
parse_port_list(const char *source, std::string dest[], Error &error)
{
	unsigned n = 0;
	for (auto &&i : SplitString(source, ',')) {
		if (n >= MAX_PORTS) {
			error.Set(config_domain,
				  "too many port names");
			return 0;
		}

		dest[n++] = std::move(i);
	}

	if (n == 0) {
		error.Format(config_domain,
			     "at least one port name expected");
		return 0;
	}

	return n;
}

bool
JackOutput::Configure(const ConfigBlock &block, Error &error)
{
	if (!base.Configure(block, error))
		return false;

	options = JackNullOption;

	name = block.GetBlockValue("client_name", nullptr);
	if (name != nullptr)
		options = jack_options_t(options | JackUseExactName);
	else
		/* if there's a no configured client name, we don't
		   care about the JackUseExactName option */
		name = "Music Player Daemon";

	server_name = block.GetBlockValue("server_name", nullptr);
	if (server_name != nullptr)
		options = jack_options_t(options | JackServerName);

	if (!block.GetBlockValue("autostart", false))
		options = jack_options_t(options | JackNoStartServer);

	/* configure the source ports */

	const char *value = block.GetBlockValue("source_ports", "left,right");
	num_source_ports = parse_port_list(value, source_ports, error);
	if (num_source_ports == 0)
		return false;

	/* configure the destination ports */

	value = block.GetBlockValue("destination_ports", nullptr);
	if (value == nullptr) {
		/* compatibility with MPD < 0.16 */
		value = block.GetBlockValue("ports", nullptr);
		if (value != nullptr)
			FormatWarning(jack_output_domain,
				      "deprecated option 'ports' in line %d",
				      block.line);
	}

	if (value != nullptr) {
		num_destination_ports =
			parse_port_list(value, destination_ports, error);
		if (num_destination_ports == 0)
			return false;
	} else {
		num_destination_ports = 0;
	}

	if (num_destination_ports > 0 &&
	    num_destination_ports != num_source_ports)
		FormatWarning(jack_output_domain,
			      "number of source ports (%u) mismatches the "
			      "number of destination ports (%u) in line %d",
			      num_source_ports, num_destination_ports,
			      block.line);

	ringbuffer_size = block.GetBlockValue("ringbuffer_size", 32768u);

	return true;
}

inline bool
JackOutput::Enable(Error &error)
{
	for (unsigned i = 0; i < num_source_ports; ++i)
		ringbuffer[i] = nullptr;

	return Connect(error);
}

inline void
JackOutput::Disable()
{
	if (client != nullptr)
		Disconnect();

	for (unsigned i = 0; i < num_source_ports; ++i) {
		if (ringbuffer[i] != nullptr) {
			jack_ringbuffer_free(ringbuffer[i]);
			ringbuffer[i] = nullptr;
		}
	}
}

static AudioOutput *
mpd_jack_init(const ConfigBlock &block, Error &error)
{
	JackOutput *jd = new JackOutput();

	if (!jd->Configure(block, error)) {
		delete jd;
		return nullptr;
	}

	jack_set_error_function(mpd_jack_error);

#ifdef HAVE_JACK_SET_INFO_FUNCTION
	jack_set_info_function(mpd_jack_info);
#endif

	return &jd->base;
}

/**
 * Stops the playback on the JACK connection.
 */
void
JackOutput::Stop()
{
	if (client == nullptr)
		return;

	if (shutdown)
		/* the connection has failed; close it */
		Disconnect();
	else
		/* the connection is alive: just stop playback */
		jack_deactivate(client);
}

inline bool
JackOutput::Start(Error &error)
{
	assert(client != nullptr);
	assert(audio_format.channels <= num_source_ports);

	/* allocate the ring buffers on the first open(); these
	   persist until MPD exits.  It's too unsafe to delete them
	   because we can never know when mpd_jack_process() gets
	   called */
	for (unsigned i = 0; i < num_source_ports; ++i) {
		if (ringbuffer[i] == nullptr)
			ringbuffer[i] =
				jack_ringbuffer_create(ringbuffer_size);

		/* clear the ring buffer to be sure that data from
		   previous playbacks are gone */
		jack_ringbuffer_reset(ringbuffer[i]);
	}

	if ( jack_activate(client) ) {
		error.Set(jack_output_domain, "cannot activate client");
		Stop();
		return false;
	}

	const char *dports[MAX_PORTS], **jports;
	unsigned num_dports;
	if (num_destination_ports == 0) {
		/* no output ports were configured - ask libjack for
		   defaults */
		jports = jack_get_ports(client, nullptr, nullptr,
					JackPortIsPhysical | JackPortIsInput);
		if (jports == nullptr) {
			error.Set(jack_output_domain, "no ports found");
			Stop();
			return false;
		}

		assert(*jports != nullptr);

		for (num_dports = 0; num_dports < MAX_PORTS &&
			     jports[num_dports] != nullptr;
		     ++num_dports) {
			FormatDebug(jack_output_domain,
				    "destination_port[%u] = '%s'\n",
				    num_dports,
				    jports[num_dports]);
			dports[num_dports] = jports[num_dports];
		}
	} else {
		/* use the configured output ports */

		num_dports = num_destination_ports;
		for (unsigned i = 0; i < num_dports; ++i)
			dports[i] = destination_ports[i].c_str();

		jports = nullptr;
	}

	assert(num_dports > 0);

	const char *duplicate_port = nullptr;
	if (audio_format.channels >= 2 && num_dports == 1) {
		/* mix stereo signal on one speaker */

		std::fill(dports + num_dports, dports + audio_format.channels,
			  dports[0]);
	} else if (num_dports > audio_format.channels) {
		if (audio_format.channels == 1 && num_dports > 2) {
			/* mono input file: connect the one source
			   channel to the both destination channels */
			duplicate_port = dports[1];
			num_dports = 1;
		} else
			/* connect only as many ports as we need */
			num_dports = audio_format.channels;
	}

	assert(num_dports <= num_source_ports);

	for (unsigned i = 0; i < num_dports; ++i) {
		int ret = jack_connect(client, jack_port_name(ports[i]),
				       dports[i]);
		if (ret != 0) {
			error.Format(jack_output_domain,
				     "Not a valid JACK port: %s", dports[i]);

			if (jports != nullptr)
				free(jports);

			Stop();
			return false;
		}
	}

	if (duplicate_port != nullptr) {
		/* mono input file: connect the one source channel to
		   the both destination channels */
		int ret;

		ret = jack_connect(client, jack_port_name(ports[0]),
				   duplicate_port);
		if (ret != 0) {
			error.Format(jack_output_domain,
				     "Not a valid JACK port: %s",
				     duplicate_port);

			if (jports != nullptr)
				free(jports);

			Stop();
			return false;
		}
	}

	if (jports != nullptr)
		free(jports);

	return true;
}

inline bool
JackOutput::Open(AudioFormat &new_audio_format, Error &error)
{
	pause = false;

	if (client != nullptr && shutdown)
		Disconnect();

	if (client == nullptr && !Connect(error))
		return false;

	set_audioformat(this, new_audio_format);
	audio_format = new_audio_format;

	return Start(error);
}

inline size_t
JackOutput::WriteSamples(const float *src, size_t n_frames)
{
	assert(n_frames > 0);

	const unsigned n_channels = audio_format.channels;

	float *dest[MAX_CHANNELS];
	size_t space = -1;
	for (unsigned i = 0; i < n_channels; ++i) {
		jack_ringbuffer_data_t d[2];
		jack_ringbuffer_get_write_vector(ringbuffer[i], d);

		/* choose the first non-empty writable area */
		const jack_ringbuffer_data_t &e = d[d[0].len == 0];

		if (e.len < space)
			/* send data symmetrically */
			space = e.len;

		dest[i] = (float *)e.buf;
	}

	space /= jack_sample_size;
	if (space == 0)
		return 0;

	const size_t result = n_frames = std::min(space, n_frames);

	while (n_frames-- > 0)
		for (unsigned i = 0; i < n_channels; ++i)
			*dest[i]++ = *src++;

	const size_t per_channel_advance = result * jack_sample_size;
	for (unsigned i = 0; i < n_channels; ++i)
		jack_ringbuffer_write_advance(ringbuffer[i],
					      per_channel_advance);

	return result;
}

inline size_t
JackOutput::Play(const void *chunk, size_t size, Error &error)
{
	pause = false;

	const size_t frame_size = audio_format.GetFrameSize();
	assert(size % frame_size == 0);
	size /= frame_size;

	while (true) {
		if (shutdown) {
			error.Set(jack_output_domain,
				  "Refusing to play, because "
				  "there is no client thread");
			return 0;
		}

		size_t frames_written =
			WriteSamples((const float *)chunk, size);
		if (frames_written > 0)
			return frames_written * frame_size;

		/* XXX do something more intelligent to
		   synchronize */
		usleep(1000);
	}
}

inline bool
JackOutput::Pause()
{
	if (shutdown)
		return false;

	pause = true;

	return true;
}

typedef AudioOutputWrapper<JackOutput> Wrapper;

const struct AudioOutputPlugin jack_output_plugin = {
	"jack",
	mpd_jack_test_default_device,
	mpd_jack_init,
	&Wrapper::Finish,
	&Wrapper::Enable,
	&Wrapper::Disable,
	&Wrapper::Open,
	&Wrapper::Close,
	&Wrapper::Delay,
	nullptr,
	&Wrapper::Play,
	nullptr,
	nullptr,
	&Wrapper::Pause,
	nullptr,
};
