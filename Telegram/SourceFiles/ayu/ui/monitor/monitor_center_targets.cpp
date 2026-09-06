// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "ayu/ui/monitor/monitor_center_targets.h"

#include "ayu/data/ayu_database.h"
#include "ayu/data/entities.h"
#include "ayu/features/monitor/monitor.h"
#include "ayu/ui/monitor/monitor_center.h"
#include "data/data_peer_id.h"
#include "data/data_session.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "styles/style_boxes.h"
#include "styles/style_settings.h"
#include "styles/style_window.h"
#include "ui/boxes/confirm_box.h"
#include "ui/painter.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "window/window_session_controller.h"

#include <map>

namespace MonitorCenter {
namespace {

constexpr auto kRowHeaderHeight = 48;
constexpr auto kRowEditorLineHeight = 30;
constexpr auto kRowEditorPad = 12;
constexpr auto kRowRemoveHeight = 44;

constexpr auto kTypeCount = 7;

const std::vector<QString> &TypeLabels() {
	static const auto result = std::vector<QString>{
		u"Photo"_q,
		u"Video"_q,
		u"Voice"_q,
		u"Audio"_q,
		u"Video note"_q,
		u"GIF"_q,
		u"Document"_q,
	};
	return result;
}

const std::vector<std::string> &TypeNames() {
	static const auto result = std::vector<std::string>{
		"photo",
		"video",
		"voice",
		"audio",
		"video_note",
		"gif",
		"document",
	};
	return result;
}

[[nodiscard]] std::vector<QString> ParseTypes(const std::string &mediaTypes) {
	auto result = std::vector<QString>();
	const auto parts = QString::fromStdString(mediaTypes)
		.split(',', Qt::SkipEmptyParts);
	for (const auto &part : parts) {
		result.push_back(part.trimmed());
	}
	return result;
}

} // namespace

// A standalone switch widget built on ToggleView.
class ToggleWidget final : public Ui::RpWidget {
public:
	ToggleWidget(QWidget *parent, bool checked)
	: Ui::RpWidget(parent)
	, _view(st::defaultToggle, checked, [=] { update(); }) {
		setCursor(style::cur_pointer);
		resize(_view.getSize());
	}

	void setChecked(bool value) {
		_view.setChecked(value, anim::type::instant);
	}

	[[nodiscard]] rpl::producer<bool> checkedChanges() const {
		return _view.checkedChanges();
	}

protected:
	void paintEvent(QPaintEvent *e) override {
		auto p = QPainter(this);
		_view.paint(p, 0, 0, width());
	}

	void mousePressEvent(QMouseEvent *e) override {
		_view.setChecked(!_view.checked(), anim::type::normal);
	}

private:
	Ui::ToggleView _view;

};

class TargetsView::Row final : public Ui::RpWidget {
public:
	Row(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		MonitorTarget target,
		int doneCount,
		int64 doneBytes,
		int failedCount,
		Fn<void()> changed,
		Fn<void()> geometryChanged);

protected:
	void paintEvent(QPaintEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	int resizeGetHeight(int newWidth) override;

private:
	void saveTypes();
	void updateChildrenGeometry(int newWidth);

	const not_null<Window::SessionController*> _controller;
	MonitorTarget _target;
	int _doneCount = 0;
	int64 _doneBytes = 0;
	int _failedCount = 0;
	Fn<void()> _changed;
	Fn<void()> _geometryChanged;

	bool _expanded = false;
	std::vector<Ui::Checkbox*> _typeChecks;
	object_ptr<ToggleWidget> _toggle;
	object_ptr<Ui::SettingsButton> _remove;

};

TargetsView::TargetsView(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Ui::RpWidget(parent)
, _controller(controller) {
	reload();
}

void TargetsView::reload() {
	_rows.clear();

	const auto session = &_controller->session();
	const auto userId = session->userId().bare & PeerId::kChatTypeMask;
	auto done = std::map<long long, int>();
	auto failed = std::map<long long, int>();
	auto bytes = std::map<long long, int64>();
	for (const auto &entry : AyuDatabase::Monitor::getTargetStats(userId)) {
		done.emplace(entry.peerId, entry.doneCount);
		failed.emplace(entry.peerId, entry.failedCount);
		bytes.emplace(entry.peerId, entry.doneBytes);
	}

	for (const auto &target : AyuDatabase::Monitor::getAllMonitorTargets(userId)) {
		auto row = object_ptr<Row>(
			this,
			_controller,
			target,
			done.contains(target.peerId) ? done[target.peerId] : 0,
			bytes.contains(target.peerId) ? bytes[target.peerId] : 0,
			failed.contains(target.peerId) ? failed[target.peerId] : 0,
			[=] { reload(); },
			[=] { relayout(); });
		_rows.push_back(std::move(row));
	}
	resizeToWidth(width());
	update();
}

void TargetsView::relayout() {
	resizeToWidth(width());
	update();
}

int TargetsView::resizeGetHeight(int newWidth) {
	auto y = 40;
	for (const auto &row : _rows) {
		row->resizeToWidth(newWidth);
		row->setGeometry(0, y, newWidth, row->height());
		y += row->height();
	}
	return y + 16;
}

void TargetsView::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	p.fillRect(e->rect(), st::boxBg);

	if (_rows.empty()) {
		p.setFont(st::normalFont);
		p.setPen(st::windowSubTextFg);
		p.drawText(
			rect().marginsRemoved(QMargins(24, 24, 24, 24)),
			style::al_center | Qt::TextWordWrap,
			u"No monitored chats yet.\nUse the chat context menu to add one."_q);
		return;
	}
	p.setFont(st::normalFont);
	p.setPen(st::windowSubTextFg);
	p.drawText(
		16,
		26,
		u"Targets (add via the chat context menu)"_q);
}

TargetsView::Row::Row(
	QWidget *parent,
	not_null<Window::SessionController*> controller,
	MonitorTarget target,
	int doneCount,
	int64 doneBytes,
	int failedCount,
	Fn<void()> changed,
	Fn<void()> geometryChanged)
: Ui::RpWidget(parent)
, _controller(controller)
, _target(std::move(target))
, _doneCount(doneCount)
, _doneBytes(doneBytes)
, _failedCount(failedCount)
, _changed(std::move(changed))
, _geometryChanged(std::move(geometryChanged))
, _toggle(this, _target.enabled)
, _remove(
		this,
		rpl::single(u"Remove target"_q),
		st::settingsButton) {
	_toggle->setChecked(_target.enabled);
	_toggle->checkedChanges(
	) | rpl::on_next([=](bool checked) {
		_target.enabled = checked;
		AyuDatabase::Monitor::upsertMonitorTarget(_target);
		AyuFeatures::Monitor::InvalidateTargetsCache();
	}, _toggle->lifetime());

	const auto types = ParseTypes(_target.mediaTypes);
	const auto empty = _target.mediaTypes.empty();
	for (auto i = 0; i != kTypeCount; ++i) {
		const auto checked = empty
			|| (std::find(
					types.begin(),
					types.end(),
					QString::fromStdString(TypeNames()[i]))
				!= types.end());
		auto check = object_ptr<Ui::Checkbox>(
			this,
			TypeLabels()[i],
			checked,
			st::defaultCheckbox);
		const auto raw = check.data();
		raw->checkedChanges(
		) | rpl::on_next([=](bool) {
			saveTypes();
		}, raw->lifetime());
		_typeChecks.push_back(raw);
	}
	const auto ownTarget = _target;
	const auto ownChanged = _changed;
	_remove->addClickHandler([=, this] {
		_controller->show(Ui::MakeConfirmBox({
			.text = u"Stop monitoring this chat?"_q,
			.confirmed = [=](Fn<void()> &&close) {
				AyuDatabase::Monitor::removeMonitorTarget(
					ownTarget.userId,
					ownTarget.peerId,
					ownTarget.topicId);
				AyuFeatures::Monitor::InvalidateTargetsCache();
				close();
				ownChanged();
			},
			.confirmText = tr::lng_box_delete(),
		}));
	});
	// Editor controls live in updateChildrenGeometry(); keep them out of
	// sight until the first layout actually places them.
	for (const auto check : _typeChecks) {
		check->hide();
	}
	_remove->hide();
}

void TargetsView::Row::saveTypes() {
	auto kept = std::vector<std::string>();
	auto all = true;
	for (auto i = 0; i != kTypeCount; ++i) {
		if (_typeChecks[i]->checked()) {
			kept.push_back(TypeNames()[i]);
		} else {
			all = false;
		}
	}
	if (all) {
		// Everything allowed = follow the global settings.
		_target.mediaTypes.clear();
	} else {
		auto joined = std::string();
		for (auto i = 0; i != int(kept.size()); ++i) {
			if (i) {
				joined += ',';
			}
			joined += kept[i];
		}
		_target.mediaTypes = joined;
	}
	AyuDatabase::Monitor::upsertMonitorTarget(_target);
	AyuFeatures::Monitor::InvalidateTargetsCache();
}

int TargetsView::Row::resizeGetHeight(int newWidth) {
	updateChildrenGeometry(newWidth);
	return _expanded
		? kRowHeaderHeight
			+ kRowEditorPad + kTypeCount * kRowEditorLineHeight
			+ kRowEditorPad + kRowRemoveHeight
		: kRowHeaderHeight;
}

void TargetsView::Row::updateChildrenGeometry(int newWidth) {
	_toggle->move(
		newWidth - 16 - _toggle->width(),
		(kRowHeaderHeight - _toggle->height()) / 2);
	for (const auto check : _typeChecks) {
		check->setVisible(_expanded);
	}
	_remove->setVisible(_expanded);
	if (_expanded) {
		auto y = kRowHeaderHeight + kRowEditorPad;
		for (const auto check : _typeChecks) {
			check->moveToLeft(32, y);
			check->resizeToNaturalWidth(newWidth - 64);
			y += kRowEditorLineHeight;
		}
		y += kRowEditorPad;
		_remove->setGeometry(24, y, newWidth - 48, kRowRemoveHeight - 4);
	}
}

void TargetsView::Row::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	const auto w = width();

	p.setFont(st::semiboldFont);
	p.setPen(st::windowFg);
	p.drawText(
		16,
		kRowHeaderHeight / 2 + st::semiboldFont->height / 2
			- st::semiboldFont->descent,
		MonitorPeerName(_controller, _target.peerId));

	p.setFont(st::normalFont);
	p.setPen(_failedCount > 0 ? st::boxTextFgError : st::windowSubTextFg);
	const auto stats = u"%1 · %2"_q
		.arg(_doneCount)
		.arg(MonitorFormatBytes(_doneBytes));
	const auto statsRight = w - 16 - _toggle->width() - 12;
	p.drawText(
		statsRight - QFontMetrics(st::normalFont).horizontalAdvance(stats),
		kRowHeaderHeight / 2 + st::normalFont->height / 2
			- st::normalFont->descent,
		stats);

	p.fillRect(0, kRowHeaderHeight - 1, w, 1, st::shadowFg);
	if (_expanded) {
		p.fillRect(0, height() - 1, w, 1, st::shadowFg);
	}
}

void TargetsView::Row::mousePressEvent(QMouseEvent *e) {
	// Only the header toggles expansion; clicks in the editor area are
	// left to its own controls.
	if (e->pos().y() >= kRowHeaderHeight) {
		return;
	}
	_expanded = !_expanded;
	resizeToWidth(width());
	update();
	_geometryChanged();
}

} // namespace MonitorCenter
