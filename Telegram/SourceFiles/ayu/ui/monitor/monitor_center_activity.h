// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#pragma once

#include "ui/rp_widget.h"

namespace Window {
class SessionController;
} // namespace Window

namespace MonitorCenter {

class ActivityView final : public Ui::RpWidget {
public:
	ActivityView(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

protected:
	void paintEvent(QPaintEvent *e) override;

};

} // namespace MonitorCenter
