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

// Monitored targets list: enable/disable, per-target media type filter
// editor and removal, with per-target download stats.
class TargetsView final : public Ui::RpWidget {
public:
	TargetsView(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

protected:
	void paintEvent(QPaintEvent *e) override;
	int resizeGetHeight(int newWidth) override;

private:
	void reload();
	void relayout();

	const not_null<Window::SessionController*> _controller;
	class Row;
	std::vector<object_ptr<Row>> _rows;
	bool _loaded = false;

};

} // namespace MonitorCenter
