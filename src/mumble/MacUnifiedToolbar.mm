/* Copyright (C) 2013, Mikkel Krautz <mikkel@krautz.dk>

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

#include "MacUnifiedToolbar.h"
#include "MacUnifiedToolbarStyle.h"
#import "MacUnifiedToolbarDelegate.h"
#import "MacUnifiedToolbarResizeObserver.h"

#include <QtMacExtras>
#include <qpa/qplatformnativeinterface.h>
#include <QWidget>
#include <QMainWindow>
#include <QToolBar>
#include <QMacNativeWidget>
#include <QVBoxLayout>
#include <QPushButton>
#include <QPalette>
#include <QStyle>
#include <QProxyStyle>

#import <AppKit/AppKit.h>

static NSWindow *qt_mac_window_for(QMainWindow *w) {
	QWindow *window = w->windowHandle();
	if (window) {	
		return static_cast<NSWindow *>(QGuiApplication::platformNativeInterface()->nativeResourceForWindow("nswindow", window));
	}
	return nil;
}

static bool addToolbarAsNative(NSWindow *window, QToolBar *toolBar) {
	NSView *contentView = [window contentView];
	NSView *themeFrame = [contentView superview];
	NSView *toolbarView = nil;
	for (NSView *subView in [themeFrame subviews]) {
		NSRange r = [[subView description] rangeOfString:@"NSToolbarView"];
		if (r.location != NSNotFound) {
			toolbarView = subView;
			break;
		}
	}
	if (!toolbarView) {
		return false;
	}

	QMacNativeWidget *toolbarContainer = new QMacNativeWidget();
	NSView *nativeToolbarContainer = toolbarContainer->nativeView();

	QVBoxLayout *layout = new QVBoxLayout();
	layout->setContentsMargins(0, 0, 0, 0);
	toolbarContainer->setLayout(layout);

	layout->addWidget(toolBar);
	toolbarContainer->show();

	MacUnifiedToolbarStyle *toolbarStyle = new MacUnifiedToolbarStyle();
	toolBar->setStyle(toolbarStyle);
	toolBar->show();

	[toolbarView addSubview:nativeToolbarContainer];
	[toolbarView setPostsFrameChangedNotifications:YES];

	MacUnifiedToolbarResizeObserver *observer = [[MacUnifiedToolbarResizeObserver alloc] initWithQtToolbarContainer:toolbarContainer andNSToolbarView:toolbarView];
	[[NSNotificationCenter defaultCenter] addObserver:observer
                                                 selector:@selector(syncToolbarPosition)
                                                     name:NSViewFrameDidChangeNotification
                                                   object:toolbarView];
	[observer syncToolbarPosition];

	return true;
}

void setMacToolbarForMainWindow(QMainWindow *mw, QToolBar *toolBar) {
	NSWindow *window = qt_mac_window_for(mw);

	mw->removeToolBar(toolBar);

	NSToolbar *toolbar = [[NSToolbar alloc] initWithIdentifier:@"MumbleMainWindow"];

	MacUnifiedToolbarDelegate *unifiedToolbarDelegate = [[MacUnifiedToolbarDelegate alloc] init];
	[toolbar setDelegate:unifiedToolbarDelegate];

	[toolbar setDisplayMode:NSToolbarDisplayModeIconOnly];
	[toolbar setSizeMode:NSToolbarSizeModeRegular];
	[toolbar setAllowsUserCustomization:NO];
	[toolbar setAutosavesConfiguration:NO];

	[window setToolbar:toolbar];
	[toolbar release];

	addToolbarAsNative(window, toolBar);
}
