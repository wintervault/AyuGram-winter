// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "ayu/ui/monitor/monitor_center.h"

#include "ayu/ui/monitor/monitor_center_activity.h"
#include "ayu/ui/monitor/monitor_center_targets.h"
#include "data/data_peer.h"
#include "data/data_peer_id.h"
#include "data/data_session.h"
#include "main/main_session.h"
#include "lang/lang_keys.h"
#include "profile/profile_back_button.h"
#include "styles/style_basic.h"
#include "styles/style_chat.h"
#include "styles/style_window.h"
#include "ui/abstract_button.h"
#include "ui/ui_utility.h"
#include "ui/widgets/scroll_area.h"
#include "ui/widgets/shadow.h"
#include "window/section_widget.h"
#include "window/section_memento.h"
#include "window/window_session_controller.h"

#include <QPainter>
#include <QPointer>

namespace MonitorCenter {

enum class View {
	activity,
	targets,
};

class SectionMemento;

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
	static constexpr auto kHeight = 34;
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

class FixedBar final : public Ui::RpWidget {
public:
	FixedBar(
		QWidget *parent,
		not_null<Window::SessionController*> controller)
	: Ui::RpWidget(parent)
	, _controller(controller)
	, _back(this)
	, _switch(this, std::vector<QString>{
		tr::ayu_MonitorCenterActivity(tr::now),
		tr::ayu_MonitorCenterTargets(tr::now) }) {
		_back->setText(tr::ayu_MonitorCenter(tr::now));
		_back->setClickedCallback([=] { goBack(); });
	}

	[[nodiscard]] rpl::producer<int> viewRequests() const {
		return _switch->activated();
	}

	void setView(int index) {
		_switch->setActive(index);
	}

	// When animating mode is enabled the content is hidden and the
	// whole fixed bar acts like a back button.
	void setAnimatingMode(bool enabled) {
		if (_animatingMode != enabled) {
			_animatingMode = enabled;
			setCursor(_animatingMode ? style::cur_pointer : style::cur_default);
			if (_animatingMode) {
				setAttribute(Qt::WA_OpaquePaintEvent, false);
				hideChildren();
			} else {
				setAttribute(Qt::WA_OpaquePaintEvent);
				showChildren();
			}
			show();
		}
	}

protected:
	void paintEvent(QPaintEvent *e) override {
		if (!_animatingMode) {
			auto p = QPainter(this);
			p.fillRect(e->rect(), st::topBarBg);
		}
	}

	int resizeGetHeight(int newWidth) override {
		const auto switchWidth = _switch->width();
		_back->resizeToWidth(newWidth - switchWidth);
		_back->moveToLeft(0, 0);
		_switch->moveToRight(
			0,
			(_back->height() - _switch->height()) / 2);
		return _back->height();
	}

private:
	void goBack() {
		_controller->showBackFromStack();
	}

	not_null<Window::SessionController*> _controller;
	object_ptr<Profile::BackButton> _back;
	object_ptr<ViewSwitch> _switch;
	bool _animatingMode = false;

};

class Widget final : public Window::SectionWidget {
public:
	Widget(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

	bool hasTopBarShadow() const override {
		return true;
	}

	QPixmap grabForShowAnimation(
		const Window::SectionSlideParams &params) override;

	bool showInternal(
		not_null<Window::SectionMemento*> memento,
		const Window::SectionShow &params) override;
	std::shared_ptr<Window::SectionMemento> createMemento() override;

	void setInternalState(const QRect &geometry, not_null<SectionMemento*> memento);

	// Float player interface.
	bool floatPlayerHandleWheelEvent(QEvent *e) override;
	QRect floatPlayerAvailableRect() override;

protected:
	void resizeEvent(QResizeEvent *e) override;
	void paintEvent(QPaintEvent *e) override;
	void showAnimatedHook(const Window::SectionSlideParams &params) override;
	void showFinishedHook() override;
	void doSetInnerFocus() override;

private:
	void showView(View view);
	void restoreState(not_null<SectionMemento*> memento);

	object_ptr<Ui::ScrollArea> _scroll;
	QPointer<Ui::RpWidget> _content;
	QPointer<ActivityView> _activity;
	object_ptr<FixedBar> _fixedBar;
	object_ptr<Ui::PlainShadow> _fixedBarShadow;
	View _view = View::activity;

};

class SectionMemento final : public Window::SectionMemento {
public:
	SectionMemento() = default;

	object_ptr<Window::SectionWidget> createWidget(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		Window::Column column,
		const QRect &geometry) override {
		if (column == Window::Column::Third) {
			return nullptr;
		}
		auto result = object_ptr<Widget>(parent, controller);
		result->setInternalState(geometry, this);
		return result;
	}

	void setView(View view) {
		_view = view;
	}

	[[nodiscard]] View view() const {
		return _view;
	}

	void setScrollTop(int scrollTop) {
		_scrollTop = scrollTop;
	}

	[[nodiscard]] int scrollTop() const {
		return _scrollTop;
	}

private:
	View _view = View::activity;
	int _scrollTop = 0;

};

Widget::Widget(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Window::SectionWidget(parent, controller)
, _scroll(this, st::historyScroll, false)
, _fixedBar(this, controller)
, _fixedBarShadow(this) {
	_fixedBar->move(0, 0);
	_fixedBar->resizeToWidth(width());
	_fixedBar->show();

	_fixedBarShadow->raise();

	_fixedBar->viewRequests(
	) | rpl::on_next([=](int index) {
		showView(index == 0 ? View::activity : View::targets);
	}, lifetime());

	_scroll->move(0, _fixedBar->height());
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

void Widget::showView(View view) {
	if (_view == view && _content) {
		return;
	}
	_view = view;
	_fixedBar->setView(view == View::activity ? 0 : 1);
	if (view == View::activity) {
		_activity = _scroll->setOwnedWidget(
			object_ptr<ActivityView>(this, controller()));
		_content = _activity.data();
	} else {
		_content = _scroll->setOwnedWidget(
			object_ptr<TargetsView>(this, controller()));
	}
	if (_content) {
		_content->resizeToWidth(_scroll->width());
		_content->show();
	}
	_scroll->scrollToY(0);
}

QPixmap Widget::grabForShowAnimation(
		const Window::SectionSlideParams &params) {
	if (params.withTopBarShadow) {
		_fixedBarShadow->hide();
	}
	auto result = Ui::GrabWidget(this);
	if (params.withTopBarShadow) {
		_fixedBarShadow->show();
	}
	return result;
}

void Widget::doSetInnerFocus() {
	_scroll->setFocus();
}

bool Widget::floatPlayerHandleWheelEvent(QEvent *e) {
	return _scroll->viewportEvent(e);
}

QRect Widget::floatPlayerAvailableRect() {
	return mapToGlobal(_scroll->geometry());
}

bool Widget::showInternal(
		not_null<Window::SectionMemento*> memento,
		const Window::SectionShow &params) {
	if (const auto my = dynamic_cast<SectionMemento*>(memento.get())) {
		restoreState(my);
		return true;
	}
	return false;
}

std::shared_ptr<Window::SectionMemento> Widget::createMemento() {
	auto result = std::make_shared<SectionMemento>();
	result->setView(_view);
	result->setScrollTop(_scroll->scrollTop());
	return result;
}

void Widget::setInternalState(
		const QRect &geometry,
		not_null<SectionMemento*> memento) {
	setGeometry(geometry);
	Ui::SendPendingMoveResizeEvents(this);
	restoreState(memento);
}

void Widget::restoreState(not_null<SectionMemento*> memento) {
	_view = memento->view();
	showView(_view);
	_scroll->scrollToY(memento->scrollTop());
}

void Widget::resizeEvent(QResizeEvent *e) {
	if (!width() || !height()) {
		return;
	}
	_fixedBar->resizeToWidth(width());
	_fixedBarShadow->resize(width(), st::lineWidth);
	_scroll->setGeometry(
		0,
		_fixedBar->height(),
		width(),
		height() - _fixedBar->height());
	if (_content) {
		_content->resizeToWidth(_scroll->width());
	}
}

void Widget::paintEvent(QPaintEvent *e) {
	if (animatingShow()) {
		SectionWidget::paintEvent(e);
		return;
	} else if (controller()->contentOverlapped(this, e)) {
		return;
	}
	SectionWidget::PaintBackground(
		controller(),
		controller()->currentChatTheme(),
		this,
		e->rect());
}

void Widget::showAnimatedHook(const Window::SectionSlideParams &params) {
	_fixedBar->setAnimatingMode(true);
	if (params.withTopBarShadow) {
		_fixedBarShadow->show();
	}
}

void Widget::showFinishedHook() {
	_fixedBar->setAnimatingMode(false);
}

void ShowMonitorCenter(not_null<Window::SessionController*> controller) {
	controller->showSection(std::make_shared<SectionMemento>());
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
