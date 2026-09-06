// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#pragma once

#include "base/weak_qptr.h"
#include "ui/rp_widget.h"

#include <QString>

namespace Window {
class SessionController;
} // namespace Window

namespace MonitorCenter {

void ShowMonitorCenter(not_null<Window::SessionController*> controller);

[[nodiscard]] QString MonitorPeerName(
	not_null<Window::SessionController*> controller,
	long long barePeerId);

// Peer name plus the topic title for topic-scoped targets.
[[nodiscard]] QString MonitorTargetName(
	not_null<Window::SessionController*> controller,
	long long barePeerId,
	long long topicId);

[[nodiscard]] QString MonitorFormatBytes(long long bytes);

// Modal-looking confirmation card rendered inside its host window
// (independent windows have no layer stack for Ui::GenericBox).
class ConfirmOverlay final : public Ui::RpWidget {
public:
	static void Show(
		not_null<QWidget*> host,
		QString title,
		QString text,
		QString confirmText,
		Fn<void()> confirmed);

protected:
	void paintEvent(QPaintEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;
	bool eventFilter(QObject *obj, QEvent *e) override;

private:
	ConfirmOverlay(
		not_null<QWidget*> host,
		QString title,
		QString text,
		QString confirmText,
		Fn<void()> confirmed);

	void layoutCard();
	[[nodiscard]] QPoint hoverHostPos() const;

	QString _title;
	QString _text;
	QString _confirmText;
	Fn<void()> _confirmed;
	QRect _card;
	QRect _confirmButton;
	QRect _cancelButton;

};

} // namespace MonitorCenter
