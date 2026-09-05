// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "ayu/ui/monitor/monitor_center_activity.h"

#include "window/window_session_controller.h"

namespace MonitorCenter {

ActivityView::ActivityView(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Ui::RpWidget(parent) {
}

void ActivityView::paintEvent(QPaintEvent *e) {
	QPainter p(this);
	p.fillRect(e->rect(), st::boxBg);
}

} // namespace MonitorCenter
