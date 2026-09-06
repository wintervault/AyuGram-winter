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
#include "ui/widgets/scroll_area.h"
#include "ui/widgets/rp_window.h"
#include "window/window_session_controller.h"

#include <QPainter>
#include <QScreen>

namespace MonitorCenter {

enum class View {
	activity,
	targets,
};

class ViewSwitch final : public Ui::AbstractButton {
public:
	ViewSwitch(QWidget *parent, std::vector<QString> labels)
	: Ui::AbstractButton(parent) {
		const auto metrics = QFontMetrics(st::normalFont);
		auto x = kPadding;
		for (auto &label : labels) {
			const auto width = metrics.horizontalAdvance(label)
				+ 2 * kPadding
				+ kExtraWidth;
			_items.push_back({ label, x, width });
			x += width;
		}
		resize(x + kPadding, kHeight);
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
		for (auto i = 0; i != int(_items.size()); ++i) {
			const auto &item = _items[i];
			const auto active = (i == _active);
			const auto font = active ? st::semiboldFont : st::normalFont;
			const auto metrics = QFontMetrics(font);
			p.setFont(font);
			p.setPen(active ? st::windowBoldFg : st::windowSubTextFg);
			const auto baseline = (height() + metrics.ascent()) / 2
				- metrics.descent() / 2;
			p.drawText(item.left, baseline, item.text);
			if (active) {
				p.fillRect(
					item.left + kPadding,
					height() - kUnderline,
					item.width - 2 * kPadding,
					kUnderline,
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
	static constexpr auto kPadding = 10;
	static constexpr auto kExtraWidth = 4;
	static constexpr auto kHeight = 44;
	static constexpr auto kUnderline = 2;

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
		auto geometry = QRect(0, 0, 760, 640);
		geometry.moveCenter(available.center());
		setGeometry(geometry);
	}

	body()->paintRequest(
	) | rpl::on_next([=](QRect clip) {
		QPainter(body().get()).fillRect(clip, st::windowBg);
	}, body()->lifetime());

	_header->setGeometry(0, 0, body()->width(), 44);
	_header->show();
	_switch->moveToRight(12, 0);
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

	_scroll->move(0, 44);
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
	const auto margins = frameMargins();
	const auto headerTop = margins.top();
	const auto contentWidth = width() - margins.left() - margins.right();
	_header->setGeometry(
		margins.left(),
		headerTop,
		contentWidth,
		44);
	_switch->moveToRight(12, 0);
	_scroll->setGeometry(
		margins.left(),
		headerTop + 44,
		contentWidth,
		height() - headerTop - 44 - margins.bottom());
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

} // namespace MonitorCenter
