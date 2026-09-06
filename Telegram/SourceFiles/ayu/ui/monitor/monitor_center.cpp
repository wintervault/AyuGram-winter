// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "ayu/ui/monitor/monitor_center.h"

#include "ayu/ui/monitor/monitor_center_activity.h"
#include "ayu/ui/monitor/monitor_center_targets.h"
#include "lang/lang_keys.h"
#include "data/data_channel.h"
#include "data/data_forum.h"
#include "data/data_forum_topic.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "main/main_session.h"
#include <QGuiApplication>

#include "styles/style_basic.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_window.h"
#include "base/weak_qptr.h"
#include "ui/abstract_button.h"
#include "ui/painter.h"
#include "ui/style/style_core_scale.h"
#include "ui/widgets/scroll_area.h"
#include "ui/widgets/rp_window.h"
#include "window/window_session_controller.h"

#include <QPainter>
#include <QScreen>
#include <algorithm>

namespace MonitorCenter {

namespace {

[[nodiscard]] int HeaderHeight() {
	// Text block + underline + breathing, all scale-aware.
	return st::semiboldFont->height
		+ style::ConvertScale(3)
		+ style::ConvertScale(1)
		+ 2 * style::ConvertScale(7);
}

} // namespace

enum class View {
	activity,
	targets,
};

class ViewSwitch final : public Ui::AbstractButton {
public:
	ViewSwitch(QWidget *parent, std::vector<QString> labels)
	: Ui::AbstractButton(parent) {
		const auto metrics = QFontMetrics(st::normalFont);
		const auto pad = style::ConvertScale(5);
		const auto extra = style::ConvertScale(2);
		auto x = pad;
		for (auto &label : labels) {
			const auto width = metrics.horizontalAdvance(label)
				+ 2 * pad
				+ extra;
			_items.push_back({ label, x, width });
			x += width;
		}
		resize(x + pad, HeaderHeight());
		setCursor(style::cur_pointer);
	}

	void setActive(int index) {
		if (_active != index) {
			_active = index;
			update();
		}
	}

	[[nodiscard]] rpl::producer<int> activated() const {
		return _activated.events();
	}

protected:
	void paintEvent(QPaintEvent *e) override {
		auto p = QPainter(this);
		const auto gap = style::ConvertScale(3);
		const auto underline = style::ConvertScale(1);
		for (auto i = 0; i != int(_items.size()); ++i) {
			const auto &item = _items[i];
			const auto active = (i == _active);
			const auto font = active ? st::semiboldFont : st::normalFont;
			const auto metrics = QFontMetrics(font);
			p.setFont(font);
			p.setPen(active ? st::windowBoldFg : st::windowSubTextFg);
			// Text + underline form one vertically centered block.
			const auto textTop = (height()
				- st::semiboldFont->height
				- gap
				- underline) / 2;
			const auto baseline = textTop + metrics.ascent();
			p.drawText(item.left, baseline, item.text);
			if (active) {
				// Center the underline on the text itself: text starts
				// at item.left (padding already included in the layout).
				p.fillRect(
					item.left - style::ConvertScale(1),
					textTop + st::semiboldFont->height + gap,
					metrics.horizontalAdvance(item.text)
						+ style::ConvertScale(2),
					underline,
					st::windowBgActive);
			}
		}
	}

	void mousePressEvent(QMouseEvent *e) override {
		for (auto i = 0; i != int(_items.size()); ++i) {
			if (e->pos().x() >= _items[i].left
				&& e->pos().x() < _items[i].left + _items[i].width) {
				setActive(i);
				_activated.fire_copy(i);
				break;
			}
		}
		Ui::AbstractButton::mousePressEvent(e);
	}

private:
	struct Item {
		QString text;
		int left = 0;
		int width = 0;
	};

	std::vector<Item> _items;
	int _active = 0;
	rpl::event_stream<int> _activated;

};

class CenterWindow final : public Ui::RpWindow {
public:
	CenterWindow(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

protected:
	void resizeEvent(QResizeEvent *e) override;

private:
	void showView(View view);

	const not_null<Window::SessionController*> _controller;
	object_ptr<Ui::RpWidget> _header;
	object_ptr<ViewSwitch> _switch;
	object_ptr<Ui::ScrollArea> _scroll;
	QPointer<Ui::RpWidget> _content;
	QPointer<ActivityView> _activity;
	View _view = View::activity;

};

CenterWindow::CenterWindow(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Ui::RpWindow(parent)
, _controller(controller)
, _header(body())
, _switch(_header, std::vector<QString>{
	tr::ayu_MonitorCenterActivity(tr::now),
	tr::ayu_MonitorCenterTargets(tr::now) })
, _scroll(body(), st::boxScroll, false) {
	setTitle(tr::ayu_MonitorCenter(tr::now));
	setMinimumSize({ 480, 360 });
	// Closing destroys the window: the weak singleton reference clears
	// automatically and the next ShowMonitorCenter() builds a fresh one
	// with up-to-date data.
	setAttribute(Qt::WA_DeleteOnClose);
	{
		// Center on the screen the main window lives on.
		const auto screen = parent
			? parent->screen()
			: QGuiApplication::primaryScreen();
		const auto available = screen->availableGeometry();
		auto geometry = QRect(0, 0, 900, 680);
		geometry.moveCenter(available.center());
		setGeometry(geometry);
	}

	body()->paintRequest(
	) | rpl::on_next([=](QRect clip) {
		QPainter(body().get()).fillRect(clip, st::windowBg);
	}, body()->lifetime());

	_header->setGeometry(0, 0, body()->width(), HeaderHeight());
	_header->show();
	_switch->moveToRight(style::ConvertScale(6), 0);
	_switch->show();

	_header->paintRequest(
	) | rpl::on_next([=] {
		auto p = QPainter(_header.data());
		p.fillRect(_header->rect(), st::windowBg);
		p.fillRect(
			0,
			_header->height() - 1,
			_header->width(),
			1,
			st::shadowFg);
	}, _header->lifetime());

	_switch->activated(
	) | rpl::on_next([=](int index) {
		showView(index == 0 ? View::activity : View::targets);
	}, lifetime());

	_scroll->move(0, HeaderHeight());
	_scroll->show();
	_scroll->scrolls(
	) | rpl::on_next([=] {
		if (const auto activity = _activity.data()) {
			activity->checkLoadMore(
				_scroll->scrollTop(),
				_scroll->height());
		}
	}, lifetime());

	showView(_view);
}

void CenterWindow::showView(View view) {
	if (_view == view && _content) {
		return;
	}
	_view = view;
	_switch->setActive(view == View::activity ? 0 : 1);
	if (view == View::activity) {
		_activity = _scroll->setOwnedWidget(
			object_ptr<ActivityView>(body(), _controller));
		_content = _activity.data();
	} else {
		_content = _scroll->setOwnedWidget(
			object_ptr<TargetsView>(body(), _controller));
	}
	if (_view == View::activity && _activity) {
		_activity->refreshStats();
	}
	if (_content) {
		_content->resizeToWidth(_scroll->width());
		_content->show();
	}
	_scroll->scrollToY(0);
}

void CenterWindow::resizeEvent(QResizeEvent *e) {
	// width()/height() here are the client area: it already excludes the
	// system title bar, so no frame margins may be added on top of it.
	// The old code added frameMargins().top() and shifted everything
	// below the header down by the title bar height.
	const auto w = width();
	const auto h = height();
	_header->setGeometry(0, 0, w, HeaderHeight());
	_switch->moveToRight(style::ConvertScale(6), 0);
	_scroll->setGeometry(0, HeaderHeight(), w, h - HeaderHeight());
	if (_content) {
		_content->resizeToWidth(_scroll->width());
	}
}

void ShowMonitorCenter(not_null<Window::SessionController*> controller) {
	static base::weak_qptr<CenterWindow> active;
	if (const auto existing = active.get()) {
		existing->show();
		existing->raise();
		existing->activateWindow();
		return;
	}
	// Deliberately leaked (WA_DeleteOnClose schedules destruction); the
	// session guard below only force-closes it on session teardown.
	const auto window = new CenterWindow(nullptr, controller);
	active = window;
	controller->session().lifetime().add([weak = QPointer<CenterWindow>(window)] {
		if (weak) {
			weak->deleteLater();
		}
	});
	window->show();
	window->raise();
	window->activateWindow();
}

QString MonitorPeerName(
		not_null<Window::SessionController*> controller,
		long long barePeerId) {
	auto &data = controller->session().data();
	// Monitor stores the bare peer id without the chat type shift, so
	// try the known kinds in order of likelihood.
	const auto candidates = {
		peerFromChannel(ChannelId(barePeerId)),
		peerFromChat(ChatId(barePeerId)),
		peerFromUser(UserId(barePeerId)),
	};
	for (const auto &candidate : candidates) {
		if (const auto peer = data.peerLoaded(candidate)) {
			return peer->name();
		}
	}
	return u"ID %1"_q.arg(barePeerId);
}

QString MonitorTargetName(
		not_null<Window::SessionController*> controller,
		long long barePeerId,
		long long topicId) {
	auto base = MonitorPeerName(controller, barePeerId);
	if (topicId == 0) {
		return base;
	}
	if (const auto peer = controller->session().data().peerLoaded(
			peerFromChannel(ChannelId(barePeerId)))) {
		if (const auto channel = peer->asChannel()) {
			if (const auto forum = channel->forum()) {
				if (const auto topic = forum->topicFor(MsgId(topicId))) {
					return base + u" · "_q + topic->title();
				}
			}
		}
	}
	return base + u" · #%1"_q.arg(topicId);
}

QString MonitorFormatBytes(long long bytes) {
	if (bytes >= 1024 * 1024 * 1024) {
		return (bytes / (1024.0 * 1024 * 1024) >= 10.0)
			? u"%1 GB"_q.arg(bytes / (1024.0 * 1024 * 1024), 0, 'f', 0)
			: u"%1 GB"_q.arg(bytes / (1024.0 * 1024 * 1024), 0, 'f', 1);
	} else if (bytes >= 1024 * 1024) {
		return u"%1 MB"_q.arg(bytes / (1024.0 * 1024), 0, 'f', 1);
	} else if (bytes >= 1024) {
		return u"%1 KB"_q.arg(bytes / 1024.0, 0, 'f', 0);
	}
	return u"%1 B"_q.arg(bytes);
}

ConfirmOverlay::ConfirmOverlay(
	not_null<QWidget*> host,
	QString title,
	QString text,
	QString confirmText,
	Fn<void()> confirmed)
: Ui::RpWidget(host)
, _title(std::move(title))
, _text(std::move(text))
, _confirmText(std::move(confirmText))
, _confirmed(std::move(confirmed)) {
	setMouseTracking(true);
	host->installEventFilter(this);
	setGeometry(host->rect());
	show();
	raise();
}

void ConfirmOverlay::Show(
		not_null<QWidget*> host,
		QString title,
		QString text,
		QString confirmText,
		Fn<void()> confirmed) {
	const auto window = host->window();
	new ConfirmOverlay(
		window,
		std::move(title),
		std::move(text),
		std::move(confirmText),
		std::move(confirmed));
}

void ConfirmOverlay::layoutCard() {
	const auto pad = style::ConvertScale(16);
	const auto cardW = std::min(
		style::ConvertScale(220),
		width() - 2 * style::ConvertScale(20));
	const auto textW = cardW - 2 * pad;
	const auto textH = QFontMetrics(st::normalFont).boundingRect(
		QRect(0, 0, textW, 10000),
		Qt::TextWordWrap,
		_text).height();
	const auto btnH = style::ConvertScale(18);
	const auto cardH = style::ConvertScale(14)
		+ st::semiboldFont->height
		+ style::ConvertScale(6)
		+ textH
		+ style::ConvertScale(12)
		+ btnH
		+ style::ConvertScale(14);
	_card = QRect(
		(width() - cardW) / 2,
		(height() - cardH) / 2,
		cardW,
		cardH);
	const auto fm = QFontMetrics(st::semiboldFont);
	const auto cancelW = fm.horizontalAdvance(u"Cancel"_q)
		+ 2 * style::ConvertScale(12);
	const auto confirmW = fm.horizontalAdvance(_confirmText)
		+ 2 * style::ConvertScale(12);
	const auto btnY = _card.y() + cardH
		- style::ConvertScale(14) - btnH;
	_cancelButton = QRect(
		_card.x() + cardW - pad - confirmW - style::ConvertScale(5) - cancelW,
		btnY,
		cancelW,
		btnH);
	_confirmButton = QRect(
		_card.x() + cardW - pad - confirmW,
		btnY,
		confirmW,
		btnH);
}

void ConfirmOverlay::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	layoutCard();

	const auto cursor = hoverHostPos();
	const auto confirmHover = _confirmButton.contains(cursor);
	const auto cancelHover = _cancelButton.contains(cursor);

	p.fillRect(rect(), QColor(0, 0, 0, 120));
	{
		const auto hq = PainterHighQualityEnabler(p);
		p.setPen(st::shadowFg);
		p.setBrush(st::boxBg);
		p.drawRoundedRect(
			_card,
			style::ConvertScale(8),
			style::ConvertScale(8));
	}

	const auto innerLeft = _card.x() + style::ConvertScale(16);
	const auto innerWidth = _card.width() - 2 * style::ConvertScale(16);
	auto y = _card.y() + style::ConvertScale(14);
	p.setFont(st::semiboldFont);
	p.setPen(st::boxTitleFg);
	p.drawText(
		innerLeft,
		y + st::semiboldFont->height - style::ConvertScale(1),
		_title);
	y += st::semiboldFont->height + style::ConvertScale(6);
	p.setFont(st::normalFont);
	p.setPen(st::boxTextFg);
	p.drawText(QRect(innerLeft, y, innerWidth, height() - y), Qt::TextWordWrap, _text);

	p.setFont(st::semiboldFont);
	{
		const auto hq = PainterHighQualityEnabler(p);
		p.setPen(Qt::NoPen);
		p.setBrush(st::windowBgOver);
		p.setOpacity(cancelHover ? 1.0 : 0.7);
		p.drawRoundedRect(
			_cancelButton,
			style::ConvertScale(5),
			style::ConvertScale(5));
		p.setOpacity(1.0);
	}
	p.setPen(st::windowFg);
	p.drawText(_cancelButton, style::al_center, u"Cancel"_q);

	{
		const auto hq = PainterHighQualityEnabler(p);
		p.setPen(Qt::NoPen);
		p.setBrush(st::boxTextFgError);
		p.setOpacity(confirmHover ? 0.25 : 0.15);
		p.drawRoundedRect(
			_confirmButton,
			style::ConvertScale(5),
			style::ConvertScale(5));
		p.setOpacity(1.0);
	}
	p.setPen(st::boxTextFgError);
	p.drawText(_confirmButton, style::al_center, _confirmText);
}

QPoint ConfirmOverlay::hoverHostPos() const {
	// Live cursor position: hover states follow the pointer without
	// dedicated enter/leave handling (RpWidget finalizes leaveEvent).
	return mapFromGlobal(QCursor::pos());
}

void ConfirmOverlay::mousePressEvent(QMouseEvent *e) {
	const auto pos = e->pos();
	if (_confirmButton.contains(pos)) {
		const auto callback = _confirmed;
		// hide() first: deleteLater() only queues destruction, so a
		// second click in the same event-loop round would re-run the
		// callback without this.
		hide();
		deleteLater();
		if (callback) {
			callback();
		}
	} else if (_cancelButton.contains(pos) || !_card.contains(pos)) {
		deleteLater();
	}
}

void ConfirmOverlay::mouseMoveEvent(QMouseEvent *e) {
	const auto pos = e->pos();
	const auto hover = _confirmButton.contains(pos)
		|| _cancelButton.contains(pos);
	setCursor(hover ? style::cur_pointer : style::cur_default);
	update();
}

void ConfirmOverlay::resizeEvent(QResizeEvent *e) {
	layoutCard();
	Ui::RpWidget::resizeEvent(e);
}

bool ConfirmOverlay::eventFilter(QObject *obj, QEvent *e) {
	if (obj == parentWidget() && e->type() == QEvent::Resize) {
		setGeometry(parentWidget()->rect());
	}
	return Ui::RpWidget::eventFilter(obj, e);
}

} // namespace MonitorCenter
