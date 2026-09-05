// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "ayu/ui/monitor/monitor_center_targets.h"

#include "window/window_session_controller.h"

namespace MonitorCenter {

TargetsView::TargetsView(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Ui::RpWidget(parent) {
}

void TargetsView::paintEvent(QPaintEvent *e) {
	QPainter p(this);
	p.fillRect(e->rect(), st::boxBg);
}

} // namespace MonitorCenter
