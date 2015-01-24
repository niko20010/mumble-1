/* Copyright (C) 2015, Mikkel Krautz <mikkel@krautz.dk>

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

#include "UserVolumeSliderAction.h"

#include "ClientUser.h"
#include "Database.h"

UserVolumeSliderAction::UserVolumeSliderAction(unsigned int sessionId, QWidget *parent)
	: QWidgetAction(parent)
	, m_valueLabel(NULL)
	, m_clientSession(sessionId) {
}

QWidget *UserVolumeSliderAction::createWidget(QWidget *parent) {
	QWidget *container = new QWidget(parent);

	QHBoxLayout *hbox = new QHBoxLayout();
	container->setLayout(hbox);

	QLabel *valueLabel = new QLabel();
	valueLabel->setAlignment(Qt::AlignRight|Qt::AlignVCenter);
	valueLabel->setMinimumSize(QSize(32, 0));
	m_valueLabel = valueLabel;

	QSlider *slider = new QSlider(Qt::Horizontal);
	slider->setMinimumSize(QSize(100, 0));
	connect(slider, SIGNAL(valueChanged(int)),
	        this, SLOT(onSliderValueChanged(int)));

	slider->setRange(0, 200);

	ClientUser *p = ClientUser::get(m_clientSession);
	if (p) {
		int volume = iroundf(p->fVolume * 100 + 0.5f);
		if (volume < 0) {
			volume = 0;
		} else if (volume > 200) {
			volume = 200;
		}
		slider->setValue(volume);
	} else {
		slider->setValue(100);
	}

	// Ensure the onSliderValueChanged slot
	// is invoked, even if value == 0.
	onSliderValueChanged(slider->value());

	hbox->addWidget(slider);
	hbox->addWidget(valueLabel);

	return container;
}

void UserVolumeSliderAction::deleteWidget(QWidget *widget) {
	Q_ASSERT(widget != NULL);

	widget->hide();
	widget->deleteLater();

	ClientUser *p = ClientUser::get(m_clientSession);
	if (p && !p->qsHash.isEmpty()) {
		Database::setUserVolume(p->qsHash, p->fVolume);
	}
}

void UserVolumeSliderAction::onSliderValueChanged(int value) {
	float volume = value / 100.0f;

	ClientUser *p = ClientUser::get(m_clientSession);
	if (p) {
		p->fVolume = volume;
	}

	m_valueLabel->setText(tr("%1%").arg(value, 3));
}
