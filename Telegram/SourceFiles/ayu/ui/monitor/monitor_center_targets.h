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
	// changedExternally: invoked after a target was removed — the host
	// rebuilds the whole view, which is the only reliably-rendered
	// path (in-place reloads lose child paint dispatches).
	TargetsView(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		Fn<void()> changedExternally);

protected:
	void paintEvent(QPaintEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	bool eventFilter(QObject *obj, QEvent *e) override;
	int resizeGetHeight(int newWidth) override;

private:
	void reload();
	void relayout();

	const not_null<Window::SessionController*> _controller;
	class Row;
	std::vector<object_ptr<Row>> _rows;
	Fn<void()> _changedExternally;
	bool _loaded = false;

	// "Refresh" pill in the title strip: geometry cached by paintEvent,
	// hover highlight derived from the live cursor position.
	QRect _refreshRect;
	bool _refreshHovered = false;

};

} // namespace MonitorCenter
