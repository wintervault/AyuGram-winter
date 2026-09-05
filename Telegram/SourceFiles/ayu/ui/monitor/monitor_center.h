// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#pragma once

#include <QString>

namespace Window {
class SessionController;
} // namespace Window

namespace MonitorCenter {

void ShowMonitorCenter(not_null<Window::SessionController*> controller);

[[nodiscard]] QString MonitorPeerName(
	not_null<Window::SessionController*> controller,
	long long barePeerId);

[[nodiscard]] QString MonitorFormatBytes(long long bytes);

} // namespace MonitorCenter
