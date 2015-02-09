/* Copyright (C) 2011, Benjamin Jemlich <pcgod@users.sourceforge.net>
   Copyright (C) 2011, Filipe Coelho <falktx@gmail.com>
   Copyright (C) 2015, Mikkel Krautz <mikkel@krautz.dk>

   All rights reserved.

   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions
   are met:

   - Redistributions of source code must retain the above copyright notice,
     this list of conditions and the following disclaimer.
   - Redistributions in binary form must reproduce the above copyright notice,
     this list of conditions and the following disclaimer in the documentation
     and/or other materials provided with the distribution.
   - Neither the name of the Mumble Developers nor the names of its
     contributors may be used to endorse or promote products derived from this
     software without specific prior written permission.

   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
   ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
   A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR
   CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
   EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
   PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
   PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
   LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
   NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
   SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include "mumble_pch.hpp"

#include "JackAudio.h"

#include <cstring>

#include "User.h"
#include "Global.h"
#include "MainWindow.h"
#include "Timer.h"
#include "Settings.h"

static JackAudioSystem *jasys = NULL;

// jackStatusToStringList converts a jack_status_t (a flag type
// that can contain multiple Jack statuses) to q QStringList.
QStringList jackStatusToStringList(jack_status_t status) {
	QStringList statusList;

	if ((status & JackFailure) != 0) {
		statusList << QLatin1String("JackFailure - overall operation failed");
	}
	if ((status & JackInvalidOption) != 0) {
		statusList << QLatin1String("JackInvalidOption - the operation contained an invalid or unsupported option");
	}
	if ((status & JackNameNotUnique) != 0)  {
		statusList << QLatin1String("JackNameNotUnique - the desired client name is not unique");
	}
	if ((status & JackServerStarted) != 0) {
		statusList << QLatin1String("JackServerStarted - the server was started as a result of this operation");
	}
	if ((status & JackServerFailed) != 0) {
		statusList << QLatin1String("JackServerFailed - unable to connect to the JACK server");
	}
	if ((status & JackServerError) != 0) {
		statusList << QLatin1String("JackServerError - communication error with the JACK server");
	}
	if ((status & JackNoSuchClient) != 0) {
		statusList << QLatin1String("JackNoSuchClient - requested client does not exist");
	}
	if ((status & JackLoadFailure) != 0) {
		statusList << QLatin1String("JackLoadFailure - unable to load initial client");
	}
	if ((status & JackInitFailure) != 0) {
		statusList << QLatin1String("JackInitFailure - unable to initialize client");
	}
	if ((status & JackShmFailure) != 0)  {
		statusList << QLatin1String("JackShmFailure - unable to access shared memory");
	}
	if ((status & JackVersionError) != 0) {
		statusList << QLatin1String("JackVersionError - client's protocol version does not match");
	}
	if ((status & JackBackendError) != 0) {
		statusList << QLatin1String("JackBackendError - a backend error occurred");
	}
	if ((status & JackClientZombie) != 0) {
		statusList << QLatin1String("JackClientZombie - client zombified");
	}

	return statusList;
}

class JackAudioInputRegistrar : public AudioInputRegistrar {
	public:
		JackAudioInputRegistrar();
		virtual AudioInput *create();
		virtual const QList<audioDevice> getDeviceChoices();
		virtual void setDeviceChoice(const QVariant &, Settings &);
		virtual bool canEcho(const QString &) const;
};

class JackAudioOutputRegistrar : public AudioOutputRegistrar {
	public:
		JackAudioOutputRegistrar();
		virtual AudioOutput *create();
		virtual const QList<audioDevice> getDeviceChoices();
		virtual void setDeviceChoice(const QVariant &, Settings &);
};

class JackAudioInit : public DeferInit {
	public:
		JackAudioInputRegistrar *m_jackAudioInputRegistrar;
		JackAudioOutputRegistrar *m_jackAudioOutputRegistrar;
		
		void initialize() {
			jasys = new JackAudioSystem();
			jasys->init_jack();

			jasys->m_waitMutex.lock();
			jasys->m_waitCondition.wait(&jasys->m_waitMutex, 1000);
			jasys->m_waitMutex.unlock();

 			if (jasys->m_jackIsGood) {
				m_jackAudioInputRegistrar = new JackAudioInputRegistrar();
				m_jackAudioOutputRegistrar = new JackAudioOutputRegistrar();
			} else {
				m_jackAudioInputRegistrar = NULL;
				m_jackAudioOutputRegistrar = NULL;
				delete jasys;
				jasys = NULL;
			}
		}

		void destroy() {
			delete m_jackAudioInputRegistrar;
			m_jackAudioInputRegistrar = NULL;

			delete m_jackAudioOutputRegistrar;
			m_jackAudioOutputRegistrar = NULL;

			if (jasys != NULL) {
				jasys->close_jack();
				delete jasys;
				jasys = NULL;
			}
		}
};

static JackAudioInit jackinit;

JackAudioSystem::JackAudioSystem()
	: m_jackIsGood(false)
	, m_sampleRate(0) {
}

JackAudioSystem::~JackAudioSystem() {
}

void JackAudioSystem::init_jack() {
	const char **ports = NULL;
	jack_status_t status = static_cast<jack_status_t>(0);
	int err = 0;

	QString clientName = g.s.qsJackClientName;
	if (clientName.isEmpty()) {
		clientName = QLatin1String("mumble");
		qWarning("JackAudioSystem: client name set via config option 'jack/clientname' is empty, using default '%s' client name", qPrintable(clientName));
	}

	m_client = jack_client_open(qPrintable(clientName), JackNoStartServer, &status);
	if (m_client == NULL) {
		QStringList errors = jackStatusToStringList(status);
		qWarning("JackAudioSystem: unable to open jack client due to %li errors:", static_cast<long>(errors.count()));
		for (int i = 0; i < errors.count(); i++) {
			qWarning("JackAudioSystem:  %s", qPrintable(errors.at(i)));
		}
		goto out;
	}

	m_in_port = jack_port_register(m_client, "input", JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);
	if (m_in_port == NULL) {
		qWarning("JackAudioSystem: unable to register 'input' port");
		goto out;
	}

	m_out_port = jack_port_register(m_client, "output", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
	if (m_out_port == NULL) {
		qWarning("JackAudioSystem: unable to register 'output' port");
		goto out;
	}

	err = jack_set_process_callback(m_client, process_callback, this);
	if (err != 0) {
		qWarning("JackAudioSystem: unable to set process callback - jack_set_process_callback() returned %li", static_cast<long>(err));
		goto out;
	}

	err = jack_set_sample_rate_callback(m_client, srate_callback, this);
	if (err != 0)  {
		qWarning("JackAudioSystem: unable to set sample rate callback - jack_set_sample_rate_callback() returned %li", static_cast<long>(err));
		goto out;
	}

	jack_on_shutdown(m_client, shutdown_callback, this);

	m_sampleRate = jack_get_sample_rate(m_client);
	if (m_sampleRate < 0) {
		qWarning("JackAudioSystem: invalid sample rate of '%li' retrieved via jack_get_sample_rate() - aborting", static_cast<long>(m_sampleRate));
		goto out;
	}

	err = jack_activate(m_client);
	if (err != 0) {
		qWarning("JackAudioSystem: unable to activate client - jack_activate() returned %li", static_cast<long>(err));
		goto out;
	}

	ports = jack_get_ports(m_client, 0, 0, JackPortIsPhysical);
	if (ports != NULL) {
		int i = 0;
		while (ports[i]) {
			jack_port_t *port = jack_port_by_name(m_client, ports[i]);
			if (port == NULL)  {
				qWarning("JackAudioSystem: jack_port_by_name() returned an invalid port - skipping it");
				continue;
			}

			int port_flags = jack_port_flags(port);

			if (port_flags & (JackPortIsPhysical|JackPortIsOutput) && strstr(jack_port_type(port), "audio") != NULL) {
				err = jack_connect(m_client, ports[i], jack_port_name(m_in_port));
				if (err != 0) {
					qWarning("JackAudioSystem: unable to connect port '%s' to '%s' - jack_connect() returned %li", ports[i], jack_port_name(m_in_port), static_cast<long>(err));
					goto out;
				}
			}
			
			if (port_flags & (JackPortIsPhysical|JackPortIsInput) && strstr(jack_port_type(port), "audio") != NULL) {
				err = jack_connect(m_client, jack_port_name(m_out_port), ports[i]);
				if (err != 0) {
					qWarning("JackAudioSystem: unable to connect port '%s' to '%s' - jack_connect() returned %li", jack_port_name(m_out_port), ports[i], static_cast<long>(err));
					goto out;
				}	
			}

			++i;
		}
	}

	m_inputs.insert(QString(), tr("Hardware Ports"));
	m_outputs.insert(QString(), tr("Hardware Ports"));

	m_jackIsGood = true;

out:
	jack_free(ports);
	
	// If we get here, and m_jackIsGood isn't set, it's
	// because we've encountered an error. Clean up all
	// our resources.
	if (!m_jackIsGood) {
		// Clean up client if it's still active.
		if (m_client != NULL) {
			err = jack_client_close(m_client);
			if (err != 0) {
				qWarning("JackAudioSystem: unable to to disconnect from the JACK server - jack_client_close() returned %li", static_cast<long>(err));
			}
			m_client = NULL;
		}
	}
}

void JackAudioSystem::close_jack() {
	int err = 0;

	if (m_client != NULL) {
		err = jack_deactivate(m_client);
		if (err != 0)  {
			qWarning("JackAudioSystem: unable to remove client from the process graph - jack_deactivate() returned %li", static_cast<long>(err));
		}

		err = jack_client_close(m_client);
		if (err != 0) {
			qWarning("JackAudioSystem: unable to disconnect from the JACK server - jack_client_close() returned %li", static_cast<long>(err));
		}

		m_client = NULL;
	}
}

int JackAudioSystem::process_callback(jack_nframes_t nframes, void *arg) {
	JackAudioSystem *jas = reinterpret_cast<JackAudioSystem *>(arg);
	if (jas == NULL) {
		qWarning("JackAudioSystem: encountered invalid JackAudioSystem pointer in process_callback");
		return -1;
	}

	if (jas != NULL && jas->m_jackIsGood) {
		AudioInputPtr ai = g.ai;
		AudioOutputPtr ao = g.ao;
		JackAudioInput *jai = reinterpret_cast<JackAudioInput *>(ai.get());
		JackAudioOutput *jao = reinterpret_cast<JackAudioOutput *>(ao.get());

		if (jai && jai->bRunning && jai->iMicChannels > 0 && !jai->isFinished()) {
			void *input = jack_port_get_buffer(jas->m_in_port, nframes);
			if (input != NULL) {
				jai->addMic(input, nframes);
			}

			if (jao && jao->bRunning && jao->iChannels > 0 && !jao->isFinished()) {
				jack_default_audio_sample_t *output = reinterpret_cast<jack_default_audio_sample_t *>(jack_port_get_buffer(jas->m_out_port, nframes));
				if (output != NULL) {
					memset(output, 0, sizeof(jack_default_audio_sample_t) * nframes);
					jao->mix(output, nframes);
				}
			}
		}
	}

	return 0;
}

int JackAudioSystem::srate_callback(jack_nframes_t frames, void *arg) {
	JackAudioSystem *jas = reinterpret_cast<JackAudioSystem *>(arg);
	if (jas == NULL) {
		qWarning("JackAudioSystem: encountered invalid JackAudioSystem pointer in srate_callback");
		return -1;
	}

	jas->m_sampleRate = frames;

	return 0;
}

void JackAudioSystem::shutdown_callback(void *arg) {
	JackAudioSystem *jas = reinterpret_cast<JackAudioSystem *>(arg);
	if (jas == NULL)  {
		qWarning("JackAudioSystem: encountered invalid JackAudioSystem pointer in shutdown_callback");
		return;
	}

	jas->m_jackIsGood = false;
}

JackAudioInputRegistrar::JackAudioInputRegistrar() : AudioInputRegistrar(QLatin1String("JACK"), 10) {
}

AudioInput *JackAudioInputRegistrar::create() {
	return new JackAudioInput();
}

const QList<audioDevice> JackAudioInputRegistrar::getDeviceChoices() {
	QList<audioDevice> choices;

	QStringList inputDevices = jasys->m_inputs.keys();
	qSort(inputDevices);

	foreach(const QString &dev, inputDevices) {
		choices << audioDevice(jasys->m_inputs.value(dev), dev);
	}

	return choices;
}

void JackAudioInputRegistrar::setDeviceChoice(const QVariant &choice, Settings &s) {
	Q_UNUSED(choice);
	Q_UNUSED(s);
}

bool JackAudioInputRegistrar::canEcho(const QString &osys) const {
	Q_UNUSED(osys);
	return false;
}

JackAudioOutputRegistrar::JackAudioOutputRegistrar() : AudioOutputRegistrar(QLatin1String("JACK"), 10) {
}

AudioOutput *JackAudioOutputRegistrar::create() {
	return new JackAudioOutput();
}

const QList<audioDevice> JackAudioOutputRegistrar::getDeviceChoices() {
	QList<audioDevice> choices;

	QStringList outputDevices = jasys->m_outputs.keys();
	qSort(outputDevices);

	foreach(const QString &dev, outputDevices) {
		choices << audioDevice(jasys->m_outputs.value(dev), dev);
	}

	return choices;
}

void JackAudioOutputRegistrar::setDeviceChoice(const QVariant &choice, Settings &s) {
	Q_UNUSED(choice);
	Q_UNUSED(s);
}

JackAudioInput::JackAudioInput() {
	bRunning = true;
	iMicChannels = 0;
};

JackAudioInput::~JackAudioInput() {
	bRunning = false;
	iMicChannels = 0;

	m_waitMutex.lock();
	m_waitCondition.wakeAll();
	m_waitMutex.unlock();
	
	wait();
}

void JackAudioInput::run() {
	if (jasys && jasys->m_jackIsGood) {
		iMicFreq = jasys->m_sampleRate;
		iMicChannels = 1;
		eMicFormat = SampleFloat;
		initializeMixer();
	}

	m_waitMutex.lock();
	while (bRunning) {
		m_waitCondition.wait(&m_waitMutex);
	}
	m_waitMutex.unlock();
}

JackAudioOutput::JackAudioOutput() {
	bRunning = true;
	iChannels = 0;
}

JackAudioOutput::~JackAudioOutput() {
	bRunning = false;
	iChannels = 0;
	
	m_waitMutex.lock();
	m_waitCondition.wakeAll();
	m_waitMutex.unlock();
	
	wait();
}

void JackAudioOutput::run() {
	if (jasys && jasys->m_jackIsGood) {
		unsigned int chanmasks[32];
		memset(&chanmasks, 0, sizeof(chanmasks));

		chanmasks[0] = SPEAKER_FRONT_LEFT;
		chanmasks[1] = SPEAKER_FRONT_RIGHT;

		eSampleFormat = SampleFloat;
		iMixerFreq = jasys->m_sampleRate;
		iChannels = 1;

		initializeMixer(chanmasks);
	}

	m_waitMutex.lock();
	while (bRunning) {
		m_waitCondition.wait(&m_waitMutex);
	}
	m_waitMutex.unlock();
}
